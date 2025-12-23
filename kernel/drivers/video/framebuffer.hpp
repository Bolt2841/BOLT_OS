#pragma once
/* ===========================================================================
 * BOLT OS - Framebuffer Graphics Driver
 * High-resolution VESA graphics support
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

// Color in 32-bit ARGB format
struct Color32 {
    u8 b, g, r, a;
    
    constexpr Color32() : b(0), g(0), r(0), a(255) {}
    constexpr Color32(u8 r, u8 g, u8 b, u8 a = 255) : b(b), g(g), r(r), a(a) {}
    
    // Common colors
    static constexpr Color32 Black()       { return Color32(0, 0, 0); }
    static constexpr Color32 White()       { return Color32(255, 255, 255); }
    static constexpr Color32 Red()         { return Color32(255, 0, 0); }
    static constexpr Color32 Green()       { return Color32(0, 255, 0); }
    static constexpr Color32 Blue()        { return Color32(0, 0, 255); }
    static constexpr Color32 Yellow()      { return Color32(255, 255, 0); }
    static constexpr Color32 Cyan()        { return Color32(0, 255, 255); }
    static constexpr Color32 Magenta()     { return Color32(255, 0, 255); }
    static constexpr Color32 Gray()        { return Color32(128, 128, 128); }
    static constexpr Color32 DarkGray()    { return Color32(64, 64, 64); }
    static constexpr Color32 LightGray()   { return Color32(192, 192, 192); }
    
    // Theme colors
    static constexpr Color32 Background()  { return Color32(30, 30, 30); }      // Dark background
    static constexpr Color32 Surface()     { return Color32(45, 45, 45); }      // Slightly lighter
    static constexpr Color32 Primary()     { return Color32(100, 200, 100); }   // Green accent
    static constexpr Color32 Secondary()   { return Color32(100, 150, 200); }   // Blue accent
    static constexpr Color32 Text()        { return Color32(220, 220, 220); }   // Light text
    static constexpr Color32 TextDim()     { return Color32(150, 150, 150); }   // Dimmed text
};

// VESA info structure (matches bootloader layout at 0x600)
struct VESAInfo {
    u16 width;          // 0x600
    u16 height;         // 0x602
    u8  bpp;            // 0x604
    u8  _pad;           // 0x605
    u16 pitch;          // 0x606
    u32 framebuffer;    // 0x608
    u8  enabled;        // 0x60C
} __attribute__((packed));

class Framebuffer {
public:
    static void init();
    static bool is_available();
    
    // Basic info
    static u32 width();
    static u32 height();
    static u32 bpp();
    static u32 pitch();
    
    // Character dimensions (for console calculations)
    static constexpr u32 char_width() { return CHAR_WIDTH; }
    static constexpr u32 char_height() { return CHAR_HEIGHT; }
    
    // Pixel operations
    static void put_pixel(u32 x, u32 y, Color32 color);
    static Color32 get_pixel(u32 x, u32 y);
    
    // Drawing primitives
    static void fill_rect(u32 x, u32 y, u32 w, u32 h, Color32 color);
    static void draw_rect(u32 x, u32 y, u32 w, u32 h, Color32 color);
    static void draw_line(i32 x0, i32 y0, i32 x1, i32 y1, Color32 color);
    static void draw_hline(u32 x, u32 y, u32 len, Color32 color);
    static void draw_vline(u32 x, u32 y, u32 len, Color32 color);
    
    // Mouse cursor
    static void draw_mouse_cursor(i32 x, i32 y);
    static void save_under_cursor(i32 x, i32 y);
    static void restore_under_cursor();
    
    // Screen operations
    static void clear(Color32 color = Color32::Background());
    static void swap_buffers();  // For double buffering (future)
    
    // Text rendering (8x16 font)
    static void draw_char(u32 x, u32 y, char c, Color32 fg, Color32 bg = Color32::Background());
    static void draw_string(u32 x, u32 y, const char* str, Color32 fg, Color32 bg = Color32::Background());
    
    // Console-style text (with cursor tracking)
    static void print(const char* str);
    static void println(const char* str = "");
    static void print_dec(i32 num);
    static void set_text_color(Color32 fg, Color32 bg = Color32::Background());
    static void set_cursor(u32 col, u32 row);
    static u32 get_cursor_col();
    static u32 get_cursor_row();
    static void scroll();

private:
    static u8* fb_ptr;
    static u32 fb_width;
    static u32 fb_height;
    static u32 fb_bpp;
    static u32 fb_pitch;
    static bool available;
    
    // Text cursor
    static u32 cursor_col;
    static u32 cursor_row;
    static Color32 text_fg;
    static Color32 text_bg;
    
    static constexpr u32 CHAR_WIDTH = 8;
    static constexpr u32 CHAR_HEIGHT = 16;
    
    // Mouse cursor backup
    static constexpr u32 CURSOR_SIZE = 12;
    static u32 cursor_backup[CURSOR_SIZE * CURSOR_SIZE];
    static i32 cursor_backup_x;
    static i32 cursor_backup_y;
    static bool cursor_saved;
};

} // namespace bolt::drivers
