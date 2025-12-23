#pragma once
/* ===========================================================================
 * BOLT OS - PS/2 Mouse Driver
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

struct MouseState {
    i32 x;
    i32 y;
    bool left_button;
    bool right_button;
    bool middle_button;
    i8 scroll_delta;  // Scroll wheel: positive = up, negative = down
};

class Mouse {
public:
    static void init();
    static void handle_interrupt();
    static void poll();  // Poll for mouse data (call frequently)
    
    static MouseState get_state();
    static i32 get_x();
    static i32 get_y();
    static bool left_pressed();
    static bool right_pressed();
    static bool middle_pressed();
    static i8 get_scroll_delta();  // Get and clear scroll delta
    
    // Screen bounds (set dynamically based on video mode)
    static void set_bounds(i32 width, i32 height);
    
private:
    static constexpr u16 DATA_PORT    = 0x60;
    static constexpr u16 STATUS_PORT  = 0x64;
    static constexpr u16 COMMAND_PORT = 0x64;
    
    static void wait_write();
    static void wait_read();
    static void write_command(u8 cmd);
    static void write_data(u8 data);
    static u8 read_data();
    
    static MouseState state;
    static u8 packet[4];  // 4 bytes for scroll wheel mice
    static u8 packet_index;
    static u8 packet_size;  // 3 for standard, 4 for scroll wheel
    static bool has_wheel;
    static i32 screen_width;
    static i32 screen_height;
};

} // namespace bolt::drivers
