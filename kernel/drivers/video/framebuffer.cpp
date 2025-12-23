/* ===========================================================================
 * BOLT OS - Framebuffer Graphics Implementation
 * =========================================================================== */

#include "framebuffer.hpp"
#include "font.hpp"
#include "font8x16.hpp"
#include "../../core/memory/heap.hpp"
#include "../../core/memory/vmm.hpp"
#include "../../core/sys/system.hpp"

namespace bolt::drivers {

// Static member definitions
u8* Framebuffer::fb_ptr = nullptr;
u32 Framebuffer::fb_width = 0;
u32 Framebuffer::fb_height = 0;
u32 Framebuffer::fb_bpp = 0;
u32 Framebuffer::fb_pitch = 0;
bool Framebuffer::available = false;

u32 Framebuffer::cursor_col = 0;
u32 Framebuffer::cursor_row = 0;
Color32 Framebuffer::text_fg = Color32::Text();
Color32 Framebuffer::text_bg = Color32::Background();

// Mouse cursor backup
u32 Framebuffer::cursor_backup[CURSOR_SIZE * CURSOR_SIZE] = {0};
i32 Framebuffer::cursor_backup_x = -1;
i32 Framebuffer::cursor_backup_y = -1;
bool Framebuffer::cursor_saved = false;

// VESA info is stored by bootloader at fixed address
static volatile VESAInfo* vesa_info = reinterpret_cast<volatile VESAInfo*>(0x600);

void Framebuffer::init() {
    // Check if VESA was enabled by bootloader
    if (vesa_info->enabled != 1) {
        available = false;
        return;
    }
    
    fb_width = vesa_info->width;
    fb_height = vesa_info->height;
    fb_bpp = vesa_info->bpp;
    fb_pitch = vesa_info->pitch;
    u32 fb_phys = vesa_info->framebuffer;
    
    // Validate
    if (fb_width == 0 || fb_height == 0 || fb_phys == 0) {
        available = false;
        return;
    }
    
    // Calculate framebuffer size (round up to page boundary)
    u32 fb_size = fb_pitch * fb_height;
    u32 fb_pages = (fb_size + 0xFFF) / 0x1000;
    
    // Identity-map the framebuffer memory region
    for (u32 i = 0; i < fb_pages; i++) {
        u32 addr = fb_phys + (i * 0x1000);
        mem::VMM::map_page(addr, addr, mem::PageFlags::Present | mem::PageFlags::ReadWrite);
    }
    
    fb_ptr = reinterpret_cast<u8*>(fb_phys);
    available = true;
    
    // Report detected video info to System
    if (sys::g_system) {
        sys::g_system->video.framebuffer = fb_phys;
        sys::g_system->video.width = fb_width;
        sys::g_system->video.height = fb_height;
        sys::g_system->video.pitch = fb_pitch;
        sys::g_system->video.bpp = static_cast<u8>(fb_bpp);
        sys::g_system->video.available = true;
        sys::g_system->video.is_graphics = true;
    }
    
    // Clear screen to background color
    clear();
}

bool Framebuffer::is_available() {
    return available;
}

u32 Framebuffer::width() { return fb_width; }
u32 Framebuffer::height() { return fb_height; }
u32 Framebuffer::bpp() { return fb_bpp; }
u32 Framebuffer::pitch() { return fb_pitch; }

void Framebuffer::put_pixel(u32 x, u32 y, Color32 color) {
    if (!available || x >= fb_width || y >= fb_height) return;
    
    u32 offset = y * fb_pitch + x * (fb_bpp / 8);
    
    if (fb_bpp == 32) {
        u32* pixel = reinterpret_cast<u32*>(fb_ptr + offset);
        *pixel = *reinterpret_cast<u32*>(&color);
    } else if (fb_bpp == 24) {
        fb_ptr[offset] = color.b;
        fb_ptr[offset + 1] = color.g;
        fb_ptr[offset + 2] = color.r;
    } else if (fb_bpp == 16) {
        // RGB565
        u16 pixel = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
        *reinterpret_cast<u16*>(fb_ptr + offset) = pixel;
    }
}

Color32 Framebuffer::get_pixel(u32 x, u32 y) {
    if (!available || x >= fb_width || y >= fb_height) return Color32::Black();
    
    u32 offset = y * fb_pitch + x * (fb_bpp / 8);
    
    if (fb_bpp == 32) {
        return *reinterpret_cast<Color32*>(fb_ptr + offset);
    } else if (fb_bpp == 24) {
        return Color32(fb_ptr[offset + 2], fb_ptr[offset + 1], fb_ptr[offset]);
    }
    return Color32::Black();
}

void Framebuffer::fill_rect(u32 x, u32 y, u32 w, u32 h, Color32 color) {
    if (!available) return;
    
    // Clip to screen
    if (x >= fb_width || y >= fb_height) return;
    if (x + w > fb_width) w = fb_width - x;
    if (y + h > fb_height) h = fb_height - y;
    
    u32 bytes_per_pixel = fb_bpp / 8;
    
    for (u32 py = y; py < y + h; py++) {
        u8* row = fb_ptr + py * fb_pitch + x * bytes_per_pixel;
        
        if (fb_bpp == 32) {
            u32* pixels = reinterpret_cast<u32*>(row);
            u32 pixel_val = *reinterpret_cast<u32*>(&color);
            for (u32 i = 0; i < w; i++) {
                pixels[i] = pixel_val;
            }
        } else if (fb_bpp == 24) {
            for (u32 i = 0; i < w; i++) {
                row[i * 3] = color.b;
                row[i * 3 + 1] = color.g;
                row[i * 3 + 2] = color.r;
            }
        } else if (fb_bpp == 16) {
            u16* pixels = reinterpret_cast<u16*>(row);
            u16 pixel_val = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
            for (u32 i = 0; i < w; i++) {
                pixels[i] = pixel_val;
            }
        }
    }
}

void Framebuffer::draw_rect(u32 x, u32 y, u32 w, u32 h, Color32 color) {
    if (!available || w < 2 || h < 2) return;
    draw_hline(x, y, w, color);
    draw_hline(x, y + h - 1, w, color);
    draw_vline(x, y, h, color);
    draw_vline(x + w - 1, y, h, color);
}

void Framebuffer::draw_hline(u32 x, u32 y, u32 len, Color32 color) {
    if (!available || y >= fb_height) return;
    if (x >= fb_width) return;
    if (x + len > fb_width) len = fb_width - x;
    
    u32 bytes_per_pixel = fb_bpp / 8;
    u8* row = fb_ptr + y * fb_pitch + x * bytes_per_pixel;
    
    if (fb_bpp == 32) {
        u32* pixels = reinterpret_cast<u32*>(row);
        u32 pixel_val = *reinterpret_cast<u32*>(&color);
        for (u32 i = 0; i < len; i++) pixels[i] = pixel_val;
    } else if (fb_bpp == 24) {
        for (u32 i = 0; i < len; i++) {
            row[i * 3] = color.b;
            row[i * 3 + 1] = color.g;
            row[i * 3 + 2] = color.r;
        }
    }
}

void Framebuffer::draw_vline(u32 x, u32 y, u32 len, Color32 color) {
    for (u32 i = 0; i < len; i++) {
        put_pixel(x, y + i, color);
    }
}

void Framebuffer::draw_line(i32 x0, i32 y0, i32 x1, i32 y1, Color32 color) {
    // Bresenham's line algorithm
    i32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
    i32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
    i32 sx = x0 < x1 ? 1 : -1;
    i32 sy = y0 < y1 ? 1 : -1;
    i32 err = dx - dy;
    
    while (true) {
        if (x0 >= 0 && y0 >= 0) {
            put_pixel(static_cast<u32>(x0), static_cast<u32>(y0), color);
        }
        
        if (x0 == x1 && y0 == y1) break;
        
        i32 e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void Framebuffer::clear(Color32 color) {
    if (!available) return;
    fill_rect(0, 0, fb_width, fb_height, color);
    cursor_col = 0;
    cursor_row = 0;
    cursor_saved = false;  // Invalidate cursor backup on clear
}

void Framebuffer::swap_buffers() {
    // Future: implement double buffering
}

// Mouse cursor - simple arrow shape
void Framebuffer::save_under_cursor(i32 x, i32 y) {
    if (!available) return;
    
    for (u32 py = 0; py < CURSOR_SIZE; py++) {
        for (u32 px = 0; px < CURSOR_SIZE; px++) {
            i32 sx = x + static_cast<i32>(px);
            i32 sy = y + static_cast<i32>(py);
            if (sx >= 0 && sy >= 0 && static_cast<u32>(sx) < fb_width && static_cast<u32>(sy) < fb_height) {
                u32 offset = static_cast<u32>(sy) * fb_pitch + static_cast<u32>(sx) * (fb_bpp / 8);
                if (fb_bpp == 32) {
                    cursor_backup[py * CURSOR_SIZE + px] = *reinterpret_cast<u32*>(fb_ptr + offset);
                }
            } else {
                cursor_backup[py * CURSOR_SIZE + px] = 0;
            }
        }
    }
    cursor_backup_x = x;
    cursor_backup_y = y;
    cursor_saved = true;
}

void Framebuffer::restore_under_cursor() {
    if (!available || !cursor_saved) return;
    
    for (u32 py = 0; py < CURSOR_SIZE; py++) {
        for (u32 px = 0; px < CURSOR_SIZE; px++) {
            i32 sx = cursor_backup_x + static_cast<i32>(px);
            i32 sy = cursor_backup_y + static_cast<i32>(py);
            if (sx >= 0 && sy >= 0 && static_cast<u32>(sx) < fb_width && static_cast<u32>(sy) < fb_height) {
                u32 offset = static_cast<u32>(sy) * fb_pitch + static_cast<u32>(sx) * (fb_bpp / 8);
                if (fb_bpp == 32) {
                    *reinterpret_cast<u32*>(fb_ptr + offset) = cursor_backup[py * CURSOR_SIZE + px];
                }
            }
        }
    }
    cursor_saved = false;
}

void Framebuffer::draw_mouse_cursor(i32 x, i32 y) {
    if (!available) return;
    
    // Only redraw if cursor moved
    if (cursor_saved && x == cursor_backup_x && y == cursor_backup_y) {
        return;  // Cursor hasn't moved, don't redraw
    }
    
    // Restore previous location first
    restore_under_cursor();
    
    // Save pixels under new cursor position
    save_under_cursor(x, y);
    
    // Simple arrow cursor bitmap (12x12)
    static const u8 cursor_shape[12][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,1,1,1,1,0,0,0},
        {1,2,2,1,2,1,0,0,0,0,0,0},
        {1,1,0,0,1,2,1,0,0,0,0,0},
        {0,0,0,0,0,1,1,0,0,0,0,0},
    };
    
    Color32 white(255, 255, 255);
    Color32 black(0, 0, 0);
    
    for (u32 py = 0; py < 12; py++) {
        for (u32 px = 0; px < 12; px++) {
            i32 sx = x + static_cast<i32>(px);
            i32 sy = y + static_cast<i32>(py);
            if (cursor_shape[py][px] == 1) {
                if (sx >= 0 && sy >= 0) put_pixel(static_cast<u32>(sx), static_cast<u32>(sy), black);
            } else if (cursor_shape[py][px] == 2) {
                if (sx >= 0 && sy >= 0) put_pixel(static_cast<u32>(sx), static_cast<u32>(sy), white);
            }
        }
    }
}

// Use the 8x16 font for cleaner text
void Framebuffer::draw_char(u32 x, u32 y, char c, Color32 fg, Color32 bg) {
    if (!available) return;
    
    const u8* glyph = Font8x16::get_glyph(c);
    
    // Draw 8x16 font natively (no scaling)
    for (u32 py = 0; py < CHAR_HEIGHT; py++) {
        u8 row = glyph[py];
        for (u32 px = 0; px < CHAR_WIDTH; px++) {
            Color32 color = (row & (0x80 >> px)) ? fg : bg;
            put_pixel(x + px, y + py, color);
        }
    }
}

void Framebuffer::draw_string(u32 x, u32 y, const char* str, Color32 fg, Color32 bg) {
    if (!available) return;
    
    while (*str) {
        draw_char(x, y, *str, fg, bg);
        x += CHAR_WIDTH;
        str++;
    }
}

void Framebuffer::set_text_color(Color32 fg, Color32 bg) {
    text_fg = fg;
    text_bg = bg;
}

void Framebuffer::set_cursor(u32 col, u32 row) {
    cursor_col = col;
    cursor_row = row;
}

u32 Framebuffer::get_cursor_col() { return cursor_col; }
u32 Framebuffer::get_cursor_row() { return cursor_row; }

void Framebuffer::print(const char* str) {
    if (!available) return;
    
    u32 max_cols = fb_width / CHAR_WIDTH;
    u32 max_rows = fb_height / CHAR_HEIGHT;
    
    while (*str) {
        char c = *str++;
        
        if (c == '\n') {
            cursor_col = 0;
            cursor_row++;
        } else if (c == '\r') {
            cursor_col = 0;
        } else if (c == '\t') {
            cursor_col = (cursor_col + 4) & ~3;
        } else if (c == '\b') {
            if (cursor_col > 0) {
                cursor_col--;
                draw_char(cursor_col * CHAR_WIDTH, cursor_row * CHAR_HEIGHT, ' ', text_fg, text_bg);
            }
        } else {
            draw_char(cursor_col * CHAR_WIDTH, cursor_row * CHAR_HEIGHT, c, text_fg, text_bg);
            cursor_col++;
        }
        
        if (cursor_col >= max_cols) {
            cursor_col = 0;
            cursor_row++;
        }
        
        if (cursor_row >= max_rows) {
            scroll();
            cursor_row = max_rows - 1;
        }
    }
}

void Framebuffer::println(const char* str) {
    print(str);
    print("\n");
}

void Framebuffer::print_dec(i32 num) {
    if (!available) return;
    
    if (num < 0) {
        print("-");
        num = -num;
    }
    if (num == 0) {
        print("0");
        return;
    }
    
    char buf[12];
    int i = 0;
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    // Print in reverse
    while (i > 0) {
        char c[2] = {buf[--i], 0};
        print(c);
    }
}

void Framebuffer::scroll() {
    if (!available) return;
    
    // Move screen up by one line
    u32 line_bytes = CHAR_HEIGHT * fb_pitch;
    u32 screen_bytes = (fb_height - CHAR_HEIGHT) * fb_pitch;
    
    // Copy rows up
    u8* dst = fb_ptr;
    u8* src = fb_ptr + line_bytes;
    for (u32 i = 0; i < screen_bytes; i++) {
        dst[i] = src[i];
    }
    
    // Clear bottom line
    u32 max_cols = fb_width / CHAR_WIDTH;
    for (u32 col = 0; col < max_cols; col++) {
        draw_char(col * CHAR_WIDTH, (fb_height / CHAR_HEIGHT - 1) * CHAR_HEIGHT, 
                  ' ', text_fg, text_bg);
    }
}

} // namespace bolt::drivers
