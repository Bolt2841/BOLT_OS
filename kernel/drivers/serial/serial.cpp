/* ===========================================================================
 * BOLT OS - Serial Port Driver Implementation
 * =========================================================================== */

#include "serial.hpp"
#include "../../core/sys/io.hpp"
#include "../timer/rtc.hpp"

namespace bolt::drivers {

u16 Serial::port = COM1;
bool Serial::initialized = false;

// ANSI color codes
static const char* ANSI_RESET   = "\033[0m";
static const char* ANSI_RED     = "\033[91m";
static const char* ANSI_GREEN   = "\033[92m";
static const char* ANSI_YELLOW  = "\033[93m";
static const char* ANSI_CYAN    = "\033[96m";
static const char* ANSI_WHITE   = "\033[97m";
static const char* ANSI_ORANGE  = "\033[38;5;208m";
static const char* ANSI_DIM     = "\033[90m";

bool Serial::init(u16 com_port, u32 baud) {
    port = com_port;
    
    u16 divisor = 115200 / baud;
    
    // Disable interrupts
    io::outb(port + 1, 0x00);
    
    // Enable DLAB (set baud rate divisor)
    io::outb(port + 3, 0x80);
    
    // Set divisor (lo byte)
    io::outb(port + 0, divisor & 0xFF);
    
    // Set divisor (hi byte)
    io::outb(port + 1, (divisor >> 8) & 0xFF);
    
    // 8 bits, no parity, one stop bit
    io::outb(port + 3, 0x03);
    
    // Enable FIFO, clear them, with 14-byte threshold
    io::outb(port + 2, 0xC7);
    
    // IRQs enabled, RTS/DSR set
    io::outb(port + 4, 0x0B);
    
    // Set in loopback mode, test the serial chip
    io::outb(port + 4, 0x1E);
    
    // Test serial chip (send byte 0xAE and check if it returns same byte)
    io::outb(port + 0, 0xAE);
    
    // Check if serial is faulty
    if (io::inb(port + 0) != 0xAE) {
        return false;
    }
    
    // If serial is not faulty, set it in normal operation mode
    io::outb(port + 4, 0x0F);
    
    initialized = true;
    return true;
}

bool Serial::is_transmit_empty() {
    return (io::inb(port + 5) & 0x20) != 0;
}

void Serial::write_char(char c) {
    if (!initialized) return;
    
    while (!is_transmit_empty());
    io::outb(port, c);
}

void Serial::write(const char* str) {
    while (*str) {
        if (*str == '\n') {
            write_char('\r');
        }
        write_char(*str++);
    }
}

void Serial::writeln(const char* str) {
    write(str);
    write_char('\r');
    write_char('\n');
}

void Serial::write_hex(u32 value) {
    write("0x");
    for (int i = 7; i >= 0; i--) {
        u8 nibble = (value >> (i * 4)) & 0xF;
        write_char(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

void Serial::write_dec(i32 value) {
    if (value < 0) {
        write_char('-');
        value = -value;
    }
    
    if (value == 0) {
        write_char('0');
        return;
    }
    
    char buf[12];
    int i = 0;
    
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) {
        write_char(buf[--i]);
    }
}

bool Serial::has_data() {
    if (!initialized) return false;
    return (io::inb(port + 5) & 0x01) != 0;
}

char Serial::read_char() {
    if (!initialized) return 0;
    while (!has_data());
    return io::inb(port);
}

void Serial::write_timestamp() {
    u8 h = 0, m = 0, s = 0;
    if (RTC::is_initialized()) {
        RTC::DateTime dt = RTC::get_datetime();
        h = dt.hour;
        m = dt.minute;
        s = dt.second;
    }
    
    write(ANSI_DIM);
    write("[");
    if (h < 10) write_char('0');
    write_dec(h);
    write_char(':');
    if (m < 10) write_char('0');
    write_dec(m);
    write_char(':');
    if (s < 10) write_char('0');
    write_dec(s);
    write("]");
    write(ANSI_RESET);
}

const char* Serial::type_to_string(LogType type) {
    switch (type) {
        case LogType::Info:    return " INFO  ";
        case LogType::Debug:   return " DEBUG ";
        case LogType::Loading: return " LOAD  ";
        case LogType::Success: return "  OK   ";
        case LogType::Warning: return " WARN  ";
        case LogType::Error:   return " ERROR ";
        default:               return " ????? ";
    }
}

const char* Serial::type_to_color(LogType type) {
    switch (type) {
        case LogType::Info:    return ANSI_WHITE;
        case LogType::Debug:   return ANSI_CYAN;
        case LogType::Loading: return ANSI_ORANGE;
        case LogType::Success: return ANSI_GREEN;
        case LogType::Warning: return ANSI_YELLOW;
        case LogType::Error:   return ANSI_RED;
        default:               return ANSI_WHITE;
    }
}

// Unified logging: [HH:MM:SS] [MODULE] [TYPE] Message
void Serial::log(const char* module, LogType type, const char* msg) {
    // Timestamp
    write_timestamp();
    
    // Module (fixed width padding to 8 chars)
    write(" [");
    write(ANSI_CYAN);
    int len = 0;
    const char* m = module;
    while (*m++) len++;
    // Pad to 8 chars
    write(module);
    for (int i = len; i < 8; i++) write_char(' ');
    write(ANSI_RESET);
    write("] ");
    
    // Type with color
    write("[");
    write(type_to_color(type));
    write(type_to_string(type));
    write(ANSI_RESET);
    write("] ");
    
    // Message with appropriate color
    write(type_to_color(type));
    write(msg);
    write(ANSI_RESET);
    writeln("");
}

// Unified logging with two message parts: [HH:MM:SS] [MODULE] [TYPE] Msg1 Msg2
void Serial::log(const char* module, LogType type, const char* msg1, const char* msg2) {
    // Timestamp
    write_timestamp();
    
    // Module (fixed width padding to 8 chars)
    write(" [");
    write(ANSI_CYAN);
    int len = 0;
    const char* m = module;
    while (*m++) len++;
    write(module);
    for (int i = len; i < 8; i++) write_char(' ');
    write(ANSI_RESET);
    write("] ");
    
    // Type with color
    write("[");
    write(type_to_color(type));
    write(type_to_string(type));
    write(ANSI_RESET);
    write("] ");
    
    // Message with appropriate color
    write(type_to_color(type));
    write(msg1);
    write(msg2);
    write(ANSI_RESET);
    writeln("");
}

void Serial::log_hex(const char* module, LogType type, const char* msg, u32 value) {
    // Timestamp
    write_timestamp();
    
    // Module
    write(" [");
    write(ANSI_CYAN);
    int len = 0;
    const char* m = module;
    while (*m++) len++;
    write(module);
    for (int i = len; i < 8; i++) write_char(' ');
    write(ANSI_RESET);
    write("] ");
    
    // Type
    write("[");
    write(type_to_color(type));
    write(type_to_string(type));
    write(ANSI_RESET);
    write("] ");
    
    // Message with value
    write(type_to_color(type));
    write(msg);
    write(": ");
    write_hex(value);
    write(ANSI_RESET);
    writeln("");
}

// Legacy functions - now use unified format

void Serial::debug(const char* msg) {
    log("DEBUG", LogType::Debug, msg);
}

void Serial::debug_value(const char* name, u32 value) {
    log_hex("DEBUG", LogType::Debug, name, value);
}

void Serial::log_module(const char* module, const char* msg) {
    log(module, LogType::Info, msg);
}

void Serial::log_module_hex(const char* module, const char* msg, u32 value) {
    log_hex(module, LogType::Info, msg, value);
}

void Serial::log_ok(const char* module, const char* msg) {
    log(module, LogType::Success, msg);
}

void Serial::log_fail(const char* module, const char* msg) {
    log(module, LogType::Error, msg);
}

void Serial::log_warn(const char* module, const char* msg) {
    log(module, LogType::Warning, msg);
}

void Serial::log_loading(const char* module, const char* msg) {
    log(module, LogType::Loading, msg);
}

} // namespace bolt::drivers
