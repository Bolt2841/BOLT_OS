#pragma once
/* ===========================================================================
 * BOLT OS - Filesystem Commands
 * =========================================================================== */

namespace bolt::shell::cmd {

// Basic filesystem operations
void ls(int argc, char** argv);
void cd(int argc, char** argv);
void pwd();
void cat(int argc, char** argv);
void write(int argc, char** argv);
void touch(int argc, char** argv);
void rm(int argc, char** argv);
void mkdir_cmd(int argc, char** argv);

// Advanced filesystem operations
void rmdir_cmd(int argc, char** argv);  // Remove directory
void cp(int argc, char** argv);          // Copy file
void mv(int argc, char** argv);          // Move/rename file
void stat_cmd(int argc, char** argv);    // File statistics
void tree(int argc, char** argv);        // Directory tree view
void df();                               // Disk free space
void head(int argc, char** argv);        // First N lines
void tail(int argc, char** argv);        // Last N lines
void append(int argc, char** argv);      // Append to file
void find_cmd(int argc, char** argv);    // Search for files
void wc(int argc, char** argv);          // Word/line/byte count
void du(int argc, char** argv);          // Directory usage
void edit(int argc, char** argv);        // Simple text editor

} // namespace bolt::shell::cmd
