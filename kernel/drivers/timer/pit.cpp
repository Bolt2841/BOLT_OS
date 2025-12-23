/* ===========================================================================
 * BOLT OS - Uptime Tracker (using RTC)
 * =========================================================================== */

#include "pit.hpp"
#include "rtc.hpp"

namespace bolt::drivers {

u32 PIT::boot_hour = 0;
u32 PIT::boot_minute = 0;
u32 PIT::boot_second = 0;

void PIT::init(u32) {
    // Store boot time from RTC
    RTC::DateTime dt = RTC::get_datetime();
    boot_hour = dt.hour;
    boot_minute = dt.minute;
    boot_second = dt.second;
}

u32 PIT::get_ticks() {
    return get_seconds() * 1000;
}

u32 PIT::get_seconds() {
    RTC::DateTime dt = RTC::get_datetime();
    
    // Convert current time to seconds since midnight
    u32 current = dt.hour * 3600 + dt.minute * 60 + dt.second;
    u32 boot = boot_hour * 3600 + boot_minute * 60 + boot_second;
    
    // Handle day wrap
    if (current < boot) {
        current += 24 * 3600;
    }
    
    return current - boot;
}

u32 PIT::get_milliseconds() {
    return get_seconds() * 1000;
}

} // namespace bolt::drivers
