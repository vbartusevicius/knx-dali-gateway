#include "firmware/NetworkServices.hpp"

#include <array>
#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_client.h>
#include <sys/time.h>

#include "firmware/Board.hpp"
#include "firmware/RuntimeState.hpp"
#include "firmware/Version.hpp"
#include "firmware/WebAssets.hpp"
#include "network/ApiCodec.hpp"
#include "network/HaDiscovery.hpp"
#include "network/NetworkConfig.hpp"

namespace gateway::firmware {
namespace {

constexpr const char* log_tag = "net";
constexpr std::size_t max_ws_clients = 6;
constexpr std::size_t scratch_json_size = 16384;

// Pre-allocated static buffers so HTTP/WS/MQTT never fragment the heap.
char g_http_scratch[scratch_json_size]{};
char g_ws_scratch[scratch_json_size]{};
SemaphoreHandle_t g_http_mutex = nullptr;
StaticSemaphore_t g_http_mutex_buf{};

Queue<control::Command>* g_commands = nullptr;
I2cOled* g_oled = nullptr;
httpd_handle_t g_httpd = nullptr;
esp_mqtt_client_handle_t g_mqtt = nullptr;

network::ReconnectBackoff g_wifi_backoff{};
network::ReconnectBackoff g_mqtt_backoff{};
device::WifiState g_wifi_state = device::WifiState::Off;
bool g_mqtt_connected = false;
bool g_ha_gateway_published = false;
std::bitset<64> g_ha_published_channels;
bool g_reload_requested = false;
bool g_ap_portal_active = false;
char g_ipv4[16]{"0.0.0.0"};
char g_chip_id[12]{"0000000"};

void init_chip_id() noexcept {
    std::uint8_t mac[6]{};
    (void)esp_efuse_mac_get_default(mac);
    const std::uint32_t tail =
        (static_cast<std::uint32_t>(mac[2] & 0x0FU) << 24U) | (static_cast<std::uint32_t>(mac[3]) << 16U) |
        (static_cast<std::uint32_t>(mac[4]) << 8U) | static_cast<std::uint32_t>(mac[5]);
    std::snprintf(g_chip_id, sizeof(g_chip_id), "%07lx", static_cast<unsigned long>(tail));
}

std::uint32_t now_ms() noexcept {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void lock_http() noexcept {
    if (g_http_mutex) {
        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
    }
}

void unlock_http() noexcept {
    if (g_http_mutex) {
        xSemaphoreGive(g_http_mutex);
    }
}

struct WebFsEntry {
    char path[32];
    char mime[40];
    std::uint32_t offset;
    std::uint32_t size;
};

// Serves a file from the `storage` flash partition if a valid `littlefs.bin`
// image ("KDWFS1\0\0") was flashed or uploaded via /api/ota/littlefs.
bool serve_from_storage_partition(httpd_req_t* req, const char* route) noexcept {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!part) {
        return false;
    }
    struct Header {
        char magic[8];
        std::uint32_t count;
    } hdr{};
    if (esp_partition_read(part, 0, &hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    if (std::memcmp(hdr.magic, "KDWFS1\0\0", 8) != 0 || hdr.count == 0 || hdr.count > 16) {
        return false;
    }
    for (std::uint32_t i = 0; i < hdr.count; ++i) {
        WebFsEntry entry{};
        if (esp_partition_read(part, sizeof(hdr) + i * sizeof(entry), &entry, sizeof(entry)) != ESP_OK) {
            return false;
        }
        entry.path[sizeof(entry.path) - 1] = '\0';
        entry.mime[sizeof(entry.mime) - 1] = '\0';
        if (std::strcmp(entry.path, route) != 0) {
            continue;
        }
        if (entry.offset + entry.size > part->size) {
            return false;
        }
        httpd_resp_set_type(req, entry.mime);
        std::array<char, 1024> chunk{};
        std::uint32_t remaining = entry.size;
        std::uint32_t pos = entry.offset;
        while (remaining > 0) {
            const std::uint32_t n =
                remaining > chunk.size() ? static_cast<std::uint32_t>(chunk.size()) : remaining;
            if (esp_partition_read(part, pos, chunk.data(), n) != ESP_OK) {
                return false;
            }
            if (httpd_resp_send_chunk(req, chunk.data(), static_cast<ssize_t>(n)) != ESP_OK) {
                return false;
            }
            pos += n;
            remaining -= n;
        }
        httpd_resp_send_chunk(req, nullptr, 0);
        return true;
    }
    return false;
}

esp_err_t handle_static_asset(httpd_req_t* req) {
    const char* uri = req->uri;
    const char* route = (std::strcmp(uri, "/") == 0) ? "/index.html" : uri;
    if (serve_from_storage_partition(req, route)) {
        return ESP_OK;
    }
    if (std::strcmp(route, "/index.html") == 0) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, web_assets::index_html_data,
                               static_cast<ssize_t>(web_assets::index_html_size));
    }
    if (std::strcmp(route, "/app.css") == 0) {
        httpd_resp_set_type(req, "text/css; charset=utf-8");
        return httpd_resp_send(req, web_assets::app_css_data, static_cast<ssize_t>(web_assets::app_css_size));
    }
    if (std::strcmp(route, "/app.js") == 0) {
        httpd_resp_set_type(req, "application/javascript; charset=utf-8");
        return httpd_resp_send(req, web_assets::app_js_data, static_cast<ssize_t>(web_assets::app_js_size));
    }
    return httpd_resp_send_404(req);
}

void execute_web_json_payload(const char* buf, std::size_t len) noexcept {
    if (std::strstr(buf, "\"oled_page\"")) {
        runtime_state().next_oled_page(now_ms());
        return;
    }
    if (std::strstr(buf, "\"reset_psu\"")) {
        runtime_state().request_psu_reset();
        return;
    }
    if (std::strstr(buf, "\"oled_toggle\"")) {
        runtime_state().toggle_oled_geometry(now_ms());
        if (g_oled) {
            g_oled->reconfigure(runtime_state().network_config().oled_geometry);
        }
        return;
    }
    if (std::strstr(buf, "\"reboot\"")) {
        esp_restart();
    }
    if (const char* p = std::strstr(buf, "\"sync_time\"")) {
        (void)p;
        if (const char* ep = std::strstr(buf, "\"epoch_ms\"")) {
            const char* colon = std::strchr(ep, ':');
            if (colon) {
                const auto epoch = static_cast<std::uint64_t>(std::strtoull(colon + 1, nullptr, 10));
                runtime_state().log().sync_wall_clock(epoch, now_ms());
            }
        }
        return;
    }
    if (const auto cmd = network::parse_api_command_json(buf, len)) {
        if (g_commands && !g_commands->try_push(*cmd)) {
            runtime_state().log().record(diagnostics::LogSource::System, diagnostics::LogSeverity::Warning,
                                         diagnostics::ErrorCode::KnxCommandQueueFull, diagnostics::no_address,
                                         "Web API command queue full", now_ms());
        }
    }
}

esp_err_t handle_ws(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        return ESP_OK;
    }
    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &pkt, 0) != ESP_OK || pkt.len == 0 || pkt.len > 1023) {
        return ESP_OK;
    }
    std::array<std::uint8_t, 1024> payload{};
    pkt.payload = payload.data();
    if (httpd_ws_recv_frame(req, &pkt, pkt.len) == ESP_OK) {
        payload[pkt.len] = 0;
        execute_web_json_payload(reinterpret_cast<const char*>(payload.data()), pkt.len);
    }
    return ESP_OK;
}

esp_err_t handle_api_state(httpd_req_t* req) {
    lock_http();
    const std::size_t n =
        runtime_state().format_live_ws_json(g_http_scratch, sizeof(g_http_scratch), now_ms());
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_api_channel(httpd_req_t* req) {
    char query[32]{};
    unsigned ch = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[12]{};
        if (httpd_query_key_value(query, "ch", val, sizeof(val)) == ESP_OK) {
            ch = static_cast<unsigned>(std::strtoul(val, nullptr, 10));
        }
    }
    lock_http();
    const std::size_t n = runtime_state().format_channel_detail_json(static_cast<std::uint8_t>(ch & 0x3FU),
                                                                     g_http_scratch, sizeof(g_http_scratch));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_api_command(httpd_req_t* req) {
    std::array<char, 1024> buf{};
    const int received = httpd_req_recv(req, buf.data(), buf.size() - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty command body");
    }
    buf[static_cast<std::size_t>(received)] = '\0';
    execute_web_json_payload(buf.data(), static_cast<std::size_t>(received));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handle_api_config_get(httpd_req_t* req) {
    const auto cfg = runtime_state().network_config();
    lock_http();
    const std::size_t n = network::format_network_config_json(cfg, g_http_scratch, sizeof(g_http_scratch));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_api_config_post(httpd_req_t* req) {
    std::array<char, 1024> buf{};
    const int received = httpd_req_recv(req, buf.data(), buf.size() - 1);
    if (received <= 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty config body");
    }
    buf[static_cast<std::size_t>(received)] = '\0';
    auto cfg = runtime_state().network_config();
    if (!network::apply_network_config_json(cfg, buf.data(), static_cast<std::size_t>(received))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    runtime_state().save_network_config(cfg);
    gpio_set_level(board::dali_supply_off, cfg.internal_dali_psu_enabled ? 0 : 1);
    if (g_oled) {
        g_oled->reconfigure(cfg.oled_geometry);
    }
    g_reload_requested = true;
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handle_api_logs_get(httpd_req_t* req) {
    lock_http();
    const std::size_t n = runtime_state().format_logs_json(g_http_scratch, sizeof(g_http_scratch));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_api_logs_clear(httpd_req_t* req) {
    runtime_state().log().clear();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handle_api_debug_get(httpd_req_t* req) {
    lock_http();
    const std::size_t n = runtime_state().format_trace_json(g_http_scratch, sizeof(g_http_scratch));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_api_debug_post(httpd_req_t* req) {
    std::array<char, 256> buf{};
    const int received = httpd_req_recv(req, buf.data(), buf.size() - 1);
    if (received > 0) {
        buf[static_cast<std::size_t>(received)] = '\0';
        auto& tr = runtime_state().trace();
        if (std::strstr(buf.data(), "\"auto_trigger\":true")) {
            tr.set_auto_trigger_from_dali(true);
        } else if (std::strstr(buf.data(), "\"auto_trigger\":false")) {
            tr.set_auto_trigger_from_dali(false);
        }
        if (std::strstr(buf.data(), "\"start\"")) {
            tr.start_session(now_ms(), false);
        } else if (std::strstr(buf.data(), "\"stop\"")) {
            tr.stop_session();
        } else if (std::strstr(buf.data(), "\"clear\"")) {
            tr.clear();
        }
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t handle_api_errors_get(httpd_req_t* req) {
    lock_http();
    const std::size_t n = network::format_error_catalog_json(g_http_scratch, sizeof(g_http_scratch));
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, g_http_scratch, static_cast<ssize_t>(n));
    unlock_http();
    return err;
}

esp_err_t handle_ota_firmware(httpd_req_t* req) {
    if (req->content_len == 0 || req->content_len > 0x200000U) {
        runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                     diagnostics::ErrorCode::OtaValidationFailed, diagnostics::no_address,
                                     "firmware.bin invalid content_len", now_ms());
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid firmware.bin size");
    }
    const esp_partition_t* update_part = esp_ota_get_next_update_partition(nullptr);
    if (!update_part) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition available");
    }
    esp_ota_handle_t ota_handle = 0;
    std::array<char, 1024> chunk{};
    std::size_t remaining = req->content_len;
    bool first_chunk = true;

    while (remaining > 0) {
        const int to_read =
            remaining > chunk.size() ? static_cast<int>(chunk.size()) : static_cast<int>(remaining);
        const int r = httpd_req_recv(req, chunk.data(), to_read);
        if (r <= 0) {
            if (ota_handle != 0) {
                esp_ota_abort(ota_handle);
            }
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        }
        if (first_chunk) {
            first_chunk = false;
            if (static_cast<std::uint8_t>(chunk[0]) != 0xE9U) {
                runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                             diagnostics::ErrorCode::OtaValidationFailed,
                                             diagnostics::no_address, "bad ESP-IDF image magic!=0xE9",
                                             now_ms());
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid ESP32-C6 image magic byte");
            }
            if (esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle) != ESP_OK) {
                runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                             diagnostics::ErrorCode::OtaFlashFailed, diagnostics::no_address,
                                             "esp_ota_begin failed", now_ms());
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
            }
        }
        if (esp_ota_write(ota_handle, chunk.data(), static_cast<std::size_t>(r)) != ESP_OK) {
            esp_ota_abort(ota_handle);
            runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                         diagnostics::ErrorCode::OtaFlashFailed, diagnostics::no_address,
                                         "esp_ota_write failed", now_ms());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_write failed");
        }
        remaining -= static_cast<std::size_t>(r);
    }

    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(update_part) != ESP_OK) {
        runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                     diagnostics::ErrorCode::OtaValidationFailed, diagnostics::no_address,
                                     "esp_ota_end/set_boot failed", now_ms());
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image verification failed");
    }

    runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Info,
                                 diagnostics::ErrorCode::None, diagnostics::no_address,
                                 "Firmware OTA succeeded; rebooting", now_ms());
    (void)runtime_state().log().flush_to_storage();
    httpd_resp_sendstr(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_ota_littlefs(httpd_req_t* req) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!part) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "storage partition not found");
    }
    if (req->content_len < 16 || req->content_len > part->size) {
        runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                     diagnostics::ErrorCode::OtaValidationFailed, diagnostics::no_address,
                                     "littlefs.bin invalid size", now_ms());
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid littlefs.bin size");
    }

    const std::size_t erase_size = ((req->content_len + 4095U) / 4096U) * 4096U;
    if (esp_partition_erase_range(part, 0, erase_size) != ESP_OK) {
        runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                     diagnostics::ErrorCode::OtaFlashFailed, diagnostics::no_address,
                                     "storage partition erase failed", now_ms());
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash erase failed");
    }

    std::array<char, 1024> chunk{};
    std::size_t remaining = req->content_len;
    std::size_t offset = 0;
    while (remaining > 0) {
        const int to_read =
            remaining > chunk.size() ? static_cast<int>(chunk.size()) : static_cast<int>(remaining);
        const int r = httpd_req_recv(req, chunk.data(), to_read);
        if (r <= 0) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
        }
        if (esp_partition_write(part, offset, chunk.data(), static_cast<std::size_t>(r)) != ESP_OK) {
            runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Error,
                                         diagnostics::ErrorCode::OtaFlashFailed, diagnostics::no_address,
                                         "storage partition write failed", now_ms());
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
        }
        offset += static_cast<std::size_t>(r);
        remaining -= static_cast<std::size_t>(r);
    }

    runtime_state().log().record(diagnostics::LogSource::Ota, diagnostics::LogSeverity::Info,
                                 diagnostics::ErrorCode::None, diagnostics::no_address,
                                 "LittleFS Web UI partition updated", now_ms());
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

void start_http_server() noexcept {
    if (g_httpd) {
        return;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 18;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.stack_size = 6144;

    if (httpd_start(&g_httpd, &config) != ESP_OK) {
        ESP_LOGE(log_tag, "Failed to start HTTP/WebSocket server");
        return;
    }

    const httpd_uri_t uris[] = {
        {"/", HTTP_GET, &handle_static_asset, nullptr, false, false, nullptr},
        {"/index.html", HTTP_GET, &handle_static_asset, nullptr, false, false, nullptr},
        {"/app.css", HTTP_GET, &handle_static_asset, nullptr, false, false, nullptr},
        {"/app.js", HTTP_GET, &handle_static_asset, nullptr, false, false, nullptr},
        {"/ws", HTTP_GET, &handle_ws, nullptr, true, false, nullptr},
        {"/api/state", HTTP_GET, &handle_api_state, nullptr, false, false, nullptr},
        {"/api/channel", HTTP_GET, &handle_api_channel, nullptr, false, false, nullptr},
        {"/api/command", HTTP_POST, &handle_api_command, nullptr, false, false, nullptr},
        {"/api/config", HTTP_GET, &handle_api_config_get, nullptr, false, false, nullptr},
        {"/api/config", HTTP_POST, &handle_api_config_post, nullptr, false, false, nullptr},
        {"/api/logs", HTTP_GET, &handle_api_logs_get, nullptr, false, false, nullptr},
        {"/api/logs/clear", HTTP_POST, &handle_api_logs_clear, nullptr, false, false, nullptr},
        {"/api/debug", HTTP_GET, &handle_api_debug_get, nullptr, false, false, nullptr},
        {"/api/debug", HTTP_POST, &handle_api_debug_post, nullptr, false, false, nullptr},
        {"/api/errors", HTTP_GET, &handle_api_errors_get, nullptr, false, false, nullptr},
        {"/api/ota/firmware", HTTP_POST, &handle_ota_firmware, nullptr, false, false, nullptr},
        {"/api/ota/littlefs", HTTP_POST, &handle_ota_littlefs, nullptr, false, false, nullptr},
    };
    for (const auto& u : uris) {
        httpd_register_uri_handler(g_httpd, &u);
    }
    ESP_LOGI(log_tag, "HTTP & WebSocket server listening on :80");
}

std::uint8_t broadcast_ws_state() noexcept {
    if (!g_httpd) {
        return 0;
    }
    std::size_t fds_count = max_ws_clients;
    std::array<int, max_ws_clients> client_fds{};
    if (httpd_get_client_list(g_httpd, &fds_count, client_fds.data()) != ESP_OK || fds_count == 0) {
        return 0;
    }

    std::uint8_t ws_active = 0;
    for (std::size_t i = 0; i < fds_count; ++i) {
        if (httpd_ws_get_fd_info(g_httpd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            ++ws_active;
        }
    }
    if (ws_active == 0) {
        return 0;
    }

    const std::size_t len = runtime_state().format_live_ws_json(g_ws_scratch, sizeof(g_ws_scratch), now_ms());
    if (len == 0) {
        return ws_active;
    }

    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<std::uint8_t*>(g_ws_scratch);
    frame.len = len;
    for (std::size_t i = 0; i < fds_count; ++i) {
        if (httpd_ws_get_fd_info(g_httpd, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            (void)httpd_ws_send_frame_async(g_httpd, client_fds[i], &frame);
        }
    }
    return ws_active;
}

void wifi_event_handler(void*, esp_event_base_t base, std::int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            g_wifi_state = g_ap_portal_active ? device::WifiState::Setup : device::WifiState::Connecting;
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            g_wifi_backoff.on_disconnected(now_ms());
            if (!g_ap_portal_active) {
                g_wifi_state = device::WifiState::Connecting;
            }
            runtime_state().log().record(diagnostics::LogSource::Wifi, diagnostics::LogSeverity::Warning,
                                         diagnostics::ErrorCode::WifiDisconnected, diagnostics::no_address,
                                         "WiFi STA disconnected; auto-reconnect active", now_ms());
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* ev = static_cast<ip_event_got_ip_t*>(data);
        std::snprintf(g_ipv4, sizeof(g_ipv4), IPSTR, IP2STR(&ev->ip_info.ip));
        g_wifi_backoff.on_connected();
        g_wifi_state = device::WifiState::Connected;
        ESP_LOGI(log_tag, "WiFi STA connected, IP=%s", g_ipv4);
    }
}

void mqtt_event_handler(void*, esp_event_base_t, std::int32_t event_id, void* event_data) {
    auto* ev = static_cast<esp_mqtt_event_handle_t>(event_data);
    const auto cfg = runtime_state().network_config();
    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED: {
        g_mqtt_connected = true;
        g_ha_gateway_published = false;
        g_ha_published_channels.reset();
        g_mqtt_backoff.on_connected();
        char topic[64]{};
        std::snprintf(topic, sizeof(topic), "%s/status", cfg.mqtt_base_topic.data());
        esp_mqtt_client_publish(g_mqtt, topic, "online", 6, 1, 1);
        std::snprintf(topic, sizeof(topic), "%s/light/+/set", cfg.mqtt_base_topic.data());
        esp_mqtt_client_subscribe(g_mqtt, topic, 0);
        std::snprintf(topic, sizeof(topic), "%s/scene/set", cfg.mqtt_base_topic.data());
        esp_mqtt_client_subscribe(g_mqtt, topic, 0);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        g_mqtt_connected = false;
        g_mqtt_backoff.on_disconnected(now_ms());
        runtime_state().log().record(diagnostics::LogSource::Mqtt, diagnostics::LogSeverity::Warning,
                                     diagnostics::ErrorCode::MqttDisconnected, diagnostics::no_address,
                                     "MQTT broker disconnected", now_ms());
        break;
    case MQTT_EVENT_ERROR:
        runtime_state().log().record(diagnostics::LogSource::Mqtt, diagnostics::LogSeverity::Warning,
                                     diagnostics::ErrorCode::MqttConnectFailed, diagnostics::no_address,
                                     "MQTT transport error", now_ms());
        break;
    case MQTT_EVENT_DATA:
        if (ev && ev->topic && ev->data && ev->topic_len > 0 && ev->data_len > 0 && ev->topic_len < 96) {
            char topic[96]{};
            std::memcpy(topic, ev->topic, static_cast<std::size_t>(ev->topic_len));
            topic[ev->topic_len] = '\0';
            if (const auto cmd = network::parse_mqtt_command(cfg.mqtt_base_topic.data(), topic, ev->data,
                                                             static_cast<std::size_t>(ev->data_len))) {
                if (g_commands) {
                    (void)g_commands->try_push(*cmd);
                }
            }
        }
        break;
    default:
        break;
    }
}

void publish_ha_discovery_and_states() noexcept {
    if (!g_mqtt || !g_mqtt_connected) {
        return;
    }
    const auto cfg = runtime_state().network_config();
    char node_id[33]{};
    network::format_unique_node_id(cfg.hostname.data(), g_chip_id, node_id, sizeof(node_id));
    const network::HaDeviceMeta meta{cfg.mqtt_base_topic.data(), node_id, g_ipv4, kFirmwareVersion};
    char topic[128]{};
    char payload[640]{};

    if (cfg.ha_discovery_enabled) {
        // 1. Central Gateway Device entities (published once per MQTT connection)
        if (!g_ha_gateway_published) {
            if (network::format_ha_gateway_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, "sensor",
                                                           "total_power", topic, sizeof(topic)) &&
                network::format_ha_gateway_power_discovery_payload(meta, payload, sizeof(payload))) {
                esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
            }
            if (network::format_ha_gateway_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, "sensor",
                                                           "total_energy", topic, sizeof(topic)) &&
                network::format_ha_gateway_energy_discovery_payload(meta, payload, sizeof(payload))) {
                esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
            }
            if (network::format_ha_gateway_discovery_topic(cfg.ha_discovery_prefix.data(), node_id,
                                                           "binary_sensor", "fault", topic, sizeof(topic)) &&
                network::format_ha_gateway_fault_discovery_payload(meta, payload, sizeof(payload))) {
                esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
            }
            g_ha_gateway_published = true;
        }

        // 2. Only populated lamp addresses (0..63) are registered in HA as linked child devices.
        // If a lamp is newly discovered during a later scan, register it; if an address is no
        // longer populated, clear its retained discovery topics with an empty payload.
        for (std::uint8_t i = 0; i < 64; ++i) {
            const bool present = runtime_state().channel_present(i);
            const auto addr = *dali::ShortAddress::from(i);
            if (present && !g_ha_published_channels.test(i)) {
                const auto ch = runtime_state().channel(i);
                const auto cap = ch.has_capabilities
                                     ? std::optional<dali::ColourCapabilities>(ch.capabilities)
                                     : std::nullopt;
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "light", "") &&
                    network::format_ha_channel_discovery_payload(cfg.mqtt_base_topic.data(), node_id, addr,
                                                                 cap, payload, sizeof(payload), g_ipv4,
                                                                 kFirmwareVersion)) {
                    esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
                }
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "sensor", "_power") &&
                    network::format_ha_channel_power_discovery_payload(meta, addr, payload,
                                                                       sizeof(payload))) {
                    esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
                }
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "binary_sensor",
                                                               "_fault") &&
                    network::format_ha_channel_fault_discovery_payload(meta, addr, payload,
                                                                       sizeof(payload))) {
                    esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 1, 1);
                }
                g_ha_published_channels.set(i);
            } else if (!present && g_ha_published_channels.test(i)) {
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "light", "")) {
                    esp_mqtt_client_publish(g_mqtt, topic, "", 0, 1, 1);
                }
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "sensor", "_power")) {
                    esp_mqtt_client_publish(g_mqtt, topic, "", 0, 1, 1);
                }
                if (network::format_ha_channel_discovery_topic(cfg.ha_discovery_prefix.data(), node_id, addr,
                                                               topic, sizeof(topic), "binary_sensor",
                                                               "_fault")) {
                    esp_mqtt_client_publish(g_mqtt, topic, "", 0, 1, 1);
                }
                g_ha_published_channels.reset(i);
            }
        }
    }

    std::uint16_t total_power_deciwatts = 0;
    std::uint32_t total_energy_wh = 0;
    for (std::uint8_t i = 0; i < 64; ++i) {
        if (!runtime_state().channel_present(i)) {
            continue;
        }
        const auto ch = runtime_state().channel(i);
        total_power_deciwatts =
            static_cast<std::uint16_t>(total_power_deciwatts + ch.effective_power_deciwatts);
        total_energy_wh += ch.details.d4i_active_energy_wh;
        lighting::State st{dali::ArcLevel::from(ch.level), ch.source};
        std::snprintf(topic, sizeof(topic), "%s/light/ch%02u/state", cfg.mqtt_base_topic.data(),
                      static_cast<unsigned>(i));
        if (network::format_mqtt_channel_state(st, ch.colour, runtime_state().channel_faulty(i), payload,
                                               sizeof(payload), ch.effective_power_deciwatts,
                                               ch.details.d4i_active_energy_wh)) {
            esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 0, 1);
        }
    }

    const auto oled_st = runtime_state().oled_state();
    std::snprintf(topic, sizeof(topic), "%s/gateway/state", cfg.mqtt_base_topic.data());
    if (network::format_mqtt_gateway_state(
            total_power_deciwatts, total_energy_wh, oled_st.active_lamp_count, oled_st.faulty_gear_count,
            !oled_st.knx_bus_ok || oled_st.dali_psu_short_tripped, payload, sizeof(payload))) {
        esp_mqtt_client_publish(g_mqtt, topic, payload, 0, 0, 1);
    }
}

void apply_wifi_and_mqtt_config(bool force_ap) noexcept {
    const auto cfg = runtime_state().network_config();
    if (!cfg.wifi_enabled && !force_ap) {
        esp_wifi_stop();
        g_wifi_state = device::WifiState::Off;
        std::snprintf(g_ipv4, sizeof(g_ipv4), "0.0.0.0");
        return;
    }

    const bool has_sta = cfg.wifi_ssid[0] != '\0';
    g_ap_portal_active = force_ap || (!has_sta && cfg.ap_fallback_enabled);

    wifi_config_t ap_cfg{};
    char ap_ssid[33]{};
    network::format_unique_ap_ssid(g_chip_id, ap_ssid, sizeof(ap_ssid));
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(ap_ssid));
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    wifi_config_t sta_cfg{};
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.ssid), cfg.wifi_ssid.data(),
                 sizeof(sta_cfg.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.password), cfg.wifi_password.data(),
                 sizeof(sta_cfg.sta.password) - 1);

    esp_wifi_stop();
    if (g_ap_portal_active && has_sta) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        g_wifi_state = device::WifiState::Setup;
        std::snprintf(g_ipv4, sizeof(g_ipv4), "192.168.4.1");
    } else if (g_ap_portal_active) {
        esp_wifi_set_mode(WIFI_MODE_AP);
        esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
        g_wifi_state = device::WifiState::Setup;
        std::snprintf(g_ipv4, sizeof(g_ipv4), "192.168.4.1");
    } else {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        g_wifi_state = device::WifiState::Connecting;
    }
    esp_wifi_start();
    esp_wifi_set_ps(cfg.wifi_power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);

    if (g_mqtt) {
        esp_mqtt_client_stop(g_mqtt);
        esp_mqtt_client_destroy(g_mqtt);
        g_mqtt = nullptr;
        g_mqtt_connected = false;
    }
    if (cfg.mqtt_enabled && cfg.mqtt_host[0] != '\0') {
        char uri[96]{};
        std::snprintf(uri, sizeof(uri), "mqtt://%s:%u", cfg.mqtt_host.data(),
                      static_cast<unsigned>(cfg.mqtt_port));
        char lwt_topic[64]{};
        std::snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", cfg.mqtt_base_topic.data());
        char client_id[33]{};
        network::format_unique_node_id(cfg.hostname.data(), g_chip_id, client_id, sizeof(client_id));
        esp_mqtt_client_config_t mqtt_cfg{};
        mqtt_cfg.broker.address.uri = uri;
        mqtt_cfg.credentials.client_id = client_id;
        if (cfg.mqtt_username[0] != '\0') {
            mqtt_cfg.credentials.username = cfg.mqtt_username.data();
        }
        if (cfg.mqtt_password[0] != '\0') {
            mqtt_cfg.credentials.authentication.password = cfg.mqtt_password.data();
        }
        mqtt_cfg.session.last_will.topic = lwt_topic;
        mqtt_cfg.session.last_will.msg = "offline";
        mqtt_cfg.session.last_will.msg_len = 7;
        mqtt_cfg.session.last_will.qos = 1;
        mqtt_cfg.session.last_will.retain = 1;

        g_mqtt = esp_mqtt_client_init(&mqtt_cfg);
        if (g_mqtt) {
            esp_mqtt_client_register_event(g_mqtt, MQTT_EVENT_ANY, &mqtt_event_handler, nullptr);
            esp_mqtt_client_start(g_mqtt);
        }
    }
}

}  // namespace

NetworkServices::NetworkServices(Queue<control::Command>& commands, I2cOled& oled) noexcept
    : commands_(commands), oled_(oled) {
    g_commands = &commands_;
    g_oled = &oled_;
    g_http_mutex = xSemaphoreCreateMutexStatic(&g_http_mutex_buf);
}

void NetworkServices::start() noexcept {
    init_chip_id();
    esp_ota_mark_app_valid_cancel_rollback();

    (void)esp_netif_init();
    (void)esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wifi_init) == ESP_OK) {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr);
        apply_wifi_and_mqtt_config(false);
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    (void)esp_netif_sntp_init(&sntp_cfg);

    start_http_server();

    xTaskCreate(&NetworkServices::task_entry, "net_ui", 5120, this, 2, nullptr);
}

void NetworkServices::start_wifi_setup_portal() noexcept {
    apply_wifi_and_mqtt_config(true);
}

void NetworkServices::emergency_power_down() noexcept {
    gpio_set_level(board::dali_supply_off, 1);
    runtime_state().on_power_fail_saveb(now_ms());
}

void NetworkServices::task_entry(void* arg) {
    static_cast<NetworkServices*>(arg)->loop();
}

void NetworkServices::loop() noexcept {
    std::uint32_t last_mqtt_pub_ms = 0;
    for (;;) {
        const auto now = now_ms();

        if (g_reload_requested) {
            g_reload_requested = false;
            apply_wifi_and_mqtt_config(false);
        }

        const auto cfg = runtime_state().network_config();
        if (cfg.wifi_enabled && cfg.wifi_ssid[0] != '\0' && g_wifi_state != device::WifiState::Connected &&
            g_wifi_backoff.should_attempt(now)) {
            g_wifi_backoff.mark_attempt(now);
            esp_wifi_connect();
            if (g_wifi_backoff.attempts() >= 4 && cfg.ap_fallback_enabled && !g_ap_portal_active) {
                apply_wifi_and_mqtt_config(true);
            }
        }

        if (!runtime_state().log().clock_synced()) {
            struct timeval tv{};
            if (gettimeofday(&tv, nullptr) == 0 && tv.tv_sec > 1700000000L) {
                const auto epoch_ms = static_cast<std::uint64_t>(tv.tv_sec) * 1000ULL +
                                      static_cast<std::uint64_t>(tv.tv_usec / 1000);
                runtime_state().log().sync_wall_clock(epoch_ms, now);
            }
        }

        std::int8_t rssi = 0;
        if (g_wifi_state == device::WifiState::Connected) {
            wifi_ap_record_t ap_info{};
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                rssi = ap_info.rssi;
            }
        }

        const std::uint8_t ws_clients = broadcast_ws_state();
        runtime_state().update_network_state(g_wifi_state, rssi, g_ipv4, g_mqtt_connected, ws_clients, now,
                                             g_chip_id);

        runtime_state().render_oled(now);
        oled_.flush(runtime_state().oled_display(), runtime_state().oled_sleeping(now));

        if (g_mqtt_connected && (now - last_mqtt_pub_ms >= 2500U)) {
            last_mqtt_pub_ms = now;
            publish_ha_discovery_and_states();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

}
