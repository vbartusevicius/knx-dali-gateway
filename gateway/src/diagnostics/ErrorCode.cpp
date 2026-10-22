#include "diagnostics/ErrorCode.hpp"

#include <array>
#include <cstddef>

namespace gateway::diagnostics {
namespace {

constexpr std::array<ErrorDescriptor, 24> catalog{{
    {ErrorCode::None, "OK", "No Error", "Normal operation with no fault condition.", "None required."},
    {ErrorCode::DaliBusShort, "DALI_BUS_SHORT", "DALI Bus Short Circuit",
     "The DALI bus line (DA+/DA-) was held below the logic low threshold continuously for >50 ms.",
     "Inspect DA+/DA- wiring for a short circuit, reversed external DC supply, or damaged ballast."},
    {ErrorCode::DaliNoPower, "DALI_NO_POWER", "DALI Bus Power Absent",
     "No idle high voltage (12..20 V) detected on the DALI line.",
     "Verify JP1 jumper caps, 24 V AUX supply into the REC3-2415SRW DC-DC, or external DALI bus PSU."},
    {ErrorCode::DaliFrameCollision, "DALI_COLLISION", "DALI Bus Frame Collision",
     "A forward or backward frame collision occurred on the DALI line during transmission.",
     "Check if two ballasts share the same short address or if a DALI-2 input device transmitted at once."},
    {ErrorCode::DaliReplyTimeout, "DALI_REPLY_TIMEOUT", "DALI Ballast Reply Timeout",
     "An addressed control gear or input device did not answer within the backward-frame window.",
     "Check whether the ballast has mains power, is connected to DA+/DA-, or was replaced."},
    {ErrorCode::DaliInvalidReply, "DALI_INVALID_REPLY", "DALI Manchester Framing Error",
     "A backward frame had invalid Manchester bi-phase encoding or stop bits.",
     "Check DALI cable length (<300 m), bus capacitance, EMI near mains contactors, or duplicate addresses."},
    {ErrorCode::DaliLampFailure, "DALI_LAMP_FAILURE", "DALI Lamp / Load Failure",
     "Control gear QUERY STATUS bit 1 reports an open or shorted LED module/lamp.",
     "Inspect the LED load connected to the indicated ballast short address and replace if defective."},
    {ErrorCode::DaliGearFailure, "DALI_GEAR_FAILURE", "DALI Ballast Hardware Fault",
     "Control gear QUERY STATUS bit 0 reports an internal driver fault.",
     "Power-cycle the ballast mains supply; if fault persists, use Replace Ballast in ETS or Web UI."},
    {ErrorCode::DaliLimitError, "DALI_LIMIT_ERROR", "DALI Arc Level Limit Clamped",
     "Control gear QUERY STATUS bit 3 indicates a requested arc power level exceeded MIN/MAX bounds.",
     "Review the channel MIN LEVEL and MAX LEVEL settings in the Ballast Configuration panel."},
    {ErrorCode::DaliPowerCycleSeen, "DALI_POWER_CYCLE", "DALI Ballast Mains Power Cycle",
     "Control gear QUERY STATUS bit 7 indicates the driver lost and regained mains supply.",
     "Informational: gateway automatically reconciles or reads back the ballast level."},
    {ErrorCode::DaliUnsupportedColour, "DALI_UNSUPPORTED_COLOUR", "DT8 Colour Mode Unsupported",
     "A colour command (Tc, RGB, RGBW, or xy) targeted a gear whose DT8 features lack that mode.",
     "Check the ballast DT8 capability flags in Live Monitoring or adjust the ETS channel colour mode."},
    {ErrorCode::DaliAddressingFailed, "DALI_ADDRESSING_FAILED", "DALI Commissioning / Addressing Failed",
     "Binary search or short-address verification failed during DALI commissioning or replacement.",
     "Ensure all ballasts are powered, no bus collisions exist, and retry commissioning."},
    {ErrorCode::KnxBusPowerLoss, "KNX_BUS_POWER_LOSS", "KNX Bus Voltage Dropping (SAVEB)",
     "NCN5130 SAVEB asserted (VFILT < 14 V); emergency state flush executed.",
     "Check KNX 30 V bus choke supply and red/black terminal connection."},
    {ErrorCode::KnxBusPowerRestored, "KNX_BUS_POWER_OK", "KNX Bus Voltage Restored",
     "NCN5130 SAVEB released; KNX bus voltage returned above threshold.", "None required."},
    {ErrorCode::KnxTpUartError, "KNX_TPUART_ERROR", "KNX TP-UART Transceiver Fault",
     "UART framing or state discrepancy between ESP32-C6 and NCN5130 TP-UART.",
     "Check 16 MHz crystal Y1, NCN5130 power rails, and KNX bus polarity."},
    {ErrorCode::KnxCommandQueueFull, "KNX_CMD_QUEUE_FULL", "KNX Command Queue Saturated",
     "Inbound KNX telegram burst exceeded the DALI worker command queue capacity.",
     "Reduce cyclic KNX telegram rate or use group/broadcast addresses instead of 64 individual writes."},
    {ErrorCode::KnxStatusQueueFull, "KNX_STATUS_QUEUE_FULL", "KNX Status Queue Saturated",
     "Outbound DALI status updates exceeded the KNX transmit queue capacity.",
     "Verify KNX TP bus is connected and acknowledging frames."},
    {ErrorCode::KnxUnconfigured, "KNX_UNCONFIGURED", "KNX Application Not Programmed",
     "Gateway has no ETS application loaded or retains default address 15.15.255.",
     "Hold SW1 (PRG/RST) for 5 s to enter programming mode and download from ETS."},
    {ErrorCode::NvsOpenFailed, "NVS_OPEN_FAILED", "NVS Storage Partition Error",
     "Non-volatile storage partition could not be opened at startup.",
     "Check partition table or run a Factory Reset if flash sector corruption occurred."},
    {ErrorCode::NvsCommitFailed, "NVS_COMMIT_FAILED", "NVS Storage Write Failed",
     "Persisting target level or diagnostic log to NVS flash returned an error.",
     "Check free NVS entries and supply stability."},
    {ErrorCode::LittleFsMountFailed, "LITTLEFS_MOUNT_FAILED", "LittleFS Web Partition Unmounted",
     "The LittleFS storage partition had no valid filesystem image; serving built-in fallback Web UI.",
     "Upload littlefs.bin in the Firmware Update panel to install custom LittleFS assets."},
    {ErrorCode::WatchdogResetRecovered, "WDT_RESET_RECOVERED", "Recovered from Watchdog Reset",
     "Previous boot was terminated by the hardware task or interrupt watchdog.",
     "Inspect prior log entries for hung bus transactions or power brownouts."},
    {ErrorCode::BrownoutResetRecovered, "BROWNOUT_RECOVERED", "Recovered from Supply Brownout",
     "Previous boot reset due to 3.3 V rail dropping below the brownout detector threshold.",
     "Check C4 (470 uF) storage capacitor, KNX bus voltage (>= 21 V), and WiFi TX burst current."},
    {ErrorCode::PanicResetRecovered, "PANIC_RECOVERED", "Recovered from Firmware Panic",
     "Previous boot terminated with an unhandled CPU exception or abort.",
     "Export diagnostic logs and check firmware version."},
}};

constexpr std::array<ErrorDescriptor, 6> net_catalog{{
    {ErrorCode::WifiConnectFailed, "WIFI_CONNECT_FAILED", "WiFi STA Connection Failed",
     "Could not associate or obtain DHCP lease from configured WiFi AP; retry scheduled.",
     "Verify WiFi SSID, WPA2/WPA3 password, and 2.4 GHz signal strength in Configuration."},
    {ErrorCode::WifiDisconnected, "WIFI_DISCONNECTED", "WiFi STA Link Lost",
     "WiFi station disconnected from access point; automatic reconnect active.",
     "Check router availability; KNX-DALI lighting control continues unaffected."},
    {ErrorCode::MqttConnectFailed, "MQTT_CONNECT_FAILED", "MQTT Broker Connection Failed",
     "TCP or MQTT handshake with the configured broker failed; exponential backoff active.",
     "Verify MQTT broker IP/hostname, port, username, and password in Configuration."},
    {ErrorCode::MqttDisconnected, "MQTT_DISCONNECTED", "MQTT Broker Disconnected",
     "Connection to MQTT broker closed; automatic reconnect active.", "Check MQTT broker service status."},
    {ErrorCode::OtaValidationFailed, "OTA_INVALID_IMAGE", "OTA Image Validation Failed",
     "Uploaded .bin file failed ESP-IDF app header magic byte, size, or partition check.",
     "Upload a valid ESP32-C6 firmware.bin or littlefs.bin image."},
    {ErrorCode::OtaFlashFailed, "OTA_FLASH_FAILED", "OTA Flash Write Error",
     "Flash erase or write operation failed during OTA binary upload.",
     "Ensure stable KNX bus power and retry the firmware upload."},
}};

constexpr std::array<ErrorDescriptor, 30> full_catalog{{
    catalog[0],     catalog[1],     catalog[2],     catalog[3],     catalog[4],     catalog[5],
    catalog[6],     catalog[7],     catalog[8],     catalog[9],     catalog[10],    catalog[11],
    catalog[12],    catalog[13],    catalog[14],    catalog[15],    catalog[16],    catalog[17],
    catalog[18],    catalog[19],    catalog[20],    catalog[21],    catalog[22],    catalog[23],
    net_catalog[0], net_catalog[1], net_catalog[2], net_catalog[3], net_catalog[4], net_catalog[5],
}};

}  // namespace

const ErrorDescriptor& describe_error(ErrorCode code) noexcept {
    for (const auto& entry : full_catalog) {
        if (entry.code == code) {
            return entry;
        }
    }
    return full_catalog[0];
}

const ErrorDescriptor* error_catalog(std::size_t& count) noexcept {
    count = full_catalog.size();
    return full_catalog.data();
}

}
