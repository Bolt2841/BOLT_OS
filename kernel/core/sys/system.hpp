#pragma once
/* ===========================================================================
 * BOLT OS - System Hardware Detection
 * Runtime detection of all hardware - no hardcoded values
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::sys {

// Memory region types (from BIOS E820)
enum class MemoryType : u32 {
    Available = 1,
    Reserved = 2,
    ACPI_Reclaimable = 3,
    ACPI_NVS = 4,
    BadMemory = 5
};

// Memory map entry (matches BIOS E820 format)
struct MemoryRegion {
    u64 base;
    u64 length;
    MemoryType type;
    u32 acpi_extended;
};

// Video mode info
struct VideoInfo {
    u32 framebuffer;        // Physical address
    u32 width;
    u32 height;
    u32 pitch;              // Bytes per scanline
    u8 bpp;                 // Bits per pixel
    bool available;
    bool is_graphics;       // true = graphics, false = text mode
};

// CPU info
struct CPUInfo {
    char vendor[16];        // e.g., "GenuineIntel"
    char brand[64];         // e.g., "Intel Pentium III"
    u32 family;
    u32 model;
    u32 stepping;
    bool has_fpu;
    bool has_mmx;
    bool has_sse;
    bool has_sse2;
    bool has_pae;
    bool has_apic;
};

// Complete system information - detected at boot
struct SystemInfo {
    // Memory
    u64 total_memory;           // Total RAM in bytes
    u64 usable_memory;          // Usable RAM (excluding reserved)
    u32 memory_region_count;
    MemoryRegion memory_map[32];
    
    // Video
    VideoInfo video;
    
    // CPU
    CPUInfo cpu;
    
    // Boot info
    u32 boot_drive;
    u32 kernel_start;
    u32 kernel_end;
    u32 kernel_size;
};

class System {
public:
    // Initialize and detect all hardware
    static void init();
    
    // Get system info (read-only after init)
    static const SystemInfo& info() { return sys_info; }
    
    // Individual detection functions
    static void detect_memory();
    static void detect_cpu();
    static void detect_video();
    
    // Print system summary
    static void print_info();
    
    // Helper accessors
    static u64 total_memory() { return sys_info.total_memory; }
    static u64 usable_memory() { return sys_info.usable_memory; }
    static u32 screen_width() { return sys_info.video.width; }
    static u32 screen_height() { return sys_info.video.height; }
    static bool has_graphics() { return sys_info.video.is_graphics; }
    
private:
    static SystemInfo sys_info;
    
    // CPUID helper
    static void cpuid(u32 func, u32& eax, u32& ebx, u32& ecx, u32& edx);
    
    // Memory detection via BIOS (called from bootloader)
    static void parse_memory_map(void* map_ptr, u32 count);
};

// Global system info pointer for easy access
extern SystemInfo* g_system;

} // namespace bolt::sys
