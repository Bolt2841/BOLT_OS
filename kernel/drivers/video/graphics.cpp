/* ===========================================================================
 * BOLT OS - VGA Graphics Driver Implementation
 * =========================================================================== */

#include "graphics.hpp"
#include "../../core/sys/io.hpp"
#include "../../core/sys/config.hpp"
#include "../../core/memory/heap.hpp"

namespace bolt::drivers {

VideoMode Graphics::current_mode = VideoMode::Text80x25;
i32 Graphics::width = config::VGA_TEXT_WIDTH;
i32 Graphics::height = config::VGA_TEXT_HEIGHT;
u8* Graphics::framebuffer = reinterpret_cast<u8*>(config::VGA_GFX_BUFFER);
u8* Graphics::backbuffer = nullptr;
u8 Graphics::back_buffer_data[config::VGA_GFX_WIDTH * config::VGA_GFX_HEIGHT];

void Graphics::set_mode(VideoMode mode) {
    if (mode == VideoMode::Graphics320x200) {
        mode_13h();
        width = config::VGA_GFX_WIDTH;
        height = config::VGA_GFX_HEIGHT;
        framebuffer = reinterpret_cast<u8*>(config::VGA_GFX_BUFFER);
        backbuffer = back_buffer_data;
        current_mode = mode;
        set_default_palette();
    } else {
        mode_text();
        width = config::VGA_TEXT_WIDTH;
        height = config::VGA_TEXT_HEIGHT;
        framebuffer = reinterpret_cast<u8*>(config::VGA_TEXT_BUFFER);
        backbuffer = nullptr;
        current_mode = mode;
    }
}

VideoMode Graphics::get_mode() {
    return current_mode;
}

void Graphics::return_to_text() {
    set_mode(VideoMode::Text80x25);
}

i32 Graphics::get_width() {
    return width;
}

i32 Graphics::get_height() {
    return height;
}

void Graphics::mode_13h() {
    // Use BIOS int 10h via real mode (simplified - set VGA registers directly)
    // VGA Mode 13h register programming
    
    // Miscellaneous output register
    io::outb(0x3C2, 0x63);
    
    // Sequencer registers
    io::outb(0x3C4, 0x00); io::outb(0x3C5, 0x03);
    io::outb(0x3C4, 0x01); io::outb(0x3C5, 0x01);
    io::outb(0x3C4, 0x02); io::outb(0x3C5, 0x0F);
    io::outb(0x3C4, 0x03); io::outb(0x3C5, 0x00);
    io::outb(0x3C4, 0x04); io::outb(0x3C5, 0x0E);
    
    // Unlock CRTC
    io::outb(0x3D4, 0x11);
    io::outb(0x3D5, io::inb(0x3D5) & 0x7F);
    
    // CRTC registers for 320x200
    static const u8 crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    
    for (u8 i = 0; i < 25; i++) {
        io::outb(0x3D4, i);
        io::outb(0x3D5, crtc_regs[i]);
    }
    
    // Graphics controller registers
    io::outb(0x3CE, 0x00); io::outb(0x3CF, 0x00);
    io::outb(0x3CE, 0x01); io::outb(0x3CF, 0x00);
    io::outb(0x3CE, 0x02); io::outb(0x3CF, 0x00);
    io::outb(0x3CE, 0x03); io::outb(0x3CF, 0x00);
    io::outb(0x3CE, 0x04); io::outb(0x3CF, 0x00);
    io::outb(0x3CE, 0x05); io::outb(0x3CF, 0x40);
    io::outb(0x3CE, 0x06); io::outb(0x3CF, 0x05);
    io::outb(0x3CE, 0x07); io::outb(0x3CF, 0x0F);
    io::outb(0x3CE, 0x08); io::outb(0x3CF, 0xFF);
    
    // Attribute controller registers
    io::inb(0x3DA);  // Reset flip-flop
    for (u8 i = 0; i < 16; i++) {
        io::outb(0x3C0, i);
        io::outb(0x3C0, i);
    }
    io::outb(0x3C0, 0x10); io::outb(0x3C0, 0x41);
    io::outb(0x3C0, 0x11); io::outb(0x3C0, 0x00);
    io::outb(0x3C0, 0x12); io::outb(0x3C0, 0x0F);
    io::outb(0x3C0, 0x13); io::outb(0x3C0, 0x00);
    io::outb(0x3C0, 0x14); io::outb(0x3C0, 0x00);
    io::outb(0x3C0, 0x20);  // Enable video
}

void Graphics::mode_text() {
    // Reset to text mode - simplified
    // In a full implementation, we'd restore all VGA registers
    // For now, we'll rely on the bootloader's initial text mode setup
    
    io::outb(0x3C2, 0x67);
    
    io::outb(0x3C4, 0x00); io::outb(0x3C5, 0x03);
    io::outb(0x3C4, 0x01); io::outb(0x3C5, 0x00);
    io::outb(0x3C4, 0x02); io::outb(0x3C5, 0x03);
    io::outb(0x3C4, 0x03); io::outb(0x3C5, 0x00);
    io::outb(0x3C4, 0x04); io::outb(0x3C5, 0x02);
}

void Graphics::clear(u8 color) {
    if (current_mode == VideoMode::Graphics320x200) {
        mem::memset(backbuffer ? backbuffer : framebuffer, color, width * height);
    }
}

void Graphics::put_pixel(i32 x, i32 y, u8 color) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    
    u8* buffer = backbuffer ? backbuffer : framebuffer;
    buffer[y * width + x] = color;
}

u8 Graphics::get_pixel(i32 x, i32 y) {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0;
    
    u8* buffer = backbuffer ? backbuffer : framebuffer;
    return buffer[y * width + x];
}

void Graphics::draw_line(i32 x1, i32 y1, i32 x2, i32 y2, u8 color) {
    // Bresenham's line algorithm
    i32 dx = x2 - x1;
    i32 dy = y2 - y1;
    
    i32 sx = (dx > 0) ? 1 : -1;
    i32 sy = (dy > 0) ? 1 : -1;
    
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    
    i32 err = dx - dy;
    
    while (true) {
        put_pixel(x1, y1, color);
        
        if (x1 == x2 && y1 == y2) break;
        
        i32 e2 = 2 * err;
        
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void Graphics::draw_rect(i32 x, i32 y, i32 w, i32 h, u8 color) {
    draw_line(x, y, x + w - 1, y, color);           // Top
    draw_line(x, y + h - 1, x + w - 1, y + h - 1, color); // Bottom
    draw_line(x, y, x, y + h - 1, color);           // Left
    draw_line(x + w - 1, y, x + w - 1, y + h - 1, color); // Right
}

void Graphics::fill_rect(i32 x, i32 y, i32 w, i32 h, u8 color) {
    for (i32 py = y; py < y + h; py++) {
        for (i32 px = x; px < x + w; px++) {
            put_pixel(px, py, color);
        }
    }
}

void Graphics::draw_circle(i32 cx, i32 cy, i32 radius, u8 color) {
    // Midpoint circle algorithm
    i32 x = radius;
    i32 y = 0;
    i32 err = 0;
    
    while (x >= y) {
        put_pixel(cx + x, cy + y, color);
        put_pixel(cx + y, cy + x, color);
        put_pixel(cx - y, cy + x, color);
        put_pixel(cx - x, cy + y, color);
        put_pixel(cx - x, cy - y, color);
        put_pixel(cx - y, cy - x, color);
        put_pixel(cx + y, cy - x, color);
        put_pixel(cx + x, cy - y, color);
        
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void Graphics::fill_circle(i32 cx, i32 cy, i32 radius, u8 color) {
    for (i32 y = -radius; y <= radius; y++) {
        for (i32 x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

void Graphics::swap_buffers() {
    if (backbuffer && current_mode == VideoMode::Graphics320x200) {
        mem::memcpy(framebuffer, backbuffer, width * height);
    }
}

void Graphics::set_draw_buffer(u8* buffer) {
    backbuffer = buffer;
}

u8* Graphics::get_back_buffer() {
    return backbuffer;
}

void Graphics::set_palette_color(u8 index, u8 r, u8 g, u8 b) {
    io::outb(0x3C8, index);
    io::outb(0x3C9, r >> 2);  // VGA uses 6-bit color
    io::outb(0x3C9, g >> 2);
    io::outb(0x3C9, b >> 2);
}

void Graphics::set_default_palette() {
    // Set up a nice 256-color palette
    // First 16 colors: standard VGA colors
    static const u8 basic_colors[][3] = {
        {0, 0, 0},       // Black
        {0, 0, 170},     // Blue
        {0, 170, 0},     // Green
        {0, 170, 170},   // Cyan
        {170, 0, 0},     // Red
        {170, 0, 170},   // Magenta
        {170, 85, 0},    // Brown
        {170, 170, 170}, // Light gray
        {85, 85, 85},    // Dark gray
        {85, 85, 255},   // Light blue
        {85, 255, 85},   // Light green
        {85, 255, 255},  // Light cyan
        {255, 85, 85},   // Light red
        {255, 85, 255},  // Light magenta
        {255, 255, 85},  // Yellow
        {255, 255, 255}  // White
    };
    
    for (u8 i = 0; i < 16; i++) {
        set_palette_color(i, basic_colors[i][0], basic_colors[i][1], basic_colors[i][2]);
    }
    
    // Colors 16-231: 6x6x6 color cube
    u8 index = 16;
    for (u8 r = 0; r < 6; r++) {
        for (u8 g = 0; g < 6; g++) {
            for (u8 b = 0; b < 6; b++) {
                set_palette_color(index++, r * 51, g * 51, b * 51);
            }
        }
    }
    
    // Colors 232-255: grayscale ramp
    for (u8 i = 0; i < 24; i++) {
        u8 gray = i * 10 + 8;
        set_palette_color(232 + i, gray, gray, gray);
    }
}

} // namespace bolt::drivers
