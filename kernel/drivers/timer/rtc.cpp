/* ===========================================================================
 * BOLT OS - RTC Driver Implementation
 * =========================================================================== */

#include "rtc.hpp"
#include "../video/vga.hpp"
#include "../video/console.hpp"
#include "../../core/sys/io.hpp"

namespace bolt::drivers {

bool RTC::initialized = false;

void RTC::init() {
    // RTC doesn't need special initialization
    initialized = true;
}

u8 RTC::read_register(u8 reg) {
    io::outb(CMOS_ADDR, reg);
    return io::inb(CMOS_DATA);
}

bool RTC::is_updating() {
    io::outb(CMOS_ADDR, 0x0A);
    return (io::inb(CMOS_DATA) & 0x80) != 0;
}

u8 RTC::bcd_to_binary(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

RTC::DateTime RTC::get_datetime() {
    // Wait for RTC to not be updating
    while (is_updating());
    
    DateTime dt;
    
    u8 registerB = read_register(0x0B);
    bool bcd_mode = !(registerB & 0x04);
    bool hour_24 = registerB & 0x02;
    
    dt.second = read_register(0x00);
    dt.minute = read_register(0x02);
    dt.hour = read_register(0x04);
    dt.day = read_register(0x07);
    dt.month = read_register(0x08);
    dt.year = read_register(0x09);
    dt.weekday = read_register(0x06);
    
    // Convert BCD to binary if needed
    if (bcd_mode) {
        dt.second = bcd_to_binary(dt.second);
        dt.minute = bcd_to_binary(dt.minute);
        dt.hour = bcd_to_binary(dt.hour & 0x7F) | (dt.hour & 0x80);
        dt.day = bcd_to_binary(dt.day);
        dt.month = bcd_to_binary(dt.month);
        dt.year = bcd_to_binary(dt.year);
    }
    
    // Handle 12-hour format
    if (!hour_24 && (dt.hour & 0x80)) {
        dt.hour = ((dt.hour & 0x7F) + 12) % 24;
    }
    
    // Assume 21st century
    dt.year += 2000;
    
    return dt;
}

void RTC::print_time() {
    DateTime dt = get_datetime();
    
    if (dt.hour < 10) VGA::putchar('0');
    VGA::print_dec(dt.hour);
    VGA::putchar(':');
    if (dt.minute < 10) VGA::putchar('0');
    VGA::print_dec(dt.minute);
    VGA::putchar(':');
    if (dt.second < 10) VGA::putchar('0');
    VGA::print_dec(dt.second);
}

void RTC::print_date() {
    DateTime dt = get_datetime();
    
    VGA::print_dec(dt.year);
    VGA::putchar('-');
    if (dt.month < 10) VGA::putchar('0');
    VGA::print_dec(dt.month);
    VGA::putchar('-');
    if (dt.day < 10) VGA::putchar('0');
    VGA::print_dec(dt.day);
}

void RTC::print_datetime() {
    print_date();
    VGA::putchar(' ');
    print_time();
}

void RTC::print_datetime_to_console() {
    DateTime dt = get_datetime();
    
    // Date
    Console::print_dec(dt.year);
    Console::print("-");
    if (dt.month < 10) Console::print("0");
    Console::print_dec(dt.month);
    Console::print("-");
    if (dt.day < 10) Console::print("0");
    Console::print_dec(dt.day);
    
    Console::print(" ");
    
    // Time
    if (dt.hour < 10) Console::print("0");
    Console::print_dec(dt.hour);
    Console::print(":");
    if (dt.minute < 10) Console::print("0");
    Console::print_dec(dt.minute);
    Console::print(":");
    if (dt.second < 10) Console::print("0");
    Console::print_dec(dt.second);
}

} // namespace bolt::drivers
