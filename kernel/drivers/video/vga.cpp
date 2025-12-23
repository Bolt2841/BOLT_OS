/* ===========================================================================
 * BOLT OS - VGA Text Mode Driver Implementation
 * =========================================================================== */

#include "vga.hpp"
#include "../../core/sys/io.hpp"
#include "../../core/memory/heap.hpp"
#include "../../lib/string.hpp"

namespace bolt::drivers {

u16* VGA::buffer = reinterpret_cast<u16*>(BUFFER_ADDR);
u32 VGA::cursor_x = 0;
u32 VGA::cursor_y = CONTENT_START;
u8 VGA::color = 0x07;  // Light gray on black

void VGA::init() {
    cursor_x = 0;
    cursor_y = CONTENT_START;
    color = static_cast<u8>(Color::LightGray) | (static_cast<u8>(Color::Black) << 4);
    enable_cursor();
    
    // Clear entire screen with dark background
    fill_rect(0, 0, WIDTH, HEIGHT, ' ', Color::LightGray, Color::Black);
}

void VGA::clear() {
    // Only clear content area, preserve header and status
    fill_rect(0, CONTENT_START, WIDTH, CONTENT_HEIGHT, ' ', Color::LightGray, Color::Black);
    cursor_x = 0;
    cursor_y = CONTENT_START;
    update_cursor();
}

void VGA::set_color(Color fg, Color bg) {
    color = static_cast<u8>(fg) | (static_cast<u8>(bg) << 4);
}

void VGA::put_at(u32 x, u32 y, char c, Color fg, Color bg) {
    if (x >= WIDTH || y >= HEIGHT) return;
    u8 col = static_cast<u8>(fg) | (static_cast<u8>(bg) << 4);
    buffer[y * WIDTH + x] = make_entry(c, col);
}

void VGA::fill_rect(u32 x, u32 y, u32 w, u32 h, char c, Color fg, Color bg) {
    u8 col = static_cast<u8>(fg) | (static_cast<u8>(bg) << 4);
    for (u32 py = y; py < y + h && py < HEIGHT; py++) {
        for (u32 px = x; px < x + w && px < WIDTH; px++) {
            buffer[py * WIDTH + px] = make_entry(c, col);
        }
    }
}

void VGA::draw_hline(u32 x, u32 y, u32 len, Color fg, Color bg) {
    for (u32 i = 0; i < len && x + i < WIDTH; i++) {
        put_at(x + i, y, Box::H_LINE, fg, bg);
    }
}

void VGA::draw_box(u32 x, u32 y, u32 w, u32 h, Color fg, Color bg) {
    if (w < 2 || h < 2) return;
    
    // Corners
    put_at(x, y, Box::TOP_LEFT, fg, bg);
    put_at(x + w - 1, y, Box::TOP_RIGHT, fg, bg);
    put_at(x, y + h - 1, Box::BOT_LEFT, fg, bg);
    put_at(x + w - 1, y + h - 1, Box::BOT_RIGHT, fg, bg);
    
    // Horizontal lines
    for (u32 i = 1; i < w - 1; i++) {
        put_at(x + i, y, Box::H_LINE, fg, bg);
        put_at(x + i, y + h - 1, Box::H_LINE, fg, bg);
    }
    
    // Vertical lines
    for (u32 i = 1; i < h - 1; i++) {
        put_at(x, y + i, Box::V_LINE, fg, bg);
        put_at(x + w - 1, y + i, Box::V_LINE, fg, bg);
    }
}

void VGA::putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            buffer[cursor_y * WIDTH + cursor_x] = make_entry(' ', color);
        }
    } else {
        buffer[cursor_y * WIDTH + cursor_x] = make_entry(c, color);
        cursor_x++;
    }
    
    if (cursor_x >= WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    // Keep cursor in content area
    if (cursor_y >= HEIGHT - STATUS_HEIGHT) {
        scroll();
    }
    
    update_cursor();
}

void VGA::print(const char* str) {
    while (*str) {
        putchar(*str++);
    }
}

void VGA::println(const char* str) {
    print(str);
    putchar('\n');
}

void VGA::print_hex(u32 value) {
    char buf[16];
    str::utoa(value, buf, 16);
    print("0x");
    print(buf);
}

void VGA::print_dec(i32 value) {
    char buf[16];
    str::itoa(value, buf, 10);
    print(buf);
}

void VGA::scroll() {
    // Move content lines up by one (preserve header)
    for (u32 y = CONTENT_START; y < HEIGHT - STATUS_HEIGHT - 1; y++) {
        for (u32 x = 0; x < WIDTH; x++) {
            buffer[y * WIDTH + x] = buffer[(y + 1) * WIDTH + x];
        }
    }
    
    // Clear the last content line
    u8 blank_col = static_cast<u8>(Color::LightGray) | (static_cast<u8>(Color::Black) << 4);
    u16 blank = make_entry(' ', blank_col);
    for (u32 x = 0; x < WIDTH; x++) {
        buffer[(HEIGHT - STATUS_HEIGHT - 1) * WIDTH + x] = blank;
    }
    
    cursor_y = HEIGHT - STATUS_HEIGHT - 1;
}

void VGA::set_cursor(u32 x, u32 y) {
    cursor_x = x;
    cursor_y = y;
    update_cursor();
}

void VGA::enable_cursor() {
    io::outb(0x3D4, 0x0A);
    io::outb(0x3D5, (io::inb(0x3D5) & 0xC0) | 13);  // Start line
    io::outb(0x3D4, 0x0B);
    io::outb(0x3D5, (io::inb(0x3D5) & 0xE0) | 15);  // End line (block cursor)
}

void VGA::disable_cursor() {
    io::outb(0x3D4, 0x0A);
    io::outb(0x3D5, 0x20);
}

void VGA::update_cursor() {
    u16 pos = cursor_y * WIDTH + cursor_x;
    io::outb(0x3D4, 0x0F);
    io::outb(0x3D5, static_cast<u8>(pos & 0xFF));
    io::outb(0x3D4, 0x0E);
    io::outb(0x3D5, static_cast<u8>((pos >> 8) & 0xFF));
}

u16 VGA::make_entry(char c, u8 col) {
    return static_cast<u16>(c) | (static_cast<u16>(col) << 8);
}

void VGA::show_banner() {
    // Clean dark screen - no header, just content area
    fill_rect(0, 0, WIDTH, HEIGHT, ' ', Color::LightGray, Color::Black);
    
    // Show a subtle welcome message
    cursor_x = 0;
    cursor_y = 0;
    set_color(Color::LightGray, Color::Black);
    
    // Show welcome box
    set_color(Color::DarkGray, Color::Black);
    println("");
    print("  ");
    set_color(Color::LightGreen, Color::Black);
    print("BOLT OS");
    set_color(Color::DarkGray, Color::Black);
    print(" v0.3.0");
    println("");
    println("");
    set_color(Color::LightGray, Color::Black);
}

void VGA::update_status_bar() {
    // No status bar in clean mode
}

void VGA::show_welcome() {
    set_color(Color::LightGray, Color::Black);
}

} // namespace bolt::drivers
