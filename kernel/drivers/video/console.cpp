/* ===========================================================================
 * BOLT OS - Console Implementation with Scrollback Buffer
 * =========================================================================== */

#include "console.hpp"
#include "../../core/sys/config.hpp"

namespace bolt::drivers {

// Static member definitions
Color Console::current_fg = Color::LightGray;
Color Console::current_bg = Color::Black;

// Scrollback buffer
ConsoleChar Console::scrollback[SCROLLBACK_LINES][MAX_LINE_LENGTH];
u32 Console::scrollback_head = 0;
u32 Console::scrollback_count = 0;
u32 Console::view_offset = 0;
u32 Console::current_col = 0;

void Console::init() {
    // Initialize state variables first
    scrollback_head = 0;
    scrollback_count = 0;
    view_offset = 0;
    current_col = 0;
    
    // Clear scrollback buffer with simple byte clear
    for (u32 i = 0; i < SCROLLBACK_LINES; i++) {
        for (u32 j = 0; j < MAX_LINE_LENGTH; j++) {
            scrollback[i][j].c = ' ';
            scrollback[i][j].fg = static_cast<u8>(Color::LightGray);
            scrollback[i][j].bg = static_cast<u8>(Color::Black);
        }
    }
}

bool Console::is_graphics_mode() {
    return Framebuffer::is_available();
}

u32 Console::visible_rows() {
    if (Framebuffer::is_available()) {
        // Use framebuffer's char height constant instead of hardcoding
        return Framebuffer::height() / Framebuffer::char_height();
    }
    return config::VGA_TEXT_HEIGHT;  // Use config constant
}

Color32 Console::vga_to_color32(Color c) {
    switch (c) {
        case Color::Black:        return Color32::Background();  // Match screen bg!
        case Color::Blue:         return Color32(0, 100, 200);
        case Color::Green:        return Color32(0, 180, 80);
        case Color::Cyan:         return Color32(0, 200, 200);
        case Color::Red:          return Color32(200, 60, 60);
        case Color::Magenta:      return Color32(180, 80, 180);
        case Color::Brown:        return Color32(180, 120, 60);
        case Color::LightGray:    return Color32(180, 180, 180);
        case Color::DarkGray:     return Color32(100, 100, 100);
        case Color::LightBlue:    return Color32(100, 150, 255);
        case Color::LightGreen:   return Color32(100, 255, 100);
        case Color::LightCyan:    return Color32(100, 255, 255);
        case Color::LightRed:     return Color32(255, 100, 100);
        case Color::LightMagenta: return Color32(255, 100, 255);
        case Color::Yellow:       return Color32(255, 220, 100);
        case Color::White:        return Color32(255, 255, 255);
        default:                  return Color32(180, 180, 180);
    }
}

void Console::set_color(Color fg, Color bg) {
    current_fg = fg;
    current_bg = bg;
    
    if (Framebuffer::is_available()) {
        Framebuffer::set_text_color(vga_to_color32(fg), vga_to_color32(bg));
    } else {
        VGA::set_color(fg, bg);
    }
}

void Console::add_char_to_buffer(char c) {
    if (current_col < MAX_LINE_LENGTH) {
        scrollback[scrollback_head][current_col] = {
            c,
            static_cast<u8>(current_fg),
            static_cast<u8>(current_bg)
        };
        current_col++;
    }
}

void Console::new_line() {
    // Move to next line in buffer
    scrollback_head = (scrollback_head + 1) % SCROLLBACK_LINES;
    if (scrollback_count < SCROLLBACK_LINES) {
        scrollback_count++;
    }
    
    // Clear new line
    for (u32 i = 0; i < MAX_LINE_LENGTH; i++) {
        scrollback[scrollback_head][i] = {' ', static_cast<u8>(Color::LightGray), static_cast<u8>(Color::Black)};
    }
    current_col = 0;
}

void Console::putchar(char c) {
    if (c == '\n') {
        new_line();
        if (view_offset == 0) {
            if (Framebuffer::is_available()) {
                Framebuffer::print("\n");
            } else {
                VGA::putchar('\n');
            }
        }
    } else if (c == '\r') {
        current_col = 0;
        if (view_offset == 0) {
            if (Framebuffer::is_available()) {
                Framebuffer::set_cursor(0, Framebuffer::get_cursor_row());
            } else {
                VGA::putchar('\r');
            }
        }
    } else if (c == '\b') {
        if (current_col > 0) {
            current_col--;
            scrollback[scrollback_head][current_col] = {' ', static_cast<u8>(current_fg), static_cast<u8>(current_bg)};
        }
        if (view_offset == 0) {
            if (Framebuffer::is_available()) {
                char str[2] = {c, 0};
                Framebuffer::print(str);
            } else {
                VGA::putchar(c);
            }
        }
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++) {
            putchar(' ');
        }
    } else {
        add_char_to_buffer(c);
        
        // Handle line wrap
        u32 max_cols = Framebuffer::is_available() ? (Framebuffer::width() / 8) : 80;
        if (current_col >= max_cols) {
            new_line();
        }
        
        if (view_offset == 0) {
            if (Framebuffer::is_available()) {
                char str[2] = {c, 0};
                Framebuffer::print(str);
            } else {
                VGA::putchar(c);
            }
        }
    }
}

void Console::print(const char* str) {
    while (*str) {
        putchar(*str++);
    }
}

void Console::println(const char* str) {
    print(str);
    putchar('\n');
}

void Console::print_dec(i32 num) {
    if (num < 0) {
        putchar('-');
        num = -num;
    }
    if (num == 0) {
        putchar('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (num > 0) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    while (i > 0) {
        putchar(buf[--i]);
    }
}

void Console::print_hex(u32 num) {
    const char* hex = "0123456789ABCDEF";
    putchar('0');
    putchar('x');
    for (int i = 28; i >= 0; i -= 4) {
        putchar(hex[(num >> i) & 0xF]);
    }
}

void Console::clear() {
    // Reset state
    scrollback_head = 0;
    scrollback_count = 0;
    view_offset = 0;
    current_col = 0;
    
    // Clear scrollback buffer
    for (u32 i = 0; i < SCROLLBACK_LINES; i++) {
        for (u32 j = 0; j < MAX_LINE_LENGTH; j++) {
            scrollback[i][j].c = ' ';
            scrollback[i][j].fg = static_cast<u8>(Color::LightGray);
            scrollback[i][j].bg = static_cast<u8>(Color::Black);
        }
    }
    
    if (Framebuffer::is_available()) {
        Framebuffer::clear();
    } else {
        VGA::clear();
    }
}

void Console::redraw_screen() {
    if (!Framebuffer::is_available()) return;
    
    u32 rows = visible_rows();
    u32 cols = Framebuffer::width() / 8;
    
    u32 lines_available = scrollback_count;
    if (lines_available == 0) return;
    
    // Calculate which line in the buffer to start from
    i32 start_line = static_cast<i32>(scrollback_head) - static_cast<i32>(view_offset) - static_cast<i32>(rows) + 1;
    i32 oldest_line = static_cast<i32>(scrollback_head) - static_cast<i32>(scrollback_count) + 1;
    if (start_line < oldest_line) {
        start_line = oldest_line;
    }
    
    // Draw each row - always draw full row with background to avoid flicker
    for (u32 screen_row = 0; screen_row < rows; screen_row++) {
        i32 buffer_line = start_line + static_cast<i32>(screen_row);
        
        // Draw background for entire row first
        Framebuffer::fill_rect(0, screen_row * 16, Framebuffer::width(), 16, Color32::Background());
        
        if (buffer_line < oldest_line || buffer_line > static_cast<i32>(scrollback_head)) {
            continue;
        }
        
        // Wrap buffer index
        u32 buf_idx = (static_cast<u32>((buffer_line % SCROLLBACK_LINES) + SCROLLBACK_LINES)) % SCROLLBACK_LINES;
        
        for (u32 col = 0; col < cols && col < MAX_LINE_LENGTH; col++) {
            ConsoleChar& cc = scrollback[buf_idx][col];
            if (cc.c != ' ') {
                Color32 fg = vga_to_color32(static_cast<Color>(cc.fg));
                Framebuffer::draw_char(col * 8, screen_row * 16, cc.c, fg, Color32::Background());
            }
        }
    }
    
    // Show scroll indicator if scrolled back
    if (view_offset > 0) {
        const char* indicator = " [SCROLLED - PgDn to return] ";
        u32 ind_len = 0;
        for (const char* p = indicator; *p; p++) ind_len++;
        u32 x = Framebuffer::width() - (ind_len * 8);
        Framebuffer::draw_string(x, 0, indicator, Color32(255, 200, 0), Color32(60, 60, 60));
    }
    
    // Reset text colors
    Framebuffer::set_text_color(vga_to_color32(current_fg), vga_to_color32(current_bg));
}

void Console::scroll_up() {
    if (!Framebuffer::is_available()) return;
    
    u32 scroll_amount = 3;  // Scroll 3 lines at a time
    u32 max_offset = scrollback_count > visible_rows() ? scrollback_count - visible_rows() : 0;
    
    if (view_offset + scroll_amount > max_offset) {
        view_offset = max_offset;
    } else {
        view_offset += scroll_amount;
    }
    
    redraw_screen();
}

void Console::scroll_down() {
    if (!Framebuffer::is_available()) return;
    
    u32 scroll_amount = 3;  // Scroll 3 lines at a time
    
    if (view_offset <= scroll_amount) {
        view_offset = 0;
        // Redraw and restore normal cursor-based output
        redraw_screen();
        Framebuffer::set_cursor(current_col, visible_rows() - 1);
    } else {
        view_offset -= scroll_amount;
        redraw_screen();
    }
}

void Console::scroll_to_bottom() {
    if (view_offset != 0) {
        view_offset = 0;
        redraw_screen();
        Framebuffer::set_cursor(current_col, visible_rows() - 1);
    }
}

bool Console::is_scrolled_back() {
    return view_offset > 0;
}

void Console::show_boot_splash() {
    clear();
    
    if (Framebuffer::is_available()) {
        u32 w = Framebuffer::width();
        u32 h = Framebuffer::height();
        
        // ═══════════════════════════════════════════════════════════════
        // TOP STATUS BAR - gradient with system info
        // ═══════════════════════════════════════════════════════════════
        for (u32 y = 0; y < 24; y++) {
            u8 shade = 45 - y / 2;
            Framebuffer::draw_hline(0, y, w, Color32(shade, shade, shade + 5));
        }
        
        // Cyan accent line under status bar
        Framebuffer::draw_hline(0, 24, w, Color32(0, 180, 220));
        Framebuffer::draw_hline(0, 25, w, Color32(0, 100, 140));
        
        // Status bar text
        Framebuffer::set_cursor(1, 0);
        Framebuffer::set_text_color(Color32(0, 200, 255), Color32(45, 45, 50));
        Framebuffer::print(" BOLT OS v0.5 ");
        Framebuffer::set_text_color(Color32(120, 120, 120), Color32(45, 45, 50));
        Framebuffer::print("| ");
        Framebuffer::print_dec(static_cast<i32>(w));
        Framebuffer::print("x");
        Framebuffer::print_dec(static_cast<i32>(h));
        Framebuffer::print(" | x86 Protected Mode | PgUp/PgDn to scroll ");
        
        // ═══════════════════════════════════════════════════════════════
        // LOGO SECTION - clean block letters
        // ═══════════════════════════════════════════════════════════════
        Framebuffer::set_cursor(2, 2);
        Framebuffer::set_text_color(Color32(0, 220, 255), Color32::Background());
        Framebuffer::println("######   ######  ##    ########     ######   ######  ");
        Framebuffer::println("  ##   ##  ##  ##  ##       ##       ##    ## ##       ");
        Framebuffer::println("  ######   ##  ##  ##       ##       ##    ##  #####   ");
        Framebuffer::println("  ##   ##  ##  ##  ##       ##       ##    ##      ##  ");
        Framebuffer::println("  ######   ######  ######   ##        ######  ######   ");
        
        // Tagline
        Framebuffer::set_cursor(2, 8);
        Framebuffer::set_text_color(Color32(80, 80, 80), Color32::Background());
        Framebuffer::println("  --------------------------------------------------------");
        Framebuffer::set_text_color(Color32(150, 150, 150), Color32::Background());
        Framebuffer::println("    A Modern x86 Operating System | High Resolution Mode");
        Framebuffer::println("");
        
        // ═══════════════════════════════════════════════════════════════
        // WELCOME BOX
        // ═══════════════════════════════════════════════════════════════
        Framebuffer::set_text_color(Color32(60, 60, 60), Color32::Background());
        Framebuffer::println("  +----------------------------------------------------------+");
        Framebuffer::set_text_color(Color32(200, 200, 200), Color32::Background());
        Framebuffer::print("  |  ");
        Framebuffer::set_text_color(Color32(100, 255, 150), Color32::Background());
        Framebuffer::print("Welcome!");
        Framebuffer::set_text_color(Color32(200, 200, 200), Color32::Background());
        Framebuffer::println(" Type 'help' for commands, 'sysinfo' for system  |");
        Framebuffer::set_text_color(Color32(60, 60, 60), Color32::Background());
        Framebuffer::println("  +----------------------------------------------------------+");
        Framebuffer::println("");
        
        // Reset colors for shell
        Framebuffer::set_text_color(Color32::Text(), Color32::Background());
    } else {
        VGA::clear();
        VGA::show_banner();
    }
}

} // namespace bolt::drivers
