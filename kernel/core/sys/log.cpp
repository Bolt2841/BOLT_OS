/* ===========================================================================
 * BOLT OS - Kernel Logging System Implementation
 * ===========================================================================
 * Supports serial and VGA output with formatting
 * =========================================================================== */

#include "log.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../drivers/video/vga.hpp"
#include "../../drivers/timer/pit.hpp"
#include "../../drivers/timer/rtc.hpp"
#include "../../lib/string.hpp"
#include <stdarg.h>

namespace bolt::log {

// Static member definitions
char Logger::format_buffer[FORMAT_BUFFER_SIZE];
LogConfig Logger::config;
bool Logger::initialized = false;

void Logger::init(LogConfig cfg) {
    config = cfg;
    initialized = true;
    
    // Initial log message
    if (config.min_level <= Level::Info) {
        log(Level::Info, "Logging system initialized");
    }
}

void Logger::log(Level level, const char* message) {
    if (!initialized || level < config.min_level || level == Level::None) {
        return;
    }
    
    // Use unified Serial logging format
    if (has_flag(config.targets, Target::Serial)) {
        // Map Logger level to Serial LogType
        drivers::LogType log_type;
        switch (level) {
            case Level::Trace:
            case Level::Debug:
                log_type = drivers::LogType::Debug;
                break;
            case Level::Info:
                log_type = drivers::LogType::Info;
                break;
            case Level::Warn:
                log_type = drivers::LogType::Warning;
                break;
            case Level::Error:
            case Level::Fatal:
                log_type = drivers::LogType::Error;
                break;
            default:
                log_type = drivers::LogType::Info;
                break;
        }
        drivers::Serial::log("KERNEL", log_type, message);
    }
    
    if (has_flag(config.targets, Target::VGA)) {
        // Build output for VGA (simpler format)
        char output[FORMAT_BUFFER_SIZE];
        output[0] = '\0';
        str::cat(output, "[");
        str::cat(output, level_to_string(level));
        str::cat(output, "] ");
        str::cat(output, message);
        write_vga(output, level);
    }
}

void Logger::log(Level level, const char* message, const SourceLocation& loc) {
    if (!initialized || level < config.min_level || level == Level::None) {
        return;
    }
    
    // Use unified Serial logging format
    if (has_flag(config.targets, Target::Serial)) {
        // Map Logger level to Serial LogType
        drivers::LogType log_type;
        switch (level) {
            case Level::Trace:
            case Level::Debug:
                log_type = drivers::LogType::Debug;
                break;
            case Level::Info:
                log_type = drivers::LogType::Info;
                break;
            case Level::Warn:
                log_type = drivers::LogType::Warning;
                break;
            case Level::Error:
            case Level::Fatal:
                log_type = drivers::LogType::Error;
                break;
            default:
                log_type = drivers::LogType::Info;
                break;
        }
        
        // Build message with location if enabled
        if (config.show_location) {
            char full_msg[FORMAT_BUFFER_SIZE];
            full_msg[0] = '\0';
            
            // Extract just filename from path
            const char* filename = loc.file;
            const char* p = loc.file;
            while (*p) {
                if (*p == '/' || *p == '\\') {
                    filename = p + 1;
                }
                p++;
            }
            
            str::cat(full_msg, filename);
            str::cat(full_msg, ":");
            
            // Convert line number
            char line_str[12];
            int idx = 0;
            u32 line = loc.line;
            if (line == 0) {
                line_str[idx++] = '0';
            } else {
                char tmp[12];
                int tmp_idx = 0;
                while (line > 0) {
                    tmp[tmp_idx++] = '0' + (line % 10);
                    line /= 10;
                }
                while (tmp_idx > 0) {
                    line_str[idx++] = tmp[--tmp_idx];
                }
            }
            line_str[idx] = '\0';
            str::cat(full_msg, line_str);
            str::cat(full_msg, " ");
            str::cat(full_msg, message);
            
            drivers::Serial::log("KERNEL", log_type, full_msg);
        } else {
            drivers::Serial::log("KERNEL", log_type, message);
        }
    }
    
    if (has_flag(config.targets, Target::VGA)) {
        // Build output for VGA
        char output[FORMAT_BUFFER_SIZE];
        output[0] = '\0';
        str::cat(output, "[");
        str::cat(output, level_to_string(level));
        str::cat(output, "] ");
        str::cat(output, message);
        write_vga(output, level);
    }
}

void Logger::logf(Level level, const char* format, ...) {
    if (!initialized || level < config.min_level || level == Level::None) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Simple printf-like formatting (subset)
    char buffer[FORMAT_BUFFER_SIZE];
    usize buf_idx = 0;
    
    while (*format && buf_idx < FORMAT_BUFFER_SIZE - 1) {
        if (*format == '%' && *(format + 1)) {
            format++;
            switch (*format) {
                case 's': {
                    const char* str_arg = va_arg(args, const char*);
                    if (str_arg) {
                        while (*str_arg && buf_idx < FORMAT_BUFFER_SIZE - 1) {
                            buffer[buf_idx++] = *str_arg++;
                        }
                    }
                    break;
                }
                case 'd':
                case 'i': {
                    i32 num = va_arg(args, i32);
                    if (num < 0) {
                        buffer[buf_idx++] = '-';
                        num = -num;
                    }
                    if (num == 0) {
                        buffer[buf_idx++] = '0';
                    } else {
                        char tmp[12];
                        int tmp_idx = 0;
                        while (num > 0 && tmp_idx < 11) {
                            tmp[tmp_idx++] = '0' + (num % 10);
                            num /= 10;
                        }
                        while (tmp_idx > 0 && buf_idx < FORMAT_BUFFER_SIZE - 1) {
                            buffer[buf_idx++] = tmp[--tmp_idx];
                        }
                    }
                    break;
                }
                case 'u': {
                    u32 num = va_arg(args, u32);
                    if (num == 0) {
                        buffer[buf_idx++] = '0';
                    } else {
                        char tmp[12];
                        int tmp_idx = 0;
                        while (num > 0 && tmp_idx < 11) {
                            tmp[tmp_idx++] = '0' + (num % 10);
                            num /= 10;
                        }
                        while (tmp_idx > 0 && buf_idx < FORMAT_BUFFER_SIZE - 1) {
                            buffer[buf_idx++] = tmp[--tmp_idx];
                        }
                    }
                    break;
                }
                case 'x':
                case 'X': {
                    u32 num = va_arg(args, u32);
                    const char* hex_chars = (*format == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
                    buffer[buf_idx++] = '0';
                    if (buf_idx < FORMAT_BUFFER_SIZE - 1) buffer[buf_idx++] = 'x';
                    
                    char tmp[8];
                    int tmp_idx = 0;
                    if (num == 0) {
                        tmp[tmp_idx++] = '0';
                    } else {
                        while (num > 0 && tmp_idx < 8) {
                            tmp[tmp_idx++] = hex_chars[num & 0xF];
                            num >>= 4;
                        }
                    }
                    while (tmp_idx > 0 && buf_idx < FORMAT_BUFFER_SIZE - 1) {
                        buffer[buf_idx++] = tmp[--tmp_idx];
                    }
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    u32 num = reinterpret_cast<u32>(ptr);
                    buffer[buf_idx++] = '0';
                    if (buf_idx < FORMAT_BUFFER_SIZE - 1) buffer[buf_idx++] = 'x';
                    
                    char tmp[8];
                    for (int i = 7; i >= 0; i--) {
                        tmp[i] = "0123456789abcdef"[num & 0xF];
                        num >>= 4;
                    }
                    for (int i = 0; i < 8 && buf_idx < FORMAT_BUFFER_SIZE - 1; i++) {
                        buffer[buf_idx++] = tmp[i];
                    }
                    break;
                }
                case 'c': {
                    char c = static_cast<char>(va_arg(args, int));
                    buffer[buf_idx++] = c;
                    break;
                }
                case '%':
                    buffer[buf_idx++] = '%';
                    break;
                default:
                    buffer[buf_idx++] = '%';
                    if (buf_idx < FORMAT_BUFFER_SIZE - 1) {
                        buffer[buf_idx++] = *format;
                    }
                    break;
            }
        } else {
            buffer[buf_idx++] = *format;
        }
        format++;
    }
    
    buffer[buf_idx] = '\0';
    va_end(args);
    
    log(level, buffer);
}

void Logger::fatal(const char* msg) {
    log(Level::Fatal, msg);
    
    // Trigger kernel panic
    // For now, just halt
    asm volatile("cli");
    while (true) {
        asm volatile("hlt");
    }
}

void Logger::set_level(Level level) {
    config.min_level = level;
}

void Logger::set_targets(Target targets) {
    config.targets = targets;
}

void Logger::write_serial(const char* str) {
    // Add ANSI color if enabled
    if (config.use_colors) {
        // We'd add color codes here for serial terminals
    }
    
    drivers::Serial::write(str);
    drivers::Serial::write("\r\n");
}

void Logger::write_vga(const char* str, Level level) {
    using namespace drivers;
    
    if (config.use_colors) {
        VGA::set_color(static_cast<Color>(level_to_color_vga(level)));
    }
    
    VGA::println(str);
    
    if (config.use_colors) {
        VGA::set_color(Color::LightGray);  // Reset to default
    }
}

const char* Logger::level_to_string(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        case Level::Fatal: return "FATAL";
        default:           return "?????";
    }
}

const char* Logger::level_to_color_serial(Level level) {
    // ANSI color codes
    switch (level) {
        case Level::Trace: return "\033[90m";    // Dark gray
        case Level::Debug: return "\033[36m";    // Cyan
        case Level::Info:  return "\033[32m";    // Green
        case Level::Warn:  return "\033[33m";    // Yellow
        case Level::Error: return "\033[31m";    // Red
        case Level::Fatal: return "\033[35;1m";  // Bright magenta
        default:           return "\033[0m";     // Reset
    }
}

u8 Logger::level_to_color_vga(Level level) {
    using namespace drivers;
    switch (level) {
        case Level::Trace: return static_cast<u8>(Color::DarkGray);
        case Level::Debug: return static_cast<u8>(Color::Cyan);
        case Level::Info:  return static_cast<u8>(Color::LightGreen);
        case Level::Warn:  return static_cast<u8>(Color::Yellow);
        case Level::Error: return static_cast<u8>(Color::LightRed);
        case Level::Fatal: return static_cast<u8>(Color::Magenta);
        default:           return static_cast<u8>(Color::LightGray);
    }
}

} // namespace bolt::log
