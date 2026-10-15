#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>

#include "commissioning/Commissioning.hpp"
#include "device/OledDisplay.hpp"
#include "device/StatusLeds.hpp"
#include "diagnostics/DaliDebugTrace.hpp"
#include "diagnostics/DiagnosticLog.hpp"
#include "lighting/LightingService.hpp"
#include "network/NetworkConfig.hpp"

namespace gateway::firmware {

struct ChannelSnapshot {
    std::uint8_t level = 0;
    lighting::StateSource source = lighting::StateSource::Unknown;
    lighting::ColourState colour{};
    dali::ColourCapabilities capabilities{};
    bool has_capabilities = false;
    lighting::GearDetails details{};
    std::uint16_t effective_power_deciwatts = 0;
};

class NvsLogBackend final : public diagnostics::LogBackend {
public:
    bool open() noexcept;
    bool load(void* data, std::size_t size) noexcept override;
    bool save(const void* data, std::size_t size) noexcept override;

private:
    nvs_handle_t handle_ = 0;
};

struct InputEventSnapshot {
    std::uint8_t device = 0xFF;
    std::uint8_t instance = 0;
    std::uint8_t instance_type = 0;
    std::uint16_t event_info = 0;
    std::uint32_t uptime_ms = 0;
};

class RuntimeState final {
public:
    RuntimeState() noexcept;

    void init_storage() noexcept;
    void save_network_config(const network::NetworkConfig& cfg) noexcept;
    [[nodiscard]] network::NetworkConfig network_config() const noexcept;

    void sync_from_worker(const lighting::LightingService& lighting, const commissioning::Commissioning& comm,
                          std::uint32_t now_ms) noexcept;

    void update_knx_state(std::uint16_t ia, bool configured, bool prog_mode, bool bus_ok,
                          device::PanelHint hint, std::uint32_t now_ms) noexcept;

    void update_network_state(device::WifiState wifi, std::int8_t rssi, const char* ipv4, bool mqtt_connected,
                              std::uint8_t ws_clients, std::uint32_t now_ms,
                              const char* chip_id = nullptr) noexcept;

    void on_power_fail_saveb(std::uint32_t now_ms) noexcept;
    void request_psu_reset() noexcept;
    [[nodiscard]] bool consume_psu_reset_request() noexcept;
    void record_input_event(std::uint8_t device, std::uint8_t instance, std::uint8_t instance_type,
                            std::uint16_t event_info, std::uint32_t now_ms) noexcept;
    void next_oled_page(std::uint32_t now_ms) noexcept;
    void toggle_oled_geometry(std::uint32_t now_ms) noexcept;

    [[nodiscard]] device::OledSystemState oled_state() const noexcept;
    void render_oled(std::uint32_t now_ms) noexcept;
    [[nodiscard]] const device::OledDisplay& oled_display() const noexcept { return oled_; }
    [[nodiscard]] bool oled_sleeping(std::uint32_t now_ms) const noexcept { return oled_.sleeping(now_ms); }

    diagnostics::DiagnosticLog& log() noexcept { return log_; }
    diagnostics::DaliDebugTrace& trace() noexcept { return trace_; }

    std::size_t format_live_ws_json(char* out, std::size_t out_size, std::uint32_t now_ms) const noexcept;
    std::size_t format_channel_detail_json(std::uint8_t ch, char* out, std::size_t out_size) const noexcept;
    std::size_t format_logs_json(char* out, std::size_t out_size) const noexcept;
    std::size_t format_trace_json(char* out, std::size_t out_size, std::size_t max_items = 96) const noexcept;

    [[nodiscard]] ChannelSnapshot channel(std::uint8_t index) const noexcept;
    [[nodiscard]] bool channel_present(std::uint8_t index) const noexcept;
    [[nodiscard]] bool channel_faulty(std::uint8_t index) const noexcept;
    [[nodiscard]] std::uint8_t group_level(std::uint8_t index) const noexcept;

private:
    void lock() const noexcept;
    void unlock() const noexcept;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable StaticSemaphore_t mutex_buf_{};
    NvsLogBackend log_backend_{};
    diagnostics::DiagnosticLog log_;
    diagnostics::DaliDebugTrace trace_;
    network::NetworkConfig net_cfg_{};
    device::OledDisplay oled_{device::OledGeometry::Oled64x128};
    device::OledSystemState oled_state_{};
    std::array<ChannelSnapshot, 64> channels_{};
    std::array<std::uint8_t, 16> group_levels_{};
    commissioning::InputSnapshot inputs_{};
    std::array<std::array<std::uint8_t, 8>, 64> input_instance_types_{};
    std::array<InputEventSnapshot, 8> recent_input_events_{};
    device::LedColours led_colours_{};
    std::uint8_t input_device_count_ = 0;
    std::uint8_t ws_clients_ = 0;
    bool knx_configured_ = false;
    bool power_loss_latched_ = false;
    bool psu_reset_requested_ = false;
};

RuntimeState& runtime_state() noexcept;

}
