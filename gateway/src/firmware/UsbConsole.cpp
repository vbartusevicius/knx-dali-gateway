#include "firmware/UsbConsole.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>

#include "firmware/Application.hpp"

namespace gateway::firmware {
namespace {

std::optional<unsigned> unsigned_argument(const char* text) {
    unsigned value = 0;
    const auto end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

// Targets: a<0..63> a single gear, g<0..15> a DALI group, b the whole bus.
std::optional<dali::Address> target_argument(const char* text) {
    if (std::strcmp(text, "b") == 0) {
        return dali::Address::broadcast();
    }
    const char kind = text[0];
    if (kind != 'a' && kind != 'g') {
        return std::nullopt;
    }
    const auto value = unsigned_argument(text + 1);
    if (!value) {
        return std::nullopt;
    }
    if (kind == 'a') {
        const auto address = dali::ShortAddress::from(*value);
        return address ? std::optional{dali::Address::short_address(*address)} : std::nullopt;
    }
    const auto group = dali::Group::from(*value);
    return group ? std::optional{dali::Address::group(*group)} : std::nullopt;
}

std::optional<dali::ShortAddress> address_argument(const char* text) {
    const auto target = target_argument(text);
    return target && target->as_short() ? target->as_short() : std::nullopt;
}

}

UsbConsole::UsbConsole(Queue<control::Command>& commands, Queue<commissioning::Snapshot>& snapshots,
                       Queue<commissioning::InputSnapshot>& input_snapshots,
                       bool dali_configured, bool storage_available) noexcept
    : commands_(commands), snapshots_(snapshots), input_snapshots_(input_snapshots),
      dali_configured_(dali_configured), storage_available_(storage_available) {}

UsbConsole::~UsbConsole() {
    if (repl_) {
        repl_->del(repl_);
    }
}

esp_err_t UsbConsole::register_command(const char* name, const char* hint, esp_console_cmd_func_with_context_t handler) {
    esp_console_cmd_t command{};
    command.command = name;
    command.help = hint;
    command.hint = hint;
    command.func_w_context = handler;
    command.context = this;
    return esp_console_cmd_register(&command);
}

esp_err_t UsbConsole::start() {
    esp_console_dev_usb_serial_jtag_config_t hardware = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "gateway> ";
    config.max_cmdline_length = 128;
    config.max_history_len = 8;
    auto error = esp_console_new_repl_usb_serial_jtag(&hardware, &config, &repl_);
    if (error != ESP_OK) {
        return error;
    }
    error = esp_console_register_help_command();
    if (error != ESP_OK) {
        return error;
    }
    const struct {
        const char* name;
        const char* hint;
        esp_console_cmd_func_with_context_t handler;
    } commands[] = {
        {"status", "", status_command},
        {"level", "<a0..63|g0..15|b> <arc-level 0..254>", level_command},
        {"on", "<a0..63|g0..15|b>", on_command},
        {"off", "<a0..63|g0..15|b>", off_command},
        {"read", "<a0..63>", read_command},
        {"recover", "<a0..63>", recover_command},
        {"dim", "<a0..63|g0..15|b> up|down|stop", dim_command},
        {"scene", "<0..15>", scene_command},
        {"store", "<0..15>", store_command},
        {"scan", "", scan_command},
        {"assign", "new|all", assign_command},
        {"replace", "<a0..63>", replace_command},
        {"identify", "<a0..63>", identify_command},
        {"faults", "", faults_command},
        {"reset-addresses", "confirm", reset_addresses_command},
        {"inputs", "scan|assign new|assign all|list", inputs_command},
        {"factory-reset", "confirm", factory_reset_command}
    };
    for (const auto& command : commands) {
        error = register_command(command.name, command.hint, command.handler);
        if (error != ESP_OK) {
            return error;
        }
    }
    return esp_console_start_repl(repl_);
}

int UsbConsole::submit(const control::Command& command) {
    if (commands_.try_push(command)) {
        std::printf("queued\n");
        return 0;
    }
    std::printf("queue full\n");
    return 1;
}

int UsbConsole::status_command(void* context, int argc, char**) {
    if (argc != 1) {
        return 1;
    }
    const auto& console = *static_cast<UsbConsole*>(context);
    std::printf("dali-driver=%s target-storage=%s\n",
                console.dali_configured_ ? "configured" : "disabled/unconfigured",
                console.storage_available_ ? "available" : "unavailable");
    return 0;
}

int UsbConsole::level_command(void* context, int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }
    const auto address = target_argument(argv[1]);
    const auto value = unsigned_argument(argv[2]);
    const auto level = value ? dali::ArcLevel::from(*value) : std::nullopt;
    if (!address || !level) {
        std::printf("Expected a target a0..63, g0..15 or b and arc level 0..254; 255 is MASK, not a target.\n");
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::SetLevel{*address, *level});
}

int UsbConsole::on_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = target_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::SwitchOn{*address});
}

int UsbConsole::off_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = target_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::SwitchOff{*address});
}

int UsbConsole::read_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = address_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::Refresh{*address});
}

int UsbConsole::recover_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = address_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::Recover{*address});
}

int UsbConsole::dim_command(void* context, int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }
    const auto address = target_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    if (std::strcmp(argv[2], "up") == 0) {
        return console.submit(control::DimStart{*address, lighting::DimDirection::Up});
    }
    if (std::strcmp(argv[2], "down") == 0) {
        return console.submit(control::DimStart{*address, lighting::DimDirection::Down});
    }
    if (std::strcmp(argv[2], "stop") == 0) {
        return console.submit(control::DimStop{*address});
    }
    std::printf("Expected up, down or stop.\n");
    return 1;
}

int UsbConsole::scene_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto number = unsigned_argument(argv[1]);
    const auto scene = number ? dali::Scene::from(*number) : std::nullopt;
    if (!scene) {
        std::printf("Expected a scene number 0..15.\n");
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::RecallScene{*scene});
}

int UsbConsole::store_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto number = unsigned_argument(argv[1]);
    const auto scene = number ? dali::Scene::from(*number) : std::nullopt;
    if (!scene) {
        std::printf("Expected a scene number 0..15.\n");
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::StoreScene{*scene});
}

int UsbConsole::scan_command(void* context, int argc, char**) {
    if (argc != 1) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::ScanBus{});
}

int UsbConsole::assign_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    if (std::strcmp(argv[1], "new") == 0) {
        return console.submit(control::AssignAddresses{dali::CommissionMode::Unaddressed});
    }
    if (std::strcmp(argv[1], "all") == 0) {
        return console.submit(control::AssignAddresses{dali::CommissionMode::All});
    }
    std::printf("Expected new or all.\n");
    return 1;
}

int UsbConsole::replace_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = address_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::ReplaceDevice{*address});
}

int UsbConsole::identify_command(void* context, int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const auto address = address_argument(argv[1]);
    if (!address) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::IdentifyDevice{*address});
}

int UsbConsole::faults_command(void* context, int argc, char**) {
    if (argc != 1) {
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    std::optional<commissioning::Snapshot> snapshot;
    while (const auto next = console.snapshots_.pop(0)) {
        snapshot = *next;
    }
    if (!snapshot) {
        std::printf("no commissioning snapshot yet\n");
        return 0;
    }
    const auto print_bitmap = [](const char* name, const std::array<std::uint8_t, 8>& bitmap) {
        std::printf("%s=", name);
        for (const auto byte : bitmap) {
            std::printf("%02x", byte);
        }
    };
    print_bitmap("present", snapshot->present);
    std::printf(" ");
    print_bitmap("faulty", snapshot->faulty);
    std::printf(" phase=%u progress=%u%% found=%u error=%u\n",
                static_cast<unsigned>(snapshot->progress.phase),
                static_cast<unsigned>(snapshot->progress.progress),
                static_cast<unsigned>(snapshot->progress.found),
                static_cast<unsigned>(snapshot->progress.last_error));
    return 0;
}

int UsbConsole::reset_addresses_command(void* context, int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "confirm") != 0) {
        std::printf("This wipes every DALI short address on the bus; type 'reset-addresses confirm'.\n");
        return 1;
    }
    auto& console = *static_cast<UsbConsole*>(context);
    return console.submit(control::ResetAddresses{});
}

int UsbConsole::inputs_command(void* context, int argc, char** argv) {
    auto& console = *static_cast<UsbConsole*>(context);
    if (argc == 2 && std::strcmp(argv[1], "scan") == 0) {
        return console.submit(control::ScanInputDevices{});
    }
    if (argc == 3 && std::strcmp(argv[1], "assign") == 0) {
        if (std::strcmp(argv[2], "new") == 0) {
            return console.submit(
                control::AssignInputDevices{dali::CommissionMode::Unaddressed});
        }
        if (std::strcmp(argv[2], "all") == 0) {
            return console.submit(control::AssignInputDevices{dali::CommissionMode::All});
        }
    }
    if (argc == 2 && std::strcmp(argv[1], "list") == 0) {
        std::optional<commissioning::InputSnapshot> snapshot;
        while (const auto next = console.input_snapshots_.pop(0)) {
            snapshot = *next;
        }
        if (!snapshot) {
            std::printf("no input-device snapshot yet\n");
            return 0;
        }
        bool found = false;
        for (unsigned index = 0; index < snapshot->summary.size(); ++index) {
            if (!(snapshot->present[index / 8] & (1u << (index % 8)))) {
                continue;
            }
            found = true;
            const auto summary = snapshot->summary[index];
            std::printf("device %2u: %u instance(s)%s%s%s\n", index,
                        static_cast<unsigned>(summary & 0x1F),
                        (summary & 0x20) ? " push-button" : "",
                        (summary & 0x40) ? " occupancy" : "",
                        (summary & 0x80) ? " light" : "");
        }
        if (!found) {
            std::printf("no input devices found\n");
        }
        return 0;
    }
    std::printf("Expected 'inputs scan', 'inputs assign new|all' or 'inputs list'.\n");
    return 1;
}

int UsbConsole::factory_reset_command(void*, int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "confirm") != 0) {
        std::printf("This erases the KNX and lighting configuration; type 'factory-reset confirm'.\n");
        return 1;
    }
    factory_reset();
}

}
