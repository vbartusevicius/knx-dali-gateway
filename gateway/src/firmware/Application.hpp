#pragma once

namespace gateway::firmware {

// Firmware composition root: opens storage, creates the DALI driver, wires both into
// LightingService and starts the console. Called once from app_main.
void run();

// Erases the KNX stack's "eeprom" namespace and our "targets_v1" namespace,
// then restarts. DALI gear short addresses are not touched. Never returns.
[[noreturn]] void factory_reset();

}
