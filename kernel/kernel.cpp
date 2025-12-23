/* ===========================================================================
 * BOLT OS - C++ Kernel Main Entry
 * ===========================================================================
 * This is where all the C++ magic begins!
 * =========================================================================== */

#include "core/memory/heap.hpp"
#include "core/memory/pmm.hpp"
#include "core/memory/vmm.hpp"
#include "core/arch/gdt.hpp"
#include "core/arch/idt.hpp"
#include "core/sched/task.hpp"
#include "core/sys/events.hpp"
#include "core/sys/log.hpp"
#include "core/sys/panic.hpp"
#include "core/sys/system.hpp"
#include "drivers/video/vga.hpp"
#include "drivers/video/framebuffer.hpp"
#include "drivers/input/keyboard.hpp"
#include "drivers/input/mouse.hpp"
#include "drivers/timer/pit.hpp"
#include "drivers/timer/rtc.hpp"
#include "drivers/serial/serial.hpp"
#include "drivers/video/graphics.hpp"
#include "drivers/bus/pci.hpp"
#include "drivers/storage/ata.hpp"
#include "storage/storage.hpp"
#include "fs/ramfs.hpp"
#include "fs/fat32.hpp"
#include "shell/shell.hpp"

using namespace bolt;
using namespace bolt::drivers;
using namespace bolt::shell;
using namespace bolt::fs;
using namespace bolt::events;
using namespace bolt::log;
using namespace bolt::panic;
using namespace bolt::sched;

extern "C" void kernel_main() {
    // =========================================================================
    // Phase 0: Hardware Detection (before anything else)
    // =========================================================================
    sys::System::init();  // Detect CPU, memory, etc.
    
    // =========================================================================
    // Phase 1: Early initialization (before we can log)
    // =========================================================================
    
    // Always init VGA text mode first (needed for fallback)
    VGA::init();
    
    // Initialize serial for debug output
    Serial::init();
    
    // Initialize timer (needed for logging timestamps)
    PIT::init();
    RTC::init();
    
    // =========================================================================
    // Phase 2: Core systems with logging
    // =========================================================================
    
    // Initialize logging system - output to serial only for clean display
    LogConfig log_config = {
        .min_level = Level::Debug,
        .targets = Target::Serial,      // Debug output goes to serial console only
        .show_timestamp = true,
        .show_location = false,
        .show_level = true,
        .use_colors = true
    };
    Logger::init(log_config);
    
    // Initialize panic handler
    Panic::init();
    LOG_INFO("Panic handler ready");
    
    // Initialize legacy heap (for compatibility)
    mem::Heap::init();
    LOG_INFO("Legacy heap initialized");
    
    // Initialize Physical Memory Manager
    mem::PMM::init();
    auto pmm_stats = mem::PMM::get_stats();
    LOGF_INFO("PMM: %u MB total, %u pages free", 
              static_cast<u32>(pmm_stats.total_memory / (1024 * 1024)),
              pmm_stats.free_pages);
    
    // Initialize IDT (interrupts) - needed before VMM for page fault handler
    IDT::init();
    LOG_INFO("IDT initialized - interrupts ready");
    
    // Initialize Virtual Memory Manager
    LOG_DEBUG("Setting up paging structures...");
    mem::VMM::init();
    mem::VMM::register_page_fault_handler();
    LOG_INFO("VMM: Page tables ready, enabling paging...");
    
    // Enable paging!
    mem::VMM::enable_paging();
    LOG_INFO("VMM: Paging ENABLED - virtual memory active");
    
    // Now that paging is enabled, init framebuffer (VESA graphics)
    Framebuffer::init();
    if (Framebuffer::is_available()) {
        LOG_INFO("Framebuffer: VESA graphics initialized");
    } else {
        LOG_INFO("Framebuffer: Not available, using VGA text mode");
    }
    
    // Initialize task manager (multitasking)
    TaskManager::init();
    LOG_INFO("Task manager ready");
    
    // Initialize event system
    EventQueue::init();
    LOG_DEBUG("Event queue initialized");
    
    // =========================================================================
    // Phase 3: Device drivers
    // =========================================================================
    
    Keyboard::init();
    LOG_INFO("Keyboard driver loaded");
    
    // Initialize mouse
    Mouse::init();
    if (Framebuffer::is_available()) {
        Mouse::set_bounds(static_cast<i32>(Framebuffer::width()), 
                          static_cast<i32>(Framebuffer::height()));
    }
    LOG_INFO("Mouse driver loaded");
    
    // Initialize PCI bus
    PCI::init();
    LOG_INFO("PCI bus enumeration complete");
    
    // Initialize ATA/IDE
    ATA::init();
    LOG_INFO("ATA/IDE driver loaded");
    
    // =========================================================================
    // Phase 4: Storage Subsystem
    // =========================================================================
    
    // Initialize the new unified storage subsystem
    auto storage_result = storage::Storage::init();
    switch (storage_result) {
        case storage::StorageInitResult::Success:
            LOG_INFO("Storage: All systems operational");
            break;
        case storage::StorageInitResult::PartialSuccess:
            LOG_WARN("Storage: Some devices unavailable");
            break;
        case storage::StorageInitResult::DegradedRAMFS:
            LOG_WARN("Storage: Running in RAMFS fallback mode");
            break;
        case storage::StorageInitResult::Failed:
            LOG_ERROR("Storage: Initialization failed!");
            break;
    }
    
    // Print storage status
    storage::Storage::print_status();
    
    // Legacy filesystem support (keeping for compatibility)
    RAMFS::init();
    LOG_DEBUG("Legacy RAMFS available");
    
    // Create default files in RAMFS for shell
    RAMFS::create("/readme.txt");
    FileHandle f = RAMFS::open("/readme.txt");
    const char* readme = "Welcome to BOLT OS!\nType 'help' for commands.\n";
    RAMFS::write(f, readme, 46);
    RAMFS::close(f);
    
    RAMFS::create("/sysinfo.txt");
    f = RAMFS::open("/sysinfo.txt");
    const char* sysinfo = "BOLT OS v0.4\nArchitecture: x86 (32-bit)\nFeatures: PMM, Paging, Logging\n";
    RAMFS::write(f, sysinfo, 70);
    RAMFS::close(f);
    
    // =========================================================================
    // Phase 5: User interface
    // =========================================================================
    
    LOG_INFO("Boot complete - starting shell");
    
    // Display banner using appropriate driver
    if (Framebuffer::is_available()) {
        // Use framebuffer for high-res display
        Framebuffer::set_text_color(Color32::Primary(), Color32::Background());
        Framebuffer::println("");
        Framebuffer::println("  ____   ____  _   _____    ___  ____");
        Framebuffer::println(" | __ ) / __ \\| | |_   _|  / _ \\/ ___|");
        Framebuffer::println(" |  _ \\| |  | | |   | |   | | | \\___ \\");
        Framebuffer::println(" | |_) | |__| | |___| |   | |_| |___) |");
        Framebuffer::println(" |____/ \\____/|_____|_|    \\___/|____/");
        Framebuffer::println("");
        Framebuffer::set_text_color(Color32::Text(), Color32::Background());
        Framebuffer::println("  Welcome to BOLT OS - High Resolution Mode");
        Framebuffer::println("");
    } else {
        VGA::show_banner();
        VGA::show_welcome();
    }
    
    // Start shell
    Shell::init();
    Shell::run();
}
