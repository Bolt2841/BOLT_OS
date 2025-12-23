#pragma once
/* ===========================================================================
 * BOLT OS - Real-Time Clock (RTC) Driver
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

class RTC {
public:
    static constexpr u16 CMOS_ADDR = 0x70;
    static constexpr u16 CMOS_DATA = 0x71;
    
    struct DateTime {
        u8 second;
        u8 minute;
        u8 hour;
        u8 day;
        u8 month;
        u16 year;
        u8 weekday;
    };
    
    static void init();
    static bool is_initialized() { return initialized; }
    static DateTime get_datetime();
    
    static void print_date();
    static void print_time();
    static void print_datetime();
    static void print_datetime_to_console();  // For Console abstraction
    
private:
    static u8 read_register(u8 reg);
    static bool is_updating();
    static u8 bcd_to_binary(u8 bcd);
    static bool initialized;
};

} // namespace bolt::drivers
