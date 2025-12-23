#pragma once
/* ===========================================================================
 * BOLT OS - Uptime Tracker (using RTC)
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

class PIT {
public:
    static void init(u32 frequency = 1000);
    static void tick() {}  // No-op without interrupts
    
    static u32 get_ticks();
    static u32 get_seconds();
    static u32 get_milliseconds();
    
private:
    static u32 boot_hour;
    static u32 boot_minute;
    static u32 boot_second;
};

} // namespace bolt::drivers
