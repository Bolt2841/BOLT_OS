#pragma once
/* ===========================================================================
 * BOLT OS - System Commands
 * =========================================================================== */

namespace bolt::shell::cmd {

void help();
void clear();
void mem();
void vmm_info();
void ps();
void sysinfo();
void uptime();
void date();
void ver();
void reboot();

// Hardware detection commands
void hwinfo();      // Show all detected hardware
void cpuinfo();     // Detailed CPU info

// Hardware commands
void lspci();
void lsdisk();
void diskinfo(const char* args);
void read_sector(const char* args);

// FAT32 commands  
void mount(const char* args);
void dir(const char* args);
void type(const char* args);

} // namespace bolt::shell::cmd
