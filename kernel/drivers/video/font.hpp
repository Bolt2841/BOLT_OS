#pragma once
/* ===========================================================================
 * BOLT OS - Bitmap Font Renderer
 * ===========================================================================
 * 8x8 bitmap font for graphics mode text rendering
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

class Font {
public:
    static constexpr i32 CHAR_WIDTH = 8;
    static constexpr i32 CHAR_HEIGHT = 8;
    
    // Draw a single character
    static void draw_char(i32 x, i32 y, char c, u8 fg, u8 bg = 0);
    
    // Draw a string
    static void draw_string(i32 x, i32 y, const char* str, u8 fg, u8 bg = 0);
    
    // Get the font bitmap for a character
    static const u8* get_glyph(char c);
    
    // Calculate string width in pixels
    static i32 string_width(const char* str);
    
private:
    static const u8 font_data[];
};

} // namespace bolt::drivers
