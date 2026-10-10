#pragma once

namespace gateway::firmware {

// Firmware composition root: opens storage, creates the DALI driver, wires both into
// LightingService and starts the console. Called once from app_main.
void run();

}
