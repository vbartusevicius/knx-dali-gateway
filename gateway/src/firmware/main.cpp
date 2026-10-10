#include "firmware/Application.hpp"

// ESP-IDF entry point (the equivalent of Arduino setup()).
extern "C" void app_main() {
    gateway::firmware::run();
}
