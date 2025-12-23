#pragma once
/* ===========================================================================
 * BOLT OS - PS/2 Keyboard Driver
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "../../core/sys/config.hpp"

namespace bolt::drivers {

// Special key codes (returned as negative values or high bytes)
enum class SpecialKey : u8 {
    None = 0,
    Escape = 0x7F,
    Up = 0x80,
    Down = 0x81,
    Left = 0x82,
    Right = 0x83,
    Home = 0x84,
    End = 0x85,
    PageUp = 0x86,
    PageDown = 0x87,
    Delete = 0x88,
    Insert = 0x89,
    F1 = 0x8A,
    F2 = 0x8B,
    F3 = 0x8C,
    F4 = 0x8D,
    F5 = 0x8E,
    F6 = 0x8F,
    F7 = 0x90,
    F8 = 0x91,
    F9 = 0x92,
    F10 = 0x93,
    F11 = 0x94,
    F12 = 0x95
};

struct KeyEvent {
    char ascii;           // ASCII character (0 if special key)
    SpecialKey special;   // Special key code
    bool shift;
    bool ctrl;
    bool alt;
};

class Keyboard {
public:
    // Use config values for PS/2 ports
    static constexpr u16 DATA_PORT = config::PS2_DATA_PORT;
    static constexpr u16 STATUS_PORT = config::PS2_STATUS_PORT;
    
    static void init();
    static bool has_key();
    static void drain_mouse_data();  // Drain pending mouse data from buffer
    static char getchar();           // Blocking, ASCII only
    static char poll();              // Non-blocking ASCII
    static KeyEvent poll_event();    // Full key event with special keys
    static KeyEvent get_event();     // Blocking full event
    
    static bool is_shift_pressed() { return shift_pressed; }
    static bool is_ctrl_pressed() { return ctrl_pressed; }
    static bool is_alt_pressed() { return alt_pressed; }
    
private:
    static char scancode_to_char(u8 scancode);
    static SpecialKey scancode_to_special(u8 scancode);
    static bool shift_pressed;
    static bool ctrl_pressed;
    static bool alt_pressed;
    static bool caps_lock;
    static bool extended_key;
};

} // namespace bolt::drivers
