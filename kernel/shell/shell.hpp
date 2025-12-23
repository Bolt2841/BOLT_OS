#pragma once
/* ===========================================================================
 * BOLT OS - Shell Interface (Extended)
 * =========================================================================== */

#include "../lib/types.hpp"

namespace bolt::shell {

class Shell {
public:
    static constexpr usize MAX_CMD_LEN = 256;
    static constexpr usize MAX_ARGS = 16;
    static constexpr usize HISTORY_SIZE = 16;
    
    static void init();
    static void run();  // Main shell loop
    
    // Public path/cwd helpers for command modules
    static void resolve_path(const char* input, char* output);
    static void get_cwd(char* output);
    static void set_cwd(const char* path);
    
private:
    static void prompt();
    static void process_command(char* cmd);
    static void parse_args(char* cmd, char** argv, int& argc);
    
    // Input line editing
    static void clear_line();
    static void set_line(const char* text);
    static void history_up();
    static void history_down();
    static void add_to_history(const char* cmd);
    
    // Input state
    static char input_buffer[MAX_CMD_LEN];
    static usize input_pos;
    
    // Current working directory
    static char cwd[128];
    
    // Command history
    static char history[HISTORY_SIZE][MAX_CMD_LEN];
    static usize history_count;
    static usize history_index;
};

} // namespace bolt::shell
