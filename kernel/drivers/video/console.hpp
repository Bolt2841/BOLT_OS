#pragma once
/* ===========================================================================
 * BOLT OS - Console Abstraction
 * Unified text output for VGA text mode and Framebuffer graphics
 * With scrollback buffer support
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "vga.hpp"
#include "framebuffer.hpp"

namespace bolt::drivers {

// Scrollback buffer configuration
static constexpr u32 SCROLLBACK_LINES = 100;    // Lines to keep in history
static constexpr u32 MAX_LINE_LENGTH = 128;      // Max chars per line

// Store character with color info
struct ConsoleChar {
    char c;
    u8 fg;  // Color enum value
    u8 bg;
};

class Console {
public:
    static void init();
    
    // Basic output
    static void putchar(char c);
    static void print(const char* str);
    static void println(const char* str = "");
    static void print_dec(i32 num);
    static void print_hex(u32 num);
    
    // Colors - uses VGA Color enum, maps to Color32 for framebuffer
    static void set_color(Color fg, Color bg = Color::Black);
    
    // Screen operations
    static void clear();
    static void show_boot_splash();
    
    // Scrollback support
    static void scroll_up();        // Page Up - view older content
    static void scroll_down();      // Page Down - view newer content
    static void scroll_to_bottom(); // Jump to current output
    static bool is_scrolled_back(); // Are we viewing history?
    
    // Check which mode we're in
    static bool is_graphics_mode();
    
private:
    static Color32 vga_to_color32(Color c);
    static Color current_fg;
    static Color current_bg;
    
    // Scrollback buffer
    static ConsoleChar scrollback[SCROLLBACK_LINES][MAX_LINE_LENGTH];
    static u32 scrollback_head;      // Next line to write
    static u32 scrollback_count;     // Total lines in buffer
    static u32 view_offset;          // How many lines scrolled back (0 = bottom)
    static u32 current_col;          // Current column in current line
    
    static void add_char_to_buffer(char c);
    static void new_line();
    static void redraw_screen();
    static u32 visible_rows();
};

} // namespace bolt::drivers
