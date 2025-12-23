#pragma once
/* ===========================================================================
 * BOLT OS - VGA Text Mode Driver
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "../../core/sys/config.hpp"

namespace bolt::drivers {

enum class Color : u8 {
    Black = 0,
    Blue = 1,
    Green = 2,
    Cyan = 3,
    Red = 4,
    Magenta = 5,
    Brown = 6,
    LightGray = 7,
    DarkGray = 8,
    LightBlue = 9,
    LightGreen = 10,
    LightCyan = 11,
    LightRed = 12,
    LightMagenta = 13,
    Yellow = 14,
    White = 15
};

// Box drawing characters (CP437)
namespace Box {
    constexpr char H_LINE      = '\xC4';  // ─
    constexpr char V_LINE      = '\xB3';  // │
    constexpr char TOP_LEFT    = '\xDA';  // ┌
    constexpr char TOP_RIGHT   = '\xBF';  // ┐
    constexpr char BOT_LEFT    = '\xC0';  // └
    constexpr char BOT_RIGHT   = '\xD9';  // ┘
    constexpr char T_DOWN      = '\xC2';  // ┬
    constexpr char T_UP        = '\xC1';  // ┴
    constexpr char T_RIGHT     = '\xC3';  // ├
    constexpr char T_LEFT      = '\xB4';  // ┤
    constexpr char CROSS       = '\xC5';  // ┼
    constexpr char BLOCK_FULL  = '\xDB';  // █
    constexpr char BLOCK_HALF  = '\xDD';  // ▌
    constexpr char SHADE_LIGHT = '\xB0';  // ░
    constexpr char SHADE_MED   = '\xB1';  // ▒
    constexpr char SHADE_DARK  = '\xB2';  // ▓
    constexpr char BULLET      = '\xF9';  // ∙
    constexpr char DIAMOND     = '\x04';  // ◆
    constexpr char ARROW_RIGHT = '\x10';  // ►
    constexpr char ARROW_LEFT  = '\x11';  // ◄
}

class VGA {
public:
    static constexpr u32 WIDTH = config::VGA_WIDTH;
    static constexpr u32 HEIGHT = config::VGA_HEIGHT;
    static constexpr u32 BUFFER_ADDR = config::VGA_BUFFER;
    
    static void init();
    static void clear();
    static void set_color(Color fg, Color bg = Color::Black);
    
    static void putchar(char c);
    static void print(const char* str);
    static void println(const char* str = "");
    
    static void print_hex(u32 value);
    static void print_dec(i32 value);
    
    static void set_cursor(u32 x, u32 y);
    static u32 get_cursor_x() { return cursor_x; }
    static u32 get_cursor_y() { return cursor_y; }
    static void enable_cursor();
    static void disable_cursor();
    
    // Direct character placement
    static void put_at(u32 x, u32 y, char c, Color fg, Color bg = Color::Black);
    
    // Box drawing helpers
    static void draw_box(u32 x, u32 y, u32 w, u32 h, Color fg, Color bg = Color::Black);
    static void fill_rect(u32 x, u32 y, u32 w, u32 h, char c, Color fg, Color bg = Color::Black);
    static void draw_hline(u32 x, u32 y, u32 len, Color fg, Color bg = Color::Black);
    
    // Banner display
    static void show_banner();
    static void show_welcome();
    static void update_status_bar();
    
    // Scroll support
    static void scroll();
    
    // Content area - full screen now (no header/status)
    static constexpr u32 HEADER_HEIGHT = 0;
    static constexpr u32 STATUS_HEIGHT = 0;
    static constexpr u32 CONTENT_START = 0;
    static constexpr u32 CONTENT_HEIGHT = HEIGHT;
    
private:
    static u16* buffer;
    static u32 cursor_x;
    static u32 cursor_y;
    static u8 color;
    
    static void update_cursor();
    static u16 make_entry(char c, u8 col);
};

} // namespace bolt::drivers
