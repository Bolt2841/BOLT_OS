/* ===========================================================================
 * BOLT OS - System Commands Implementation
 * =========================================================================== */

#include "system.hpp"
#include "../../drivers/video/console.hpp"
#include "../../drivers/video/framebuffer.hpp"
#include "../../drivers/timer/pit.hpp"
#include "../../drivers/timer/rtc.hpp"
#include "../../drivers/bus/pci.hpp"
#include "../../drivers/storage/ata.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../core/memory/heap.hpp"
#include "../../core/memory/pmm.hpp"
#include "../../core/memory/vmm.hpp"
#include "../../core/sched/task.hpp"
#include "../../core/sys/io.hpp"
#include "../../core/sys/system.hpp"
#include "../../storage/vfs.hpp"
#include "../../lib/string.hpp"
#include "../../fs/fat32.hpp"

namespace bolt::shell::cmd {

using namespace drivers;
using namespace mem;
using namespace sched;

void help() {
    DBG("CMD", "help: Displaying command list");
    
    Console::set_color(Color::Yellow);
    Console::println("BOLT OS Commands:");
    Console::set_color(Color::LightCyan);
    Console::println("  help     - Show this help");
    Console::println("  clear    - Clear screen");
    Console::println("  mem      - Show heap memory info");
    Console::println("  vmm      - Show virtual memory / paging info");
    Console::println("  ps       - Show running processes");
    Console::println("  echo     - Print text");
    Console::println("  sysinfo  - System information");
    Console::println("  uptime   - Show system uptime");
    Console::println("  date     - Show current date/time");
    Console::println("  hexdump  - Dump memory (hexdump <addr> [len])");
    Console::println("  ver      - Show version");
    Console::println("  reboot   - Restart system");
    Console::set_color(Color::Yellow);
    Console::println("Hardware Detection:");
    Console::set_color(Color::LightCyan);
    Console::println("  hwinfo   - Show all detected hardware");
    Console::println("  cpuinfo  - Detailed CPU information");
    Console::println("  lspci    - List PCI devices");
    Console::println("  lsdisk   - List detected hard drives");
    Console::println("  sector   - Read disk sector (sector <disk> <lba>)");
    Console::println("  mount    - Show mounted filesystems");
    Console::set_color(Color::Yellow);
    Console::println("File System:");
    Console::set_color(Color::LightCyan);
    Console::println("  ls       - List directory contents");
    Console::println("  cd       - Change directory");
    Console::println("  pwd      - Print working directory");
    Console::println("  cat      - Display file contents");
    Console::println("  write    - Write text to file");
    Console::println("  touch    - Create empty file");
    Console::println("  rm       - Remove file");
    Console::println("  mkdir    - Create directory");
    Console::println("  rmdir    - Remove empty directory");
    Console::set_color(Color::Yellow);
    Console::println("Advanced File Commands:");
    Console::set_color(Color::LightCyan);
    Console::println("  cp       - Copy file");
    Console::println("  mv       - Move/rename file");
    Console::println("  stat     - File information");
    Console::println("  tree     - Directory tree view");
    Console::println("  df       - Disk free space");
    Console::println("  du       - Directory size usage");
    Console::println("  head     - First N lines (-n)");
    Console::println("  tail     - Last N lines (-n)");
    Console::println("  append   - Append text to file");
    Console::println("  find     - Search for files (*.ext)");
    Console::println("  wc       - Count lines/words/bytes");
    Console::println("  edit     - Simple text editor");
    Console::set_color(Color::Yellow);
    Console::println("Graphics:");
    Console::set_color(Color::LightCyan);
    Console::println("  gui      - Enter graphics mode");
    Console::set_color(Color::Yellow);
    Console::println("Installation:");
    Console::set_color(Color::LightCyan);
    Console::println("  install  - Install BOLT OS to hard disk");
    Console::set_color(Color::DarkGray);
    Console::println("Shortcuts: Up/Down = History, Ctrl+C = Cancel, Ctrl+L = Clear");
    Console::set_color(Color::LightGray);
}

void clear() {
    // Show the full boot splash with banner
    Console::show_boot_splash();
}

void mem() {
    Console::set_color(Color::LightCyan);
    
    Console::print("System RAM:  ");
    u32 sys_mb = static_cast<u32>(Heap::get_total_system_memory() / (1024 * 1024));
    Console::print_dec(static_cast<i32>(sys_mb));
    Console::println(" MB");
    
    Console::print("Heap Total:  ");
    u32 heap_mb = static_cast<u32>(Heap::get_total() / (1024 * 1024));
    u32 heap_kb = static_cast<u32>((Heap::get_total() % (1024 * 1024)) / 1024);
    Console::print_dec(static_cast<i32>(heap_mb));
    Console::print(" MB ");
    Console::print_dec(static_cast<i32>(heap_kb));
    Console::println(" KB");
    
    Console::print("Heap Used:   ");
    Console::print_dec(static_cast<i32>(Heap::get_used() / 1024));
    Console::println(" KB");
    
    Console::print("Heap Free:   ");
    u32 free_mb = static_cast<u32>(Heap::get_free() / (1024 * 1024));
    u32 free_kb = static_cast<u32>((Heap::get_free() % (1024 * 1024)) / 1024);
    Console::print_dec(static_cast<i32>(free_mb));
    Console::print(" MB ");
    Console::print_dec(static_cast<i32>(free_kb));
    Console::println(" KB");
    
    Console::set_color(Color::LightGray);
}

void vmm_info() {
    Console::set_color(Color::Yellow);
    Console::println("=== Virtual Memory Manager ===");
    Console::set_color(Color::LightCyan);
    
    // Paging status
    Console::print("Paging:       ");
    if (VMM::is_paging_enabled()) {
        Console::set_color(Color::LightGreen);
        Console::println("ENABLED");
    } else {
        Console::set_color(Color::LightRed);
        Console::println("DISABLED");
    }
    Console::set_color(Color::LightCyan);
    
    // Physical memory from PMM
    auto pmm_stats = PMM::get_stats();
    Console::print("Physical RAM: ");
    Console::print_dec(static_cast<i32>(pmm_stats.total_memory / (1024 * 1024)));
    Console::println(" MB");
    
    Console::print("Total Pages:  ");
    Console::print_dec(static_cast<i32>(pmm_stats.total_pages));
    Console::println("");
    
    Console::print("Used Pages:   ");
    Console::print_dec(static_cast<i32>(pmm_stats.used_pages));
    Console::print(" (");
    Console::print_dec(static_cast<i32>(pmm_stats.used_pages * 4));
    Console::println(" KB)");
    
    Console::print("Free Pages:   ");
    Console::set_color(Color::LightGreen);
    Console::print_dec(static_cast<i32>(pmm_stats.free_pages));
    Console::set_color(Color::LightCyan);
    Console::print(" (");
    Console::print_dec(static_cast<i32>(pmm_stats.free_pages * 4 / 1024));
    Console::println(" MB)");
    
    // VMM stats
    auto vmm_stats = VMM::get_stats();
    Console::print("Page Tables:  ");
    Console::print_dec(static_cast<i32>(vmm_stats.page_tables_allocated));
    Console::println(" allocated");
    
    Console::print("Pages Mapped: ");
    Console::print_dec(static_cast<i32>(vmm_stats.pages_mapped));
    Console::println("");
    
    Console::print("Page Faults:  ");
    if (vmm_stats.page_faults > 0) {
        Console::set_color(Color::LightRed);
    }
    Console::print_dec(static_cast<i32>(vmm_stats.page_faults));
    Console::println("");
    
    Console::set_color(Color::LightGray);
}

void ps() {
    Console::set_color(Color::Yellow);
    Console::println("=== Process List ===");
    Console::set_color(Color::LightCyan);
    
    // Header
    Console::println("  PID  PPID  STATE     PRI   CPU     NAME");
    Console::set_color(Color::LightGray);
    Console::println("  ---  ----  --------  ----  ------  ----------------");
    
    // Iterate through tasks
    for (u32 i = 0; i < MAX_TASKS; i++) {
        Task* task = TaskManager::get_task(i);
        if (!task) {
            // Check if PID i exists differently since get_task uses PID not index
            continue;
        }
        
        Console::print("  ");
        
        // PID
        if (task->pid < 10) Console::print(" ");
        Console::print_dec(static_cast<i32>(task->pid));
        Console::print("   ");
        
        // PPID
        if (task->ppid < 10) Console::print(" ");
        Console::print_dec(static_cast<i32>(task->ppid));
        Console::print("   ");
        
        // State
        switch (task->state) {
            case TaskState::Ready:
                Console::set_color(Color::LightGreen);
                Console::print("Ready   ");
                break;
            case TaskState::Running:
                Console::set_color(Color::LightCyan);
                Console::print("Running ");
                break;
            case TaskState::Blocked:
                Console::set_color(Color::Yellow);
                Console::print("Blocked ");
                break;
            case TaskState::Sleeping:
                Console::set_color(Color::DarkGray);
                Console::print("Sleeping");
                break;
            case TaskState::Zombie:
                Console::set_color(Color::LightRed);
                Console::print("Zombie  ");
                break;
            default:
                Console::print("Unknown ");
                break;
        }
        Console::set_color(Color::LightGray);
        Console::print("  ");
        
        // Priority
        switch (task->priority) {
            case Priority::Idle:     Console::print("Idle"); break;
            case Priority::Low:      Console::print("Low "); break;
            case Priority::Normal:   Console::print("Norm"); break;
            case Priority::High:     Console::print("High"); break;
            case Priority::Realtime: Console::print("RT  "); break;
        }
        Console::print("  ");
        
        // CPU time (in ticks)
        Console::print_dec(static_cast<i32>(task->total_time));
        Console::print("     ");
        
        // Name
        Console::println(task->name);
    }
    
    // Summary
    auto stats = TaskManager::get_stats();
    Console::println("");
    Console::set_color(Color::LightCyan);
    Console::print("Total: ");
    Console::print_dec(static_cast<i32>(stats.total_tasks));
    Console::print("  Running: ");
    Console::print_dec(static_cast<i32>(stats.running_tasks));
    Console::print("  Ready: ");
    Console::print_dec(static_cast<i32>(stats.ready_tasks));
    Console::print("  Ctx switches: ");
    Console::print_dec(static_cast<i32>(stats.context_switches));
    Console::println("");
    Console::set_color(Color::LightGray);
}

void sysinfo() {
    Console::set_color(Color::Yellow);
    Console::println("=== BOLT OS System Information ===");
    Console::set_color(Color::LightCyan);
    
    Console::println("OS:          BOLT OS v0.5");
    Console::println("Arch:        x86 (32-bit Protected Mode)");
    
    // Display mode
    Console::print("Display:     ");
    if (Console::is_graphics_mode()) {
        Console::set_color(Color::LightGreen);
        Console::print("VESA Graphics ");
        Console::print_dec(static_cast<i32>(Framebuffer::width()));
        Console::print("x");
        Console::print_dec(static_cast<i32>(Framebuffer::height()));
        Console::print("x");
        Console::print_dec(static_cast<i32>(Framebuffer::bpp()));
        Console::println("");
    } else {
        Console::println("VGA Text Mode 80x25");
    }
    Console::set_color(Color::LightCyan);
    
    Console::print("Memory:      ");
    u32 sys_mb = static_cast<u32>(Heap::get_total_system_memory() / (1024 * 1024));
    Console::print_dec(static_cast<i32>(sys_mb));
    Console::println(" MB detected");
    
    Console::print("Uptime:      ");
    u32 seconds = PIT::get_seconds();
    u32 hours = seconds / 3600;
    u32 minutes = (seconds % 3600) / 60;
    u32 secs = seconds % 60;
    Console::print_dec(static_cast<i32>(hours));
    Console::print("h ");
    Console::print_dec(static_cast<i32>(minutes));
    Console::print("m ");
    Console::print_dec(static_cast<i32>(secs));
    Console::println("s");
    
    Console::print("Date/Time:   ");
    RTC::print_datetime_to_console();
    Console::println();
    
    Console::set_color(Color::LightGray);
}

void uptime() {
    u32 seconds = PIT::get_seconds();
    u32 hours = seconds / 3600;
    u32 minutes = (seconds % 3600) / 60;
    u32 secs = seconds % 60;
    
    Console::set_color(Color::LightCyan);
    Console::print("Uptime: ");
    Console::print_dec(static_cast<i32>(hours));
    Console::print("h ");
    Console::print_dec(static_cast<i32>(minutes));
    Console::print("m ");
    Console::print_dec(static_cast<i32>(secs));
    Console::println("s");
    
    Console::print("Ticks:  ");
    Console::print_dec(static_cast<i32>(PIT::get_ticks()));
    Console::println();
    Console::set_color(Color::LightGray);
}

void date() {
    Console::set_color(Color::LightCyan);
    RTC::print_datetime_to_console();
    Console::println();
    Console::set_color(Color::LightGray);
}

void ver() {
    Console::set_color(Color::LightCyan);
    Console::println("BOLT OS v0.5");
    Console::set_color(Color::DarkGray);
    Console::println("A minimal x86 operating system with VESA graphics");
    Console::println("Features: GDT, IDT, Memory, Paging, VGA/VESA, Keyboard, RTC, FS");
    Console::println("Built with: i686-elf-g++, NASM");
    Console::set_color(Color::LightGray);
}

void reboot() {
    Console::set_color(Color::Yellow);
    Console::println("Rebooting...");
    Console::set_color(Color::LightGray);
    
    io::outb(0x64, 0xFE);
    
    while (true) {
        asm volatile("hlt");
    }
}

// =============================================================================
// Hardware Commands
// =============================================================================

void lspci() {
    Console::set_color(Color::Yellow);
    Console::println("=== PCI Devices ===");
    Console::set_color(Color::LightGray);
    
    u32 count = PCI::get_device_count();
    if (count == 0) {
        Console::set_color(Color::DarkGray);
        Console::println("No PCI devices detected");
        Console::set_color(Color::LightGray);
        return;
    }
    
    for (u32 i = 0; i < count; i++) {
        const PCIDevice* dev = PCI::get_device(i);
        if (!dev) continue;
        
        Console::set_color(Color::LightCyan);
        Console::print("[");
        Console::print_hex(dev->bus);
        Console::print(":");
        Console::print_hex(dev->slot);
        Console::print(".");
        Console::print_hex(dev->func);
        Console::print("] ");
        
        Console::set_color(Color::White);
        Console::print_hex(dev->vendor_id);
        Console::print(":");
        Console::print_hex(dev->device_id);
        Console::print("  ");
        
        Console::set_color(Color::LightGreen);
        Console::print(pci_class_name(dev->class_code));
        Console::print(" - ");
        Console::set_color(Color::LightGray);
        Console::println(pci_subclass_name(dev->class_code, dev->subclass));
    }
    
    Console::println("");
    Console::set_color(Color::DarkGray);
    Console::print("Total: ");
    Console::print_dec(static_cast<i32>(count));
    Console::println(" device(s)");
    Console::set_color(Color::LightGray);
}

void lsdisk() {
    DBG("CMD", "lsdisk: Listing drives");
    
    Console::set_color(Color::Yellow);
    Console::println("=== ATA/IDE Drives ===");
    Console::set_color(Color::LightGray);
    
    u8 count = ATA::get_drive_count();
    Serial::log("CMD", LogType::Debug, "  Drive count: ", "");
    Serial::write_dec(count);
    Serial::writeln("");
    
    if (count == 0) {
        Console::set_color(Color::DarkGray);
        Console::println("No drives detected");
        Console::set_color(Color::LightGray);
        return;
    }
    
    for (u8 i = 0; i < count; i++) {
        const ATADrive* drv = ATA::get_drive(i);
        if (!drv || !drv->present) continue;
        
        Serial::log("CMD", LogType::Debug, "  Drive: ", drv->model);
        
        Console::set_color(Color::LightCyan);
        Console::print("Drive ");
        Console::print_dec(static_cast<i32>(i));
        Console::print(": ");
        
        Console::set_color(Color::White);
        Console::print(drv->model);
        Console::print("  ");
        
        Console::set_color(Color::LightGreen);
        Console::print_dec(static_cast<i32>(drv->size_mb));
        Console::print(" MB");
        
        Console::set_color(Color::DarkGray);
        Console::print("  [");
        Console::print(drv->is_atapi ? "ATAPI" : "ATA");
        Console::print(drv->supports_lba48 ? " LBA48" : (drv->supports_lba ? " LBA" : " CHS"));
        Console::println("]");
    }
    
    Console::set_color(Color::LightGray);
    DBG_OK("CMD", "Drive list complete");
}

// Helper: parse a decimal number from string
static u32 parse_dec(const char* str) {
    u32 val = 0;
    while (*str >= '0' && *str <= '9') {
        val = val * 10 + (*str - '0');
        str++;
    }
    return val;
}

// Helper: skip whitespace
static const char* skip_space(const char* str) {
    while (*str == ' ' || *str == '\t') str++;
    return str;
}

void read_sector(const char* args) {
    if (!args || !*args) {
        Console::set_color(Color::LightRed);
        Console::println("Usage: sector <drive> <lba>");
        Console::set_color(Color::DarkGray);
        Console::println("  Example: sector 0 0   (read MBR from first drive)");
        Console::set_color(Color::LightGray);
        return;
    }
    
    args = skip_space(args);
    u8 drive = static_cast<u8>(parse_dec(args));
    
    // Skip to next arg
    while (*args && *args != ' ') args++;
    args = skip_space(args);
    
    if (!*args) {
        Console::set_color(Color::LightRed);
        Console::println("Missing LBA argument");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u32 lba = parse_dec(args);
    
    if (drive >= ATA::get_drive_count()) {
        Console::set_color(Color::LightRed);
        Console::print("Invalid drive: ");
        Console::print_dec(static_cast<i32>(drive));
        Console::println("");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Read sector
    u8 buffer[512];
    if (!ATA::read_sectors(drive, lba, 1, buffer)) {
        Console::set_color(Color::LightRed);
        Console::println("Failed to read sector");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Display hex dump
    Console::set_color(Color::Yellow);
    Console::print("Sector ");
    Console::print_dec(static_cast<i32>(lba));
    Console::print(" from drive ");
    Console::print_dec(static_cast<i32>(drive));
    Console::println(":");
    Console::set_color(Color::LightGray);
    
    for (int row = 0; row < 32; row++) {
        // Offset
        Console::set_color(Color::DarkGray);
        Console::print_hex(row * 16);
        Console::print(": ");
        
        // Hex bytes
        Console::set_color(Color::LightCyan);
        for (int col = 0; col < 16; col++) {
            u8 byte = buffer[row * 16 + col];
            const char hex[] = "0123456789ABCDEF";
            char h[3] = { hex[byte >> 4], hex[byte & 0xF], 0 };
            Console::print(h);
            Console::print(" ");
        }
        
        // ASCII
        Console::print(" ");
        Console::set_color(Color::LightGreen);
        for (int col = 0; col < 16; col++) {
            u8 byte = buffer[row * 16 + col];
            char c = (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
            char s[2] = { c, 0 };
            Console::print(s);
        }
        Console::println("");
    }
    
    Console::set_color(Color::LightGray);
}

void diskinfo(const char* args) {
    (void)args;  // For future use
    lsdisk();
}

// =============================================================================
// FAT32 Commands (using VFS)
// =============================================================================

using namespace storage;
using namespace drivers;

void dir(const char* args) {
    const char* path = (args && *args) ? args : "/";
    
    DBG("CMD", "fat32dir: Listing directory");
    Serial::log("CMD", LogType::Debug, "  Path: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not initialized");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open directory
    u32 fd = 0;
    VFSResult result = VFS::opendir(path, fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "opendir failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::print("Cannot open directory: ");
        Console::println(vfs_result_string(result));
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG_OK("CMD", "Directory opened");
    
    Console::set_color(Color::Yellow);
    Console::print("Directory of ");
    Console::println(path);
    Console::set_color(Color::LightGray);
    Console::println("");
    
    // Read directory entries
    FileInfo info;
    u32 file_count = 0;
    u32 dir_count = 0;
    u64 total_size = 0;
    
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        if (info.is_directory()) {
            Console::set_color(Color::LightCyan);
            Console::print("<DIR>   ");
            dir_count++;
        } else {
            Console::set_color(Color::LightGray);
            // Print size with padding
            u32 size = static_cast<u32>(info.size);
            if (size < 10) Console::print("      ");
            else if (size < 100) Console::print("     ");
            else if (size < 1000) Console::print("    ");
            else if (size < 10000) Console::print("   ");
            else if (size < 100000) Console::print("  ");
            else if (size < 1000000) Console::print(" ");
            Console::print_dec(static_cast<i32>(size));
            Console::print(" ");
            file_count++;
            total_size += info.size;
        }
        
        Console::set_color(Color::White);
        Console::println(info.name);
    }
    
    VFS::closedir(fd);
    
    Console::println("");
    Console::set_color(Color::DarkGray);
    Console::print_dec(static_cast<i32>(file_count));
    Console::print(" file(s), ");
    Console::print_dec(static_cast<i32>(dir_count));
    Console::println(" dir(s)");
    Console::set_color(Color::LightGray);
    
    Serial::log("CMD", LogType::Success, "Listed ", "directory");
}

void type(const char* args) {
    if (!args || !*args) {
        Console::set_color(Color::LightRed);
        Console::println("Usage: fat32type <filename>");
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG("CMD", "fat32type: Reading file");
    
    args = skip_space(args);
    Serial::log("CMD", LogType::Debug, "  File: ", args);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not initialized");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Build full path if not absolute
    char filepath[256];
    if (args[0] == '/') {
        str::cpy(filepath, args);
    } else {
        str::cpy(filepath, "/");
        str::cat(filepath, args);
    }
    
    Serial::log("CMD", LogType::Debug, "  Full path: ", filepath);
    
    // Get file info
    FileInfo info;
    VFSResult result = VFS::stat(filepath, info);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "stat failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::print("File not found: ");
        Console::println(args);
        Console::set_color(Color::LightGray);
        return;
    }
    
    if (info.is_directory()) {
        DBG_WARN("CMD", "Cannot type a directory");
        Console::set_color(Color::LightRed);
        Console::println("Cannot type a directory");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open file for reading
    u32 fd = 0;
    result = VFS::open(filepath, FileMode::Read, fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "open failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to open file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG_OK("CMD", "File opened");
    
    // Read and display file (max 4KB for safety)
    static u8 file_buffer[4096];
    u64 bytes_read = 0;
    u64 max_size = info.size < 4096 ? info.size : 4096;
    
    result = VFS::read(fd, file_buffer, max_size, bytes_read);
    VFS::close(fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "read failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to read file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    Serial::log("CMD", LogType::Debug, "  Read bytes: ", "");
    Serial::write_dec(static_cast<u32>(bytes_read));
    Serial::writeln("");
    
    // Display contents
    Console::set_color(Color::LightGray);
    for (u64 i = 0; i < bytes_read; i++) {
        char c = static_cast<char>(file_buffer[i]);
        if (c == '\r') continue;  // Skip CR
        if (c == '\n' || (c >= 32 && c < 127)) {
            char s[2] = { c, 0 };
            Console::print(s);
        }
    }
    
    if (info.size > 4096) {
        Console::println("");
        Console::set_color(Color::Yellow);
        Console::println("... file truncated (>4KB)");
    }
    Console::set_color(Color::LightGray);
    
    DBG_OK("CMD", "File displayed");
}

void mount(const char* /* args */) {
    DBG("CMD", "mount: Mounting filesystem");
    
    // With VFS, mounts are done automatically at boot
    // This command now shows mount info
    
    Console::set_color(Color::Yellow);
    Console::println("VFS Mount Points:");
    Console::set_color(Color::LightGray);
    Console::println("");
    
    VFS::print_mounts();
    
    if (VFS::get_mount_count() == 0) {
        Console::set_color(Color::DarkGray);
        Console::println("(No filesystems mounted)");
    }
    
    Console::set_color(Color::LightGray);
}

// =============================================================================
// Hardware Detection Commands
// =============================================================================

void hwinfo() {
    const auto& info = sys::System::info();
    
    Console::set_color(Color::Yellow);
    Console::println("=== Hardware Detection (Runtime) ===");
    Console::println("");
    
    // CPU
    Console::set_color(Color::Yellow);
    Console::println("CPU:");
    Console::set_color(Color::LightCyan);
    Console::print("  Vendor:   ");
    Console::set_color(Color::White);
    Console::println(info.cpu.vendor);
    
    Console::set_color(Color::LightCyan);
    Console::print("  Brand:    ");
    Console::set_color(Color::White);
    const char* brand = info.cpu.brand;
    while (*brand == ' ') brand++;  // Trim leading spaces
    Console::println(brand[0] ? brand : "(unknown)");
    
    Console::set_color(Color::LightCyan);
    Console::print("  ID:       Family ");
    Console::print_dec(static_cast<i32>(info.cpu.family));
    Console::print(", Model ");
    Console::print_dec(static_cast<i32>(info.cpu.model));
    Console::print(", Stepping ");
    Console::print_dec(static_cast<i32>(info.cpu.stepping));
    Console::println("");
    
    Console::print("  Features: ");
    Console::set_color(Color::LightGreen);
    if (info.cpu.has_fpu) Console::print("FPU ");
    if (info.cpu.has_mmx) Console::print("MMX ");
    if (info.cpu.has_sse) Console::print("SSE ");
    if (info.cpu.has_sse2) Console::print("SSE2 ");
    if (info.cpu.has_pae) Console::print("PAE ");
    if (info.cpu.has_apic) Console::print("APIC ");
    Console::println("");
    Console::println("");
    
    // Memory
    Console::set_color(Color::Yellow);
    Console::println("Memory:");
    Console::set_color(Color::LightCyan);
    Console::print("  Total:    ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(info.total_memory / (1024 * 1024)));
    Console::println(" MB");
    
    Console::set_color(Color::LightCyan);
    Console::print("  Usable:   ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(info.usable_memory / (1024 * 1024)));
    Console::println(" MB");
    Console::println("");
    
    // Video
    Console::set_color(Color::Yellow);
    Console::println("Video:");
    Console::set_color(Color::LightCyan);
    if (info.video.is_graphics) {
        Console::print("  Mode:     ");
        Console::set_color(Color::White);
        Console::print_dec(static_cast<i32>(info.video.width));
        Console::print("x");
        Console::print_dec(static_cast<i32>(info.video.height));
        Console::print("x");
        Console::print_dec(static_cast<i32>(info.video.bpp));
        Console::println(" (VESA)");
        
        Console::set_color(Color::LightCyan);
        Console::print("  Buffer:   ");
        Console::set_color(Color::White);
        Console::print("0x");
        Console::print_hex(info.video.framebuffer);
        Console::println("");
        
        Console::set_color(Color::LightCyan);
        Console::print("  Pitch:    ");
        Console::set_color(Color::White);
        Console::print_dec(static_cast<i32>(info.video.pitch));
        Console::println(" bytes/line");
    } else {
        Console::print("  Mode:     ");
        Console::set_color(Color::White);
        Console::println("VGA Text 80x25");
    }
    Console::println("");
    
    // PCI Summary
    Console::set_color(Color::Yellow);
    Console::println("PCI Bus:");
    Console::set_color(Color::LightCyan);
    Console::print("  Devices:  ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(PCI::get_device_count()));
    Console::println(" detected");
    Console::println("");
    
    // Storage
    Console::set_color(Color::Yellow);
    Console::println("Storage:");
    Console::set_color(Color::LightCyan);
    Console::print("  ATA/IDE:  ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(ATA::get_drive_count()));
    Console::println(" drive(s)");
    
    for (u8 i = 0; i < ATA::get_drive_count(); i++) {
        const auto* drv = ATA::get_drive(i);
        if (drv && drv->present) {
            Console::set_color(Color::DarkGray);
            Console::print("    ");
            Console::print_dec(static_cast<i32>(i));
            Console::print(": ");
            Console::set_color(Color::LightGray);
            Console::print(drv->model);
            Console::print(" (");
            Console::print_dec(static_cast<i32>(drv->size_mb));
            Console::println(" MB)");
        }
    }
    
    Console::set_color(Color::LightGray);
}

void cpuinfo() {
    const auto& cpu = sys::System::info().cpu;
    
    Console::set_color(Color::Yellow);
    Console::println("=== CPU Information ===");
    Console::set_color(Color::LightGray);
    Console::println("");
    
    Console::set_color(Color::LightCyan);
    Console::print("Vendor ID:    ");
    Console::set_color(Color::White);
    Console::println(cpu.vendor);
    
    Console::set_color(Color::LightCyan);
    Console::print("Brand String: ");
    Console::set_color(Color::White);
    const char* brand = cpu.brand;
    while (*brand == ' ') brand++;
    Console::println(brand[0] ? brand : "(not available)");
    Console::println("");
    
    Console::set_color(Color::LightCyan);
    Console::print("Family:       ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(cpu.family));
    Console::println("");
    
    Console::set_color(Color::LightCyan);
    Console::print("Model:        ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(cpu.model));
    Console::println("");
    
    Console::set_color(Color::LightCyan);
    Console::print("Stepping:     ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(cpu.stepping));
    Console::println("");
    Console::println("");
    
    Console::set_color(Color::Yellow);
    Console::println("Features:");
    Console::set_color(Color::LightGray);
    
    Console::print("  FPU  (x87):     ");
    Console::println(cpu.has_fpu ? "Yes" : "No");
    
    Console::print("  MMX:            ");
    Console::println(cpu.has_mmx ? "Yes" : "No");
    
    Console::print("  SSE:            ");
    Console::println(cpu.has_sse ? "Yes" : "No");
    
    Console::print("  SSE2:           ");
    Console::println(cpu.has_sse2 ? "Yes" : "No");
    
    Console::print("  PAE  (36-bit):  ");
    Console::println(cpu.has_pae ? "Yes" : "No");
    
    Console::print("  APIC:           ");
    Console::println(cpu.has_apic ? "Yes" : "No");
    
    Console::set_color(Color::LightGray);
}

} // namespace bolt::shell::cmd
