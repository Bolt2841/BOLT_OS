#pragma once
/* ===========================================================================
 * BOLT OS - VGA Graphics Driver
 * ===========================================================================
 * Supports both text mode (80x25) and graphics mode (320x200x256)
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

enum class VideoMode {
    Text80x25,      // Standard text mode
    Graphics320x200 // VGA Mode 13h (256 colors)
};

// Standard VGA 256-color palette indices
namespace Colors {
    constexpr u8 Black        = 0;
    constexpr u8 Blue         = 1;
    constexpr u8 Green        = 2;
    constexpr u8 Cyan         = 3;
    constexpr u8 Red          = 4;
    constexpr u8 Magenta      = 5;
    constexpr u8 Brown        = 6;
    constexpr u8 LightGray    = 7;
    constexpr u8 DarkGray     = 8;
    constexpr u8 LightBlue    = 9;
    constexpr u8 LightGreen   = 10;
    constexpr u8 LightCyan    = 11;
    constexpr u8 LightRed     = 12;
    constexpr u8 LightMagenta = 13;
    constexpr u8 Yellow       = 14;
    constexpr u8 White        = 15;
}

class Graphics {
public:
    // Mode switching
    static void set_mode(VideoMode mode);
    static VideoMode get_mode();
    static void return_to_text();
    
    // Screen info
    static i32 get_width();
    static i32 get_height();
    
    // Basic drawing
    static void clear(u8 color = Colors::Black);
    static void put_pixel(i32 x, i32 y, u8 color);
    static u8 get_pixel(i32 x, i32 y);
    
    // Primitives
    static void draw_line(i32 x1, i32 y1, i32 x2, i32 y2, u8 color);
    static void draw_rect(i32 x, i32 y, i32 w, i32 h, u8 color);
    static void fill_rect(i32 x, i32 y, i32 w, i32 h, u8 color);
    static void draw_circle(i32 cx, i32 cy, i32 radius, u8 color);
    static void fill_circle(i32 cx, i32 cy, i32 radius, u8 color);
    
    // Double buffering
    static void swap_buffers();
    static void set_draw_buffer(u8* buffer);
    static u8* get_back_buffer();
    
    // Palette
    static void set_palette_color(u8 index, u8 r, u8 g, u8 b);
    static void set_default_palette();
    
private:
    static VideoMode current_mode;
    static i32 width;
    static i32 height;
    static u8* framebuffer;
    static u8* backbuffer;
    static u8 back_buffer_data[];
    
    static void mode_13h();
    static void mode_text();
};

} // namespace bolt::drivers
