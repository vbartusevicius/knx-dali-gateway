#pragma once

#include <cstddef>
#include <cstdint>

namespace gateway::diagnostics {

enum class ErrorCode : std::uint16_t {
    None = 0,
    DaliBusShort = 101,
    DaliNoPower = 102,
    DaliFrameCollision = 103,
    DaliReplyTimeout = 104,
    DaliInvalidReply = 105,
    DaliLampFailure = 106,
    DaliGearFailure = 107,
    DaliLimitError = 108,
    DaliPowerCycleSeen = 109,
    DaliUnsupportedColour = 110,
    DaliAddressingFailed = 111,
    KnxBusPowerLoss = 201,
    KnxBusPowerRestored = 202,
    KnxTpUartError = 203,
    KnxCommandQueueFull = 204,
    KnxStatusQueueFull = 205,
    KnxUnconfigured = 206,
    NvsOpenFailed = 301,
    NvsCommitFailed = 302,
    LittleFsMountFailed = 303,
    WatchdogResetRecovered = 304,
    BrownoutResetRecovered = 305,
    PanicResetRecovered = 306,
    WifiConnectFailed = 401,
    WifiDisconnected = 402,
    MqttConnectFailed = 403,
    MqttDisconnected = 404,
    OtaValidationFailed = 405,
    OtaFlashFailed = 406
};

struct ErrorDescriptor {
    ErrorCode code;
    const char* symbol;
    const char* title;
    const char* description;
    const char* remedy;
};

[[nodiscard]] const ErrorDescriptor& describe_error(ErrorCode code) noexcept;
[[nodiscard]] const ErrorDescriptor* error_catalog(std::size_t& count) noexcept;

}
