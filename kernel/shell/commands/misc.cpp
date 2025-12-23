/* ===========================================================================
 * BOLT OS - Miscellaneous Commands Implementation
 * =========================================================================== */

#include "misc.hpp"
#include "../../drivers/video/console.hpp"
#include "../../drivers/input/keyboard.hpp"
#include "../../lib/string.hpp"

namespace bolt::shell::cmd {

using namespace drivers;

void echo(const char* text) {
    Console::println(text);
}

void hexdump(const char* args) {
    // Parse address
    u32 addr = 0;
    u32 len = 64;  // Default length
    
    // Skip whitespace
    while (*args == ' ') args++;
    
    if (*args == '\0') {
        Console::set_color(Color::Red);
        Console::println("Usage: hexdump <address> [length]");
        Console::println("  address - Memory address in hex (e.g., 0x100000)");
        Console::println("  length  - Bytes to dump (default: 64)");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Parse hex address
    if (args[0] == '0' && args[1] == 'x') {
        args += 2;
    }
    
    while (*args && *args != ' ') {
        addr *= 16;
        char c = *args;
        if (c >= '0' && c <= '9') addr += c - '0';
        else if (c >= 'a' && c <= 'f') addr += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') addr += c - 'A' + 10;
        args++;
    }
    
    // Skip whitespace
    while (*args == ' ') args++;
    
    // Parse length if provided
    if (*args) {
        len = 0;
        while (*args >= '0' && *args <= '9') {
            len = len * 10 + (*args - '0');
            args++;
        }
    }
    
    // Limit dump size
    if (len > 256) len = 256;
    
    Console::set_color(Color::Yellow);
    Console::print("Memory dump at 0x");
    Console::print_hex(addr);
    Console::print(", ");
    Console::print_dec(static_cast<i32>(len));
    Console::println(" bytes:");
    
    Console::set_color(Color::LightCyan);
    
    u8* ptr = reinterpret_cast<u8*>(addr);
    for (u32 i = 0; i < len; i += 16) {
        Console::print_hex(addr + i);
        Console::print(": ");
        
        // Hex bytes
        for (u32 j = 0; j < 16 && i + j < len; j++) {
            if (ptr[i + j] < 0x10) Console::print("0");
            Console::print_hex(ptr[i + j]);
            Console::print(" ");
        }
        
        // ASCII
        Console::print(" |");
        for (u32 j = 0; j < 16 && i + j < len; j++) {
            char c = ptr[i + j];
            if (c >= 32 && c < 127) {
                char s[2] = {c, 0};
                Console::print(s);
            } else {
                Console::print(".");
            }
        }
        Console::println("|");
    }
    
    Console::set_color(Color::LightGray);
}

void gui() {
    Console::set_color(Color::Yellow);
    Console::println("Graphics mode active!");
    Console::set_color(Color::LightGray);
    if (Console::is_graphics_mode()) {
        Console::println("Running in VESA high-resolution mode.");
        Console::print("Resolution: ");
        Console::print_dec(static_cast<i32>(Framebuffer::width()));
        Console::print("x");
        Console::print_dec(static_cast<i32>(Framebuffer::height()));
        Console::print("x");
        Console::print_dec(static_cast<i32>(Framebuffer::bpp()));
        Console::println("");
    } else {
        Console::println("Fallback to VGA text mode.");
    }
}

} // namespace bolt::shell::cmd
