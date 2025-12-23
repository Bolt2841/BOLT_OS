#pragma once
/* ===========================================================================
 * BOLT OS - Kernel Logging System
 * ===========================================================================
 * Structured logging with multiple output targets and log levels.
 * Supports formatting, timestamps, and source location tracking.
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::log {

// Log severity levels
enum class Level : u8 {
    Trace   = 0,    // Very detailed tracing info
    Debug   = 1,    // Debug information
    Info    = 2,    // General information
    Warn    = 3,    // Warnings
    Error   = 4,    // Errors
    Fatal   = 5,    // Fatal errors (triggers panic)
    None    = 255   // Disable logging
};

// Log output targets
enum class Target : u8 {
    None    = 0,
    Serial  = 1 << 0,
    VGA     = 1 << 1,
    Both    = Serial | VGA
};

// Source location info
struct SourceLocation {
    const char* file;
    const char* function;
    u32 line;
};

// Log entry structure
struct LogEntry {
    Level level;
    const char* message;
    SourceLocation location;
    u32 timestamp;
};

// Log configuration
struct LogConfig {
    Level min_level;            // Minimum level to output
    Target targets;             // Output targets
    bool show_timestamp;        // Include timestamp
    bool show_location;         // Include file:line
    bool show_level;            // Include log level prefix
    bool use_colors;            // Use ANSI colors (serial) / VGA colors
};

class Logger {
public:
    // Initialize logging system
    static void init(LogConfig config = default_config());
    
    // Core logging functions
    static void log(Level level, const char* message);
    static void log(Level level, const char* message, const SourceLocation& loc);
    
    // Formatted logging (printf-style limited subset)
    static void logf(Level level, const char* format, ...);
    
    // Convenience functions for each level
    static void trace(const char* msg) { log(Level::Trace, msg); }
    static void debug(const char* msg) { log(Level::Debug, msg); }
    static void info(const char* msg)  { log(Level::Info, msg); }
    static void warn(const char* msg)  { log(Level::Warn, msg); }
    static void error(const char* msg) { log(Level::Error, msg); }
    static void fatal(const char* msg);  // Triggers panic
    
    // Configuration
    static void set_level(Level level);
    static Level get_level() { return config.min_level; }
    static void set_targets(Target targets);
    static Target get_targets() { return config.targets; }
    static void set_config(const LogConfig& cfg) { config = cfg; }
    static LogConfig get_config() { return config; }
    
    // Buffer for formatted output
    static constexpr usize FORMAT_BUFFER_SIZE = 512;
    
    // Default configuration
    static LogConfig default_config() {
        return {
            .min_level = Level::Info,
            .targets = Target::Both,
            .show_timestamp = true,
            .show_location = false,
            .show_level = true,
            .use_colors = true
        };
    }

private:
    // Write to output targets
    static void write_serial(const char* str);
    static void write_vga(const char* str, Level level);
    
    // Format helpers
    static const char* level_to_string(Level level);
    static const char* level_to_color_serial(Level level);
    static u8 level_to_color_vga(Level level);
    
    // Internal format buffer
    static char format_buffer[FORMAT_BUFFER_SIZE];
    
    // Configuration
    static LogConfig config;
    static bool initialized;
};

// Convenience macros with source location
#define LOG_TRACE(msg) ::bolt::log::Logger::log(::bolt::log::Level::Trace, msg, {__FILE__, __func__, __LINE__})
#define LOG_DEBUG(msg) ::bolt::log::Logger::log(::bolt::log::Level::Debug, msg, {__FILE__, __func__, __LINE__})
#define LOG_INFO(msg)  ::bolt::log::Logger::log(::bolt::log::Level::Info, msg, {__FILE__, __func__, __LINE__})
#define LOG_WARN(msg)  ::bolt::log::Logger::log(::bolt::log::Level::Warn, msg, {__FILE__, __func__, __LINE__})
#define LOG_ERROR(msg) ::bolt::log::Logger::log(::bolt::log::Level::Error, msg, {__FILE__, __func__, __LINE__})
#define LOG_FATAL(msg) ::bolt::log::Logger::fatal(msg)

// Formatted logging macros
#define LOGF_TRACE(fmt, ...) ::bolt::log::Logger::logf(::bolt::log::Level::Trace, fmt, ##__VA_ARGS__)
#define LOGF_DEBUG(fmt, ...) ::bolt::log::Logger::logf(::bolt::log::Level::Debug, fmt, ##__VA_ARGS__)
#define LOGF_INFO(fmt, ...)  ::bolt::log::Logger::logf(::bolt::log::Level::Info, fmt, ##__VA_ARGS__)
#define LOGF_WARN(fmt, ...)  ::bolt::log::Logger::logf(::bolt::log::Level::Warn, fmt, ##__VA_ARGS__)
#define LOGF_ERROR(fmt, ...) ::bolt::log::Logger::logf(::bolt::log::Level::Error, fmt, ##__VA_ARGS__)

// Bitwise operators for Target enum
inline Target operator|(Target a, Target b) {
    return static_cast<Target>(static_cast<u8>(a) | static_cast<u8>(b));
}

inline Target operator&(Target a, Target b) {
    return static_cast<Target>(static_cast<u8>(a) & static_cast<u8>(b));
}

inline bool has_flag(Target targets, Target flag) {
    return (static_cast<u8>(targets) & static_cast<u8>(flag)) != 0;
}

} // namespace bolt::log
