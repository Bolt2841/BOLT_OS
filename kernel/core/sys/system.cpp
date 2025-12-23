/* ===========================================================================
 * BOLT OS - System Hardware Detection Implementation
 * =========================================================================== */

#include "system.hpp"
#include "../../drivers/video/console.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../lib/string.hpp"

namespace bolt::sys {

using namespace drivers;

// Static storage
SystemInfo System::sys_info;

// Global pointer (initialized after class)
SystemInfo* g_system = nullptr;

// CPUID instruction wrapper
void System::cpuid(u32 func, u32& eax, u32& ebx, u32& ecx, u32& edx) {
    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(func), "c"(0));
}

void System::init() {
    // Set global pointer
    g_system = &sys_info;
    
    DBG("SYS", "Starting hardware detection...");
    
    // Zero out system info
    for (usize i = 0; i < sizeof(SystemInfo); i++) {
        reinterpret_cast<u8*>(&sys_info)[i] = 0;
    }
    
    // Detect all hardware
    detect_cpu();
    detect_memory();
    // Video is detected by framebuffer driver and updated here
    
    DBG_OK("SYS", "Hardware detection complete");
}

void System::detect_cpu() {
    DBG("SYS", "Detecting CPU via CPUID...");
    
    u32 eax, ebx, ecx, edx;
    
    // Get vendor string
    cpuid(0, eax, ebx, ecx, edx);
    
    // Copy vendor string (avoiding aliasing issues)
    sys_info.cpu.vendor[0] = ebx & 0xFF;
    sys_info.cpu.vendor[1] = (ebx >> 8) & 0xFF;
    sys_info.cpu.vendor[2] = (ebx >> 16) & 0xFF;
    sys_info.cpu.vendor[3] = (ebx >> 24) & 0xFF;
    sys_info.cpu.vendor[4] = edx & 0xFF;
    sys_info.cpu.vendor[5] = (edx >> 8) & 0xFF;
    sys_info.cpu.vendor[6] = (edx >> 16) & 0xFF;
    sys_info.cpu.vendor[7] = (edx >> 24) & 0xFF;
    sys_info.cpu.vendor[8] = ecx & 0xFF;
    sys_info.cpu.vendor[9] = (ecx >> 8) & 0xFF;
    sys_info.cpu.vendor[10] = (ecx >> 16) & 0xFF;
    sys_info.cpu.vendor[11] = (ecx >> 24) & 0xFF;
    sys_info.cpu.vendor[12] = '\0';
    
    // Get CPU features
    cpuid(1, eax, ebx, ecx, edx);
    sys_info.cpu.stepping = eax & 0xF;
    sys_info.cpu.model = (eax >> 4) & 0xF;
    sys_info.cpu.family = (eax >> 8) & 0xF;
    
    // Extended model/family for newer CPUs
    if (sys_info.cpu.family == 0xF) {
        sys_info.cpu.family += (eax >> 20) & 0xFF;
    }
    if (sys_info.cpu.family >= 6) {
        sys_info.cpu.model += ((eax >> 16) & 0xF) << 4;
    }
    
    // Feature flags (EDX)
    sys_info.cpu.has_fpu = (edx & (1 << 0)) != 0;
    sys_info.cpu.has_apic = (edx & (1 << 9)) != 0;
    sys_info.cpu.has_mmx = (edx & (1 << 23)) != 0;
    sys_info.cpu.has_sse = (edx & (1 << 25)) != 0;
    sys_info.cpu.has_sse2 = (edx & (1 << 26)) != 0;
    sys_info.cpu.has_pae = (edx & (1 << 6)) != 0;
    
    // Try to get brand string (extended CPUID)
    cpuid(0x80000000, eax, ebx, ecx, edx);
    if (eax >= 0x80000004) {
        u32* brand = reinterpret_cast<u32*>(sys_info.cpu.brand);
        cpuid(0x80000002, brand[0], brand[1], brand[2], brand[3]);
        cpuid(0x80000003, brand[4], brand[5], brand[6], brand[7]);
        cpuid(0x80000004, brand[8], brand[9], brand[10], brand[11]);
        sys_info.cpu.brand[48] = '\0';
    } else {
        str::cpy(sys_info.cpu.brand, sys_info.cpu.vendor);
    }
    
    // Log CPU info to serial
    Serial::write("[SYS] CPU: ");
    Serial::write(sys_info.cpu.vendor);
    Serial::write(" Family=");
    Serial::write_dec(sys_info.cpu.family);
    Serial::write(" Model=");
    Serial::write_dec(sys_info.cpu.model);
    Serial::writeln("");
}

void System::detect_memory() {
    DBG("SYS", "Detecting system memory...");
    
    // Memory is detected via BIOS E820 in bootloader
    // For now, we'll probe common memory regions
    
    // Default: assume 128MB if no memory map provided
    // The bootloader should pass this info, but we need a fallback
    
    // Probe memory by checking if addresses are readable
    // Start from 1MB (after conventional memory)
    u64 detected = 0;
    
    // Check memory in 1MB chunks up to 256MB (safe for most systems)
    // We'll refine this with actual E820 data from bootloader
    volatile u32* probe;
    
    for (u32 mb = 1; mb <= 256; mb++) {
        probe = reinterpret_cast<volatile u32*>(mb * 1024 * 1024);
        
        // Try to read/write - if it fails, we've hit unmapped memory
        u32 old = *probe;
        *probe = 0xDEADBEEF;
        
        if (*probe == 0xDEADBEEF) {
            *probe = old;  // Restore
            detected = (mb + 1) * 1024 * 1024;
        } else {
            break;
        }
    }
    
    // If detection failed, use conservative default
    if (detected == 0) {
        detected = 128 * 1024 * 1024;  // 128MB default
        DBG_WARN("SYS", "Memory probe failed, using 128MB fallback");
    }
    
    sys_info.total_memory = detected;
    sys_info.usable_memory = detected - (4 * 1024 * 1024);  // Reserve 4MB for kernel
    
    // Log memory detection
    Serial::write("[SYS] Memory: ");
    Serial::write_dec(static_cast<i32>(detected / (1024 * 1024)));
    Serial::writeln(" MB detected");
    
    // Create a simple memory map
    sys_info.memory_region_count = 3;
    
    // Region 0: Low memory (0-1MB) - Reserved for BIOS/Video
    sys_info.memory_map[0].base = 0;
    sys_info.memory_map[0].length = 1024 * 1024;
    sys_info.memory_map[0].type = MemoryType::Reserved;
    
    // Region 1: Kernel space (1MB-4MB) - Reserved
    sys_info.memory_map[1].base = 1024 * 1024;
    sys_info.memory_map[1].length = 3 * 1024 * 1024;
    sys_info.memory_map[1].type = MemoryType::Reserved;
    
    // Region 2: Usable memory (4MB+)
    sys_info.memory_map[2].base = 4 * 1024 * 1024;
    sys_info.memory_map[2].length = sys_info.usable_memory;
    sys_info.memory_map[2].type = MemoryType::Available;
}

void System::detect_video() {
    // This is called by framebuffer driver after it initializes
    // The framebuffer driver will update sys_info.video
}

void System::print_info() {
    using namespace drivers;
    
    Console::set_color(Color::Yellow);
    Console::println("=== System Information (Runtime Detected) ===");
    Console::set_color(Color::LightCyan);
    
    // CPU
    Console::print("CPU:        ");
    Console::set_color(Color::White);
    // Trim leading spaces from brand
    const char* brand = sys_info.cpu.brand;
    while (*brand == ' ') brand++;
    Console::println(brand[0] ? brand : sys_info.cpu.vendor);
    Console::set_color(Color::LightCyan);
    
    Console::print("            Family ");
    Console::print_dec(static_cast<i32>(sys_info.cpu.family));
    Console::print(", Model ");
    Console::print_dec(static_cast<i32>(sys_info.cpu.model));
    Console::print(", Stepping ");
    Console::print_dec(static_cast<i32>(sys_info.cpu.stepping));
    Console::println("");
    
    Console::print("Features:   ");
    Console::set_color(Color::LightGreen);
    if (sys_info.cpu.has_fpu) Console::print("FPU ");
    if (sys_info.cpu.has_mmx) Console::print("MMX ");
    if (sys_info.cpu.has_sse) Console::print("SSE ");
    if (sys_info.cpu.has_sse2) Console::print("SSE2 ");
    if (sys_info.cpu.has_pae) Console::print("PAE ");
    if (sys_info.cpu.has_apic) Console::print("APIC ");
    Console::println("");
    Console::set_color(Color::LightCyan);
    
    // Memory
    Console::print("Memory:     ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(sys_info.total_memory / (1024 * 1024)));
    Console::print(" MB total, ");
    Console::print_dec(static_cast<i32>(sys_info.usable_memory / (1024 * 1024)));
    Console::println(" MB usable");
    Console::set_color(Color::LightCyan);
    
    // Video
    Console::print("Video:      ");
    Console::set_color(Color::White);
    if (sys_info.video.is_graphics) {
        Console::print_dec(static_cast<i32>(sys_info.video.width));
        Console::print("x");
        Console::print_dec(static_cast<i32>(sys_info.video.height));
        Console::print("x");
        Console::print_dec(static_cast<i32>(sys_info.video.bpp));
        Console::print(" @ 0x");
        Console::print_hex(sys_info.video.framebuffer);
        Console::println("");
    } else {
        Console::println("VGA Text Mode 80x25");
    }
    
    Console::set_color(Color::LightGray);
}

} // namespace bolt::sys
