/* ===========================================================================
 * BOLT OS - Shell Implementation (Extended)
 * =========================================================================== */

#include "shell.hpp"
#include "commands/filesystem.hpp"
#include "commands/system.hpp"
#include "commands/misc.hpp"
#include "commands/installer.hpp"
#include "../drivers/video/console.hpp"
#include "../drivers/video/framebuffer.hpp"
#include "../drivers/input/keyboard.hpp"
#include "../drivers/input/mouse.hpp"
#include "../drivers/serial/serial.hpp"
#include "../core/memory/heap.hpp"
#include "../lib/string.hpp"

namespace bolt::shell {

using namespace drivers;
namespace Box = drivers::Box;

// Static member definitions
char Shell::input_buffer[MAX_CMD_LEN];
usize Shell::input_pos = 0;
char Shell::cwd[128] = "/";
char Shell::history[HISTORY_SIZE][MAX_CMD_LEN];
usize Shell::history_count = 0;
usize Shell::history_index = 0;

void Shell::init() {
    DBG("SHELL", "Initializing shell...");
    input_pos = 0;
    mem::memset(input_buffer, 0, MAX_CMD_LEN);
    str::cpy(cwd, "/");
    history_count = 0;
    history_index = 0;
    for (usize i = 0; i < HISTORY_SIZE; i++) {
        mem::memset(history[i], 0, MAX_CMD_LEN);
    }
    DBG_OK("SHELL", "Shell ready");
}

void Shell::run() {
    // Show fancy boot splash
    Console::show_boot_splash();
    
    prompt();
    
    while (true) {
        // Poll mouse for data
        Mouse::poll();
        
        // Mouse cursor disabled for now - causes visual artifacts
        // TODO: Implement proper double-buffering for cursor
        
        // Check mouse scroll wheel
        i8 scroll = Mouse::get_scroll_delta();
        if (scroll < 0) {
            Console::scroll_up();  // Wheel up = view older content
        } else if (scroll > 0) {
            Console::scroll_down();  // Wheel down = view newer content
        }
        
        KeyEvent ev = Keyboard::poll_event();
        
        // No input yet - continue polling
        if (ev.ascii == 0 && ev.special == SpecialKey::None) {
            continue;
        }
        
        // Handle special keys
        if (ev.special != SpecialKey::None) {
            switch (ev.special) {
                case SpecialKey::Up:
                    history_up();
                    break;
                case SpecialKey::Down:
                    history_down();
                    break;
                case SpecialKey::PageUp:
                    Console::scroll_up();
                    break;
                case SpecialKey::PageDown:
                    Console::scroll_down();
                    break;
                default:
                    break;
            }
            continue;
        }
        
        char c = ev.ascii;
        if (c == 0) continue;
        
        // Auto-scroll to bottom when user starts typing
        if (Console::is_scrolled_back()) {
            Console::scroll_to_bottom();
        }
        
        // Handle Ctrl+C
        if (ev.ctrl && (c == 'c' || c == 'C')) {
            Console::print("^C");
            Console::putchar('\n');
            input_pos = 0;
            mem::memset(input_buffer, 0, MAX_CMD_LEN);
            prompt();
            continue;
        }
        
        // Handle Ctrl+L (clear screen)
        if (ev.ctrl && (c == 'l' || c == 'L')) {
            cmd::clear();
            prompt();
            Console::print(input_buffer);
            continue;
        }
        
        if (c == '\n') {
            Console::putchar('\n');
            input_buffer[input_pos] = '\0';
            
            if (input_pos > 0) {
                add_to_history(input_buffer);
                process_command(input_buffer);
            }
            
            input_pos = 0;
            mem::memset(input_buffer, 0, MAX_CMD_LEN);
            history_index = history_count;
            prompt();
        }
        else if (c == '\b') {
            if (input_pos > 0) {
                input_pos--;
                input_buffer[input_pos] = '\0';
                Console::putchar('\b');
            }
        }
        else if (c >= 32 && c < 127) {
            if (input_pos < MAX_CMD_LEN - 1) {
                input_buffer[input_pos++] = c;
                Console::putchar(c);
            }
        }
    }
}

void Shell::clear_line() {
    while (input_pos > 0) {
        Console::putchar('\b');
        input_pos--;
    }
    mem::memset(input_buffer, 0, MAX_CMD_LEN);
}

void Shell::set_line(const char* text) {
    clear_line();
    str::cpy(input_buffer, text);
    input_pos = str::len(text);
    Console::print(input_buffer);
}

void Shell::history_up() {
    if (history_count == 0) return;
    
    if (history_index > 0) {
        history_index--;
        set_line(history[history_index]);
    }
}

void Shell::history_down() {
    if (history_count == 0) return;
    
    if (history_index < history_count - 1) {
        history_index++;
        set_line(history[history_index]);
    } else if (history_index == history_count - 1) {
        history_index = history_count;
        clear_line();
    }
}

void Shell::add_to_history(const char* cmd) {
    if (str::len(cmd) == 0) return;
    if (history_count > 0 && str::cmp(history[history_count - 1], cmd) == 0) return;
    
    if (history_count < HISTORY_SIZE) {
        str::cpy(history[history_count], cmd);
        history_count++;
    } else {
        for (usize i = 0; i < HISTORY_SIZE - 1; i++) {
            str::cpy(history[i], history[i + 1]);
        }
        str::cpy(history[HISTORY_SIZE - 1], cmd);
    }
}

void Shell::prompt() {
    // Modern Linux-style prompt: user@bolt:path$

    Console::set_color(Color::LightGray, Color::Black);
    Console::putchar(' ');
    Console::set_color(Color::LightGreen, Color::Black);
    Console::print("user");
    Console::set_color(Color::White, Color::Black);
    Console::putchar('@');
    Console::set_color(Color::LightCyan, Color::Black);
    Console::print("bolt");
    Console::set_color(Color::White, Color::Black);
    Console::putchar(':');
    Console::set_color(Color::LightBlue, Color::Black);
    Console::print(cwd);
    Console::set_color(Color::White, Color::Black);
    Console::print("$ ");
    Console::set_color(Color::LightGray, Color::Black);
}

void Shell::process_command(char* cmdline) {
    char* argv[MAX_ARGS];
    int argc = 0;
    
    parse_args(cmdline, argv, argc);
    
    if (argc == 0) return;
    
    const char* cmd = argv[0];
    
    // Log command to serial for debugging with unified format
    Serial::log("SHELL", LogType::Debug, "Executing: ", cmdline);
    
    // System commands
    if (str::cmp(cmd, "help") == 0) {
        cmd::help();
    }
    else if (str::cmp(cmd, "clear") == 0 || str::cmp(cmd, "cls") == 0) {
        cmd::clear();
    }
    else if (str::cmp(cmd, "mem") == 0) {
        cmd::mem();
    }
    else if (str::cmp(cmd, "vmm") == 0 || str::cmp(cmd, "paging") == 0) {
        cmd::vmm_info();
    }
    else if (str::cmp(cmd, "ps") == 0 || str::cmp(cmd, "tasks") == 0) {
        cmd::ps();
    }
    else if (str::cmp(cmd, "sysinfo") == 0) {
        cmd::sysinfo();
    }
    else if (str::cmp(cmd, "uptime") == 0) {
        cmd::uptime();
    }
    else if (str::cmp(cmd, "date") == 0) {
        cmd::date();
    }
    else if (str::cmp(cmd, "ver") == 0 || str::cmp(cmd, "version") == 0) {
        cmd::ver();
    }
    else if (str::cmp(cmd, "reboot") == 0) {
        cmd::reboot();
    }
    // Installer command
    else if (str::cmp(cmd, "install") == 0) {
        cmd::install();
    }
    // Hardware detection commands
    else if (str::cmp(cmd, "hwinfo") == 0 || str::cmp(cmd, "hw") == 0) {
        cmd::hwinfo();
    }
    else if (str::cmp(cmd, "cpuinfo") == 0 || str::cmp(cmd, "cpu") == 0) {
        cmd::cpuinfo();
    }
    // Hardware commands
    else if (str::cmp(cmd, "lspci") == 0) {
        cmd::lspci();
    }
    else if (str::cmp(cmd, "lsdisk") == 0 || str::cmp(cmd, "disks") == 0) {
        cmd::lsdisk();
    }
    else if (str::cmp(cmd, "sector") == 0) {
        // Build args string
        char args[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(args, " ");
            str::cat(args, argv[i]);
        }
        cmd::read_sector(args);
    }
    else if (str::cmp(cmd, "mount") == 0) {
        char args[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(args, " ");
            str::cat(args, argv[i]);
        }
        cmd::mount(args);
    }
    else if (str::cmp(cmd, "fat32dir") == 0) {
        char args[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(args, " ");
            str::cat(args, argv[i]);
        }
        cmd::dir(args);
    }
    else if (str::cmp(cmd, "fat32type") == 0) {
        char args[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(args, " ");
            str::cat(args, argv[i]);
        }
        cmd::type(args);
    }
    // Filesystem commands (RAMFS)
    else if (str::cmp(cmd, "ls") == 0 || str::cmp(cmd, "dir") == 0) {
        cmd::ls(argc, argv);
    }
    else if (str::cmp(cmd, "cd") == 0) {
        cmd::cd(argc, argv);
    }
    else if (str::cmp(cmd, "pwd") == 0) {
        cmd::pwd();
    }
    else if (str::cmp(cmd, "cat") == 0 || str::cmp(cmd, "type") == 0) {
        cmd::cat(argc, argv);
    }
    else if (str::cmp(cmd, "write") == 0) {
        cmd::write(argc, argv);
    }
    else if (str::cmp(cmd, "touch") == 0) {
        cmd::touch(argc, argv);
    }
    else if (str::cmp(cmd, "rm") == 0 || str::cmp(cmd, "del") == 0) {
        cmd::rm(argc, argv);
    }
    else if (str::cmp(cmd, "mkdir") == 0) {
        cmd::mkdir_cmd(argc, argv);
    }
    // Advanced filesystem commands
    else if (str::cmp(cmd, "rmdir") == 0) {
        cmd::rmdir_cmd(argc, argv);
    }
    else if (str::cmp(cmd, "cp") == 0 || str::cmp(cmd, "copy") == 0) {
        cmd::cp(argc, argv);
    }
    else if (str::cmp(cmd, "mv") == 0 || str::cmp(cmd, "move") == 0 || str::cmp(cmd, "ren") == 0) {
        cmd::mv(argc, argv);
    }
    else if (str::cmp(cmd, "stat") == 0) {
        cmd::stat_cmd(argc, argv);
    }
    else if (str::cmp(cmd, "tree") == 0) {
        cmd::tree(argc, argv);
    }
    else if (str::cmp(cmd, "df") == 0) {
        cmd::df();
    }
    else if (str::cmp(cmd, "head") == 0) {
        cmd::head(argc, argv);
    }
    else if (str::cmp(cmd, "tail") == 0) {
        cmd::tail(argc, argv);
    }
    else if (str::cmp(cmd, "append") == 0) {
        cmd::append(argc, argv);
    }
    else if (str::cmp(cmd, "find") == 0) {
        cmd::find_cmd(argc, argv);
    }
    else if (str::cmp(cmd, "wc") == 0) {
        cmd::wc(argc, argv);
    }
    else if (str::cmp(cmd, "du") == 0) {
        cmd::du(argc, argv);
    }
    else if (str::cmp(cmd, "edit") == 0) {
        cmd::edit(argc, argv);
    }
    // Misc commands
    else if (str::cmp(cmd, "echo") == 0) {
        // Build echo string from remaining args
        char text[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(text, " ");
            str::cat(text, argv[i]);
        }
        cmd::echo(text);
    }
    else if (str::cmp(cmd, "hexdump") == 0) {
        // Build args string
        char args[256] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) str::cat(args, " ");
            str::cat(args, argv[i]);
        }
        cmd::hexdump(args);
    }
    else if (str::cmp(cmd, "gui") == 0) {
        cmd::gui();
    }
    else {
        Console::set_color(Color::LightRed);
        Console::print("Unknown command: ");
        Console::println(argv[0]);
        Console::set_color(Color::LightGray);
        Console::println("Type 'help' for available commands.");
    }
}

void Shell::parse_args(char* cmd, char** argv, int& argc) {
    argc = 0;
    char* p = cmd;
    
    while (*p && argc < static_cast<int>(MAX_ARGS)) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        
        argv[argc++] = p;
        
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
}

// Path/cwd public helpers
void Shell::resolve_path(const char* input, char* output) {
    // Handle "." - current directory
    if (str::cmp(input, ".") == 0 || input[0] == '\0') {
        str::cpy(output, cwd);
    }
    // If absolute path, use it directly
    else if (input[0] == '/') {
        str::cpy(output, input);
    } else {
        // Build path from cwd + input
        str::cpy(output, cwd);
        
        // Ensure cwd part ends with /
        usize len = str::len(output);
        if (len > 0 && output[len - 1] != '/') {
            str::cat(output, "/");
        }
        
        str::cat(output, input);
    }
    
    // Normalize: remove trailing slash (except for root)
    usize len = str::len(output);
    while (len > 1 && output[len - 1] == '/') {
        output[len - 1] = '\0';
        len--;
    }
}

void Shell::get_cwd(char* output) {
    str::cpy(output, cwd);
}

void Shell::set_cwd(const char* path) {
    str::cpy(cwd, path);
    // Normalize: remove trailing slash except for root
    usize len = str::len(cwd);
    while (len > 1 && cwd[len - 1] == '/') {
        cwd[len - 1] = '\0';
        len--;
    }
}

} // namespace bolt::shell
