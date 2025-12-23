/* ===========================================================================
 * BOLT OS - Miscellaneous Commands
 * =========================================================================== */

#ifndef BOLT_SHELL_CMD_MISC_HPP
#define BOLT_SHELL_CMD_MISC_HPP

namespace bolt::shell::cmd {

// Echo text
void echo(const char* text);

// Hexdump memory
void hexdump(const char* args);

// Enter graphics mode
void gui();

} // namespace bolt::shell::cmd

#endif // BOLT_SHELL_CMD_MISC_HPP
