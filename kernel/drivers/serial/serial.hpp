#pragma once
/* ===========================================================================
 * BOLT OS - Serial Port Driver (COM1)
 * ===========================================================================
 * For debug output and potential future serial communication
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

// Log types for consistent styling
enum class LogType {
    Info,       // General info - white
    Debug,      // Debug info - cyan
    Loading,    // Loading/starting - yellow/orange
    Success,    // Success/OK - green
    Warning,    // Warning - yellow
    Error       // Error/fail - red
};

class Serial {
public:
    static constexpr u16 COM1 = 0x3F8;
    static constexpr u16 COM2 = 0x2F8;
    
    static bool init(u16 port = COM1, u32 baud = 115200);
    
    static void write_char(char c);
    static void write(const char* str);
    static void write_hex(u32 value);
    static void write_dec(i32 value);
    static void writeln(const char* str = "");
    
    static bool has_data();
    static char read_char();
    
    // Unified logging with consistent format:
    // [HH:MM:SS] [MODULE] [TYPE] Message
    static void log(const char* module, LogType type, const char* msg);
    static void log(const char* module, LogType type, const char* msg1, const char* msg2);
    static void log_hex(const char* module, LogType type, const char* msg, u32 value);
    
    // Legacy debug helpers (deprecated - use log() instead)
    static void debug(const char* msg);
    static void debug_value(const char* name, u32 value);
    
    // Legacy module logging (internally uses new unified format)
    static void log_module(const char* module, const char* msg);
    static void log_module_hex(const char* module, const char* msg, u32 value);
    static void log_ok(const char* module, const char* msg);
    static void log_fail(const char* module, const char* msg);
    static void log_warn(const char* module, const char* msg);
    static void log_loading(const char* module, const char* msg);
    
private:
    static u16 port;
    static bool initialized;
    
    static bool is_transmit_empty();
    static void write_timestamp();
    static const char* type_to_string(LogType type);
    static const char* type_to_color(LogType type);
};

// Global debug macros - clean module-prefixed output
#define DEBUG_LOG(msg) bolt::drivers::Serial::debug(msg)
#define DEBUG_VAL(name, val) bolt::drivers::Serial::debug_value(name, val)

// Unified logging macros with consistent format
// Usage: DBG_INFO("STORAGE", "Initializing...")
#define DBG_INFO(module, msg)    bolt::drivers::Serial::log(module, bolt::drivers::LogType::Info, msg)
#define DBG_DEBUG(module, msg)   bolt::drivers::Serial::log(module, bolt::drivers::LogType::Debug, msg)
#define DBG_LOADING(module, msg) bolt::drivers::Serial::log(module, bolt::drivers::LogType::Loading, msg)
#define DBG_SUCCESS(module, msg) bolt::drivers::Serial::log(module, bolt::drivers::LogType::Success, msg)
#define DBG_WARNING(module, msg) bolt::drivers::Serial::log(module, bolt::drivers::LogType::Warning, msg)
#define DBG_ERROR(module, msg)   bolt::drivers::Serial::log(module, bolt::drivers::LogType::Error, msg)

// Legacy macros (map to new unified format)
#define DBG(module, msg)         bolt::drivers::Serial::log_loading(module, msg)
#define DBG_HEX(module, msg, val) bolt::drivers::Serial::log_module_hex(module, msg, val)
#define DBG_OK(module, msg)      bolt::drivers::Serial::log_ok(module, msg)
#define DBG_FAIL(module, msg)    bolt::drivers::Serial::log_fail(module, msg)
#define DBG_WARN(module, msg)    bolt::drivers::Serial::log_warn(module, msg)

} // namespace bolt::drivers
