#pragma once

#include <esp_console.h>

#include "commissioning/Commissioning.hpp"
#include "control/Command.hpp"
#include "firmware/Queue.hpp"

namespace gateway::firmware {

// Bench/debug command line over the ESP32 USB port (status, level, on, off, read,
// recover, dim, scene, store, commissioning commands) built on ESP-IDF's
// console REPL. Development tool, not the production control path.
class UsbConsole final {
public:
    UsbConsole(Queue<control::Command>& commands, Queue<commissioning::Snapshot>& snapshots,
               Queue<commissioning::InputSnapshot>& input_snapshots, bool dali_configured,
               bool storage_available) noexcept;
    ~UsbConsole();
    UsbConsole(const UsbConsole&) = delete;
    UsbConsole& operator=(const UsbConsole&) = delete;

    esp_err_t start();

private:
    esp_err_t register_command(const char* name, const char* hint, esp_console_cmd_func_with_context_t handler);
    int submit(const control::Command& command);
    static int status_command(void* context, int argc, char** argv);
    static int level_command(void* context, int argc, char** argv);
    static int on_command(void* context, int argc, char** argv);
    static int off_command(void* context, int argc, char** argv);
    static int read_command(void* context, int argc, char** argv);
    static int recover_command(void* context, int argc, char** argv);
    static int dim_command(void* context, int argc, char** argv);
    static int scene_command(void* context, int argc, char** argv);
    static int store_command(void* context, int argc, char** argv);
    static int scan_command(void* context, int argc, char** argv);
    static int assign_command(void* context, int argc, char** argv);
    static int replace_command(void* context, int argc, char** argv);
    static int identify_command(void* context, int argc, char** argv);
    static int faults_command(void* context, int argc, char** argv);
    static int reset_addresses_command(void* context, int argc, char** argv);
    static int inputs_command(void* context, int argc, char** argv);
    static int factory_reset_command(void* context, int argc, char** argv);

    Queue<control::Command>& commands_;
    Queue<commissioning::Snapshot>& snapshots_;
    Queue<commissioning::InputSnapshot>& input_snapshots_;
    bool dali_configured_;
    bool storage_available_;
    esp_console_repl_t* repl_ = nullptr;
};

}
