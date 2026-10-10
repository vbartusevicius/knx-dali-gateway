#pragma once

#include <knx/interface_object.h>
#include <knx/property.h>
#include <knx_facade.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include "commissioning/Commissioning.hpp"
#include "config/Configuration.hpp"
#include "control/Command.hpp"
#include "knx/CommissioningProtocol.hpp"
#include "knx/LightingObjects.hpp"
#include "knx/ObjectMap.hpp"
#include "knx/generated/KnxDaliGateway.h"

namespace gateway::knxstack {

// Stack-facing application glue shared by the ESP32 firmware and the Linux
// integration-lab build. Runs against any thelsing KnxFacade instantiation
// (TP via Bau07B0, KNXnet/IP via Bau57B0): group-object writes are routed
// through ObjectMap into a command sink, StatusEvents are published back to
// the status/fault objects, function properties carry commissioning commands
// and state. All transport/queue/OS details stay outside via Hooks.
// Single instance only: the BAU's function-property entry points are plain
// C function pointers.
template <typename Facade>
class KnxApplication final {
public:
    struct Hooks {
        // Enqueue a decoded command for the worker; false when the queue is full.
        bool (*push_command)(void*, const control::Command&) = nullptr;
        // Next status event to publish, nullopt when none pending.
        std::optional<control::StatusEvent> (*pop_status)(void*) = nullptr;
        // Deliver the parsed ETS parameter block; called once when configured.
        bool (*push_configuration)(void*, const config::Configuration&) = nullptr;
        // Newest commissioning snapshot (implementations drain their queue here).
        commissioning::Snapshot (*latest_snapshot)(void*) = nullptr;
        // Newest input-device snapshot for function-property state 2.
        commissioning::InputSnapshot (*latest_input_snapshot)(void*) = nullptr;
        // Optional warning/info sink.
        void (*log)(void*, const char*) = nullptr;
        void* context = nullptr;
    };

    KnxApplication(Facade& knx, Hooks hooks) noexcept : knx_(knx), hooks_(hooks) {
        active_ = this;
    }

    void start() {
        knx_.bau().functionPropertyCallback(&on_function_property);
        knx_.bau().functionPropertyStateCallback(&on_function_property_state);
        knx_.readMemory();
        // A device that was never programmed has no usable individual
        // address (0 or the erased-flash 0xFFFF): enter programming mode so
        // it can receive one.
        if (knx_.individualAddress() == 0 || knx_.individualAddress() == 0xFFFF) {
            knx_.progMode(true);
        }
        if (hooks_.log) {
            char message[96];
            std::snprintf(message, sizeof(message),
                          "IA=%04x progMode=%u configured=%u",
                          knx_.individualAddress(),
                          static_cast<unsigned>(knx_.progMode()),
                          static_cast<unsigned>(knx_.configured()));
            hooks_.log(hooks_.context, message);
        }
        knx_.start();
    }

    // One iteration: pump the stack, attach object callbacks and publish
    // statuses once the device is configured.
    void loop() {
        knx_.loop();
        if (knx_.configured()) {
            attach_objects();
            publish_statuses();
        }
    }

    Facade& facade() { return knx_; }

    bool function_property(std::uint8_t object_index, std::uint8_t property_id,
                           std::uint8_t length, const std::uint8_t* data,
                           std::uint8_t* result_data, std::uint8_t& result_length) {
        if (object_index != knx::function_property_object_index) {
            return false;
        }
        const auto decoded = knx::decode_command(object_index, property_id, data, length);
        auto response = knx::RequestResponse::Accepted;
        if (!decoded.command) {
            response = decoded.response;
        } else if (commissioning::active(snapshot().progress.phase)) {
            response = knx::RequestResponse::Busy;
        } else if (!push_command(*decoded.command)) {
            logf("command queue full; commissioning command on property %u rejected",
                 static_cast<unsigned>(property_id));
            response = knx::RequestResponse::Busy;
        }
        result_data[0] = static_cast<std::uint8_t>(response);
        result_length = 1;
        return true;
    }

    bool function_property_state(std::uint8_t object_index, std::uint8_t property_id,
                                 std::uint8_t /*length*/, const std::uint8_t* /*data*/,
                                 std::uint8_t* result_data, std::uint8_t& result_length) {
        if (object_index != knx::function_property_object_index) {
            return false;
        }
        if (property_id == knx::state_property) {
            result_length =
                static_cast<std::uint8_t>(knx::encode_state(snapshot(), result_data));
            return true;
        }
        if (property_id == knx::input_state_property) {
            result_length = static_cast<std::uint8_t>(knx::encode_input_state(
                hooks_.latest_input_snapshot ? hooks_.latest_input_snapshot(hooks_.context)
                                             : commissioning::InputSnapshot{},
                result_data));
            return true;
        }
        return false;
    }

private:
    static bool on_function_property(std::uint8_t object_index, std::uint8_t property_id,
                                     std::uint8_t length, std::uint8_t* data,
                                     std::uint8_t* result_data, std::uint8_t& result_length) {
        return active_ && active_->function_property(object_index, property_id, length, data,
                                                     result_data, result_length);
    }

    static bool on_function_property_state(std::uint8_t object_index, std::uint8_t property_id,
                                           std::uint8_t length, std::uint8_t* data,
                                           std::uint8_t* result_data, std::uint8_t& result_length) {
        return active_ && active_->function_property_state(object_index, property_id, length, data,
                                                           result_data, result_length);
    }

    commissioning::Snapshot snapshot() {
        return hooks_.latest_snapshot ? hooks_.latest_snapshot(hooks_.context)
                                      : commissioning::Snapshot{};
    }

    bool push_command(const control::Command& command) {
        return hooks_.push_command && hooks_.push_command(hooks_.context, command);
    }

    void logf(const char* format, unsigned value) {
        if (hooks_.log) {
            char message[96];
            std::snprintf(message, sizeof(message), format, value);
            hooks_.log(hooks_.context, message);
        }
    }

    void attach_objects() {
        if (objects_attached_) {
            return;
        }
        objects_attached_ = true;
        const auto count = knx_.bau().groupObjectTable().entryCount();
        if (count < knx::object_count) {
            logf("group object table has %u entries; objects above it are ignored",
                 static_cast<unsigned>(count));
        }
        const auto last = count < knx::object_count ? count : knx::object_count;
        for (std::uint16_t number = 1; number <= last; ++number) {
            knx_.getGroupObject(number).callback(
                [this](GroupObject& object) { on_group_object(object); });
        }
        const auto* parameters = knx_.paramData(0);
        if (!hooks_.push_configuration ||
            !hooks_.push_configuration(
                hooks_.context,
                config::parse_configuration(parameters, MAIN_ParameterSize))) {
            if (hooks_.log) {
                hooks_.log(hooks_.context,
                           "configuration handoff failed; worker keeps the defaults");
            }
        }
        refresh_associations();
    }

    void on_group_object(GroupObject& object) {
        const std::uint16_t number = object.asap();
        const std::uint8_t* data = object.valueRef();
        const std::size_t length = object.valueSize();
        const std::uint8_t raw = data[0];
        const auto target = knx::object_target(number);
        if (!target) {
            // Input-channel objects bind a control-device instance, not a
            // gear address: only the switch object takes writes, feeding
            // the input channel's toggle state back to the worker.
            const auto input = knx::input_object_target(number);
            if (input && input->function == knx::InputObjectFunction::Switch &&
                length >= 1) {
                const auto channel = inputs::InputChannel::from(input->channel);
                if (channel && !push_command(
                                   control::InputFeedback{*channel, (raw & 1) != 0})) {
                    logf("command queue full; dropped input feedback on object %u", number);
                }
            }
            return;
        }
        std::optional<control::Command> command;
        switch (target->function) {
        case knx::ObjectFunction::Switch:
            command = knx::command_for_switch(number, (raw & 1) != 0);
            break;
        case knx::ObjectFunction::Brightness:
            command = knx::command_for_scaling(number, raw);
            break;
        case knx::ObjectFunction::RelativeDimming:
            command = knx::command_for_relative_dimming(number, raw);
            break;
        case knx::ObjectFunction::Scene:
            command = knx::command_for_scene(number, raw);
            break;
        case knx::ObjectFunction::ColourTemperature:
            command = knx::command_for_colour_temperature(number, data, length);
            break;
        case knx::ObjectFunction::RelativeColourTemperature:
            command = knx::command_for_relative_colour_temperature(number, raw);
            break;
        case knx::ObjectFunction::Rgb:
            command = knx::command_for_rgb(number, data, length);
            break;
        case knx::ObjectFunction::Rgbw:
            command = knx::command_for_rgbw(number, data, length);
            break;
        case knx::ObjectFunction::XyY:
            command = knx::command_for_xyy(number, data, length);
            break;
        default:
            break;
        }
        if (command && !push_command(*command)) {
            logf("command queue full; dropped write on object %u", number);
        }
    }

    // The association table is reachable only through interface-object
    // properties, so resolve its memory via PID_TABLE_REFERENCE and keep the
    // associated ASAPs in a set. An object without an entry has no destination
    // GA; writing it would emit a telegram to 0/0/0.
    void refresh_associations() {
        associated_asaps_.clear();
        std::uint8_t elements = 1;
        std::uint8_t* data = nullptr;
        std::uint32_t length = 0;
        knx_.bau().propertyValueRead(OT_ASSOC_TABLE, 0, PID_TABLE_REFERENCE,
                                     elements, 1, &data, length);
        if (data == nullptr || length < 4) {
            delete[] data;
            return;
        }
        const std::uint32_t relative =
            (static_cast<std::uint32_t>(data[0]) << 24) |
            (static_cast<std::uint32_t>(data[1]) << 16) |
            (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
        delete[] data;
        const auto* table = knx_.bau().memory().toAbsolute(relative);
        if (table == nullptr) {
            return;
        }
        const std::uint16_t count = (static_cast<std::uint16_t>(table[0]) << 8) | table[1];
        if (count > knx_.bau().groupObjectTable().entryCount() * 4) {
            return;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            const auto* entry = table + 2 + i * 4;
            associated_asaps_.push_back(
                (static_cast<std::uint16_t>(entry[2]) << 8) | entry[3]);
        }
        std::sort(associated_asaps_.begin(), associated_asaps_.end());
    }

    bool associated(std::uint16_t object) const {
        return std::binary_search(associated_asaps_.begin(), associated_asaps_.end(),
                                  object);
    }

    void publish_statuses() {
        const auto table_count = knx_.bau().groupObjectTable().entryCount();
        if (!hooks_.pop_status) {
            return;
        }
        while (const auto event = hooks_.pop_status(hooks_.context)) {
            if (const auto* update = std::get_if<control::StatusUpdate>(&*event)) {
                const auto status = knx::status_objects(*update);
                if (!status) {
                    continue;
                }
                if (status->switch_object <= table_count && associated(status->switch_object)) {
                    auto& switch_object = knx_.getGroupObject(status->switch_object);
                    switch_object.valueRef()[0] = status->on ? 1 : 0;
                    switch_object.objectWritten();
                }
                if (status->brightness_object <= table_count &&
                    associated(status->brightness_object)) {
                    auto& brightness_object = knx_.getGroupObject(status->brightness_object);
                    brightness_object.valueRef()[0] = status->scaling;
                    brightness_object.objectWritten();
                }
            } else if (const auto* colour = std::get_if<control::ColourUpdate>(&*event)) {
                const auto status = knx::colour_status_object(*colour);
                if (!status || status->object > table_count || !associated(status->object)) {
                    continue;
                }
                auto& status_object = knx_.getGroupObject(status->object);
                if (status->length > status_object.valueSize()) {
                    continue;
                }
                std::copy_n(status->data.begin(), status->length, status_object.valueRef());
                status_object.objectWritten();
            } else if (const auto* action = std::get_if<control::InputAction>(&*event)) {
                if (action->object > table_count || !associated(action->object)) {
                    continue;
                }
                auto& input_object = knx_.getGroupObject(action->object);
                if (action->length > input_object.valueSize()) {
                    continue;
                }
                std::copy_n(action->data.begin(), action->length, input_object.valueRef());
                input_object.objectWritten();
            } else {
                const auto& fault = std::get<control::FaultUpdate>(*event);
                const auto number =
                    knx::object_number(dali::Address::short_address(fault.address),
                                       knx::ObjectFunction::Fault);
                if (number != 0 && number <= table_count && associated(number)) {
                    auto& fault_object = knx_.getGroupObject(number);
                    fault_object.valueRef()[0] = fault.faulty ? 1 : 0;
                    fault_object.objectWritten();
                }
            }
        }
    }

    static KnxApplication* active_;

    Facade& knx_;
    Hooks hooks_;
    std::vector<std::uint16_t> associated_asaps_;
    bool objects_attached_ = false;
};

template <typename Facade>
KnxApplication<Facade>* KnxApplication<Facade>::active_ = nullptr;

}
