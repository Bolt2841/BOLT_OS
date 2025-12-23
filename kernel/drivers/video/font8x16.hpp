#pragma once
/* ===========================================================================
 * BOLT OS - 8x16 Bitmap Font
 * Clean, modern-looking font for high resolution graphics
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

class Font8x16 {
public:
    static constexpr u32 CHAR_WIDTH = 8;
    static constexpr u32 CHAR_HEIGHT = 16;
    
    // Get the font bitmap for a character (16 bytes per char)
    static const u8* get_glyph(char c);
    
private:
    static const u8 font_data[];
};

} // namespace bolt::drivers
