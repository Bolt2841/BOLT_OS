#pragma once
/* ===========================================================================
 * BOLT OS - Physical Memory Manager (PMM)
 * ===========================================================================
 * Manages physical memory pages using a bitmap allocator.
 * Tracks which physical pages are free/used.
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "../sys/config.hpp"

namespace bolt::mem {

// Memory region types from E820/multiboot
enum class MemoryRegionType : u32 {
    Available   = 1,    // Usable RAM
    Reserved    = 2,    // Reserved by system
    ACPI        = 3,    // ACPI reclaimable
    NVS         = 4,    // ACPI NVS
    BadMemory   = 5     // Bad memory
};

// E820 memory map entry structure
struct MemoryMapEntry {
    u64 base;
    u64 length;
    MemoryRegionType type;
    u32 acpi_attrs;
} __attribute__((packed));

// Physical memory statistics
struct PhysicalMemoryStats {
    u64 total_memory;       // Total physical memory detected
    u64 usable_memory;      // Usable memory (excluding reserved)
    u64 used_memory;        // Currently allocated memory
    u64 free_memory;        // Currently free memory
    u32 total_pages;        // Total page frames
    u32 used_pages;         // Allocated page frames
    u32 free_pages;         // Free page frames
};

class PMM {
public:
    // Page size constants (use config)
    static constexpr u32 PAGE_SIZE = config::PAGE_SIZE;
    static constexpr u32 PAGE_SHIFT = 12;           // log2(4096)
    static constexpr u32 PAGES_PER_BYTE = 8;        // Bits per byte in bitmap
    
    // Memory layout constants (use config)
    static constexpr u32 KERNEL_START = config::KERNEL_LOAD_ADDR;
    static constexpr u32 BITMAP_START = config::PMM_BITMAP_START;  // PMM bitmap location
    static constexpr u32 MAX_MEMORY = config::MAX_MEMORY;
    
    // Initialize PMM with memory map from bootloader
    static void init();
    
    // Allocate a single physical page
    // Returns physical address or 0 on failure
    static u32 alloc_page();
    
    // Allocate contiguous physical pages
    // Returns physical address of first page or 0 on failure
    static u32 alloc_pages(u32 count);
    
    // Free a single physical page
    static void free_page(u32 phys_addr);
    
    // Free contiguous physical pages
    static void free_page_range(u32 phys_addr, u32 count);
    
    // Check if a page is free
    static bool is_page_free(u32 phys_addr);
    
    // Mark a range of physical memory as used (for reserved regions)
    static void mark_region_used(u32 base, u32 length);
    
    // Mark a range of physical memory as free
    static void mark_region_free(u32 base, u32 length);
    
    // Get memory statistics
    static PhysicalMemoryStats get_stats();
    
    // Get total detected memory
    static u64 get_total_memory() { return total_memory; }
    
    // Get free memory
    static u64 get_free_memory() { return free_page_count * PAGE_SIZE; }
    
    // Get used memory
    static u64 get_used_memory() { return used_pages * PAGE_SIZE; }

private:
    // Convert address to page frame number
    static u32 addr_to_frame(u32 addr) { return addr >> PAGE_SHIFT; }
    
    // Convert page frame number to address
    static u32 frame_to_addr(u32 frame) { return frame << PAGE_SHIFT; }
    
    // Set bit in bitmap (mark page as used)
    static void bitmap_set(u32 frame);
    
    // Clear bit in bitmap (mark page as free)
    static void bitmap_clear(u32 frame);
    
    // Test bit in bitmap
    static bool bitmap_test(u32 frame);
    
    // Find first free page frame
    static u32 find_first_free();
    
    // Find first sequence of free frames
    static u32 find_first_free_sequence(u32 count);
    
    // Bitmap array (1 bit per page)
    static u8* bitmap;
    static u32 bitmap_size;         // Size in bytes
    
    // Memory tracking
    static u64 total_memory;        // Total detected RAM
    static u32 total_pages;         // Total page frames
    static u32 used_pages;          // Allocated frames
    static u32 free_page_count;     // Free frames
    
    // Memory map from bootloader
    static constexpr u32 MEMMAP_ADDR = 0x500;       // Memory map location
    static constexpr u32 MEMMAP_COUNT_ADDR = 0x5F0; // Entry count location
};

// Helper macros for page alignment
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PMM::PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PMM::PAGE_SIZE - 1) & ~(PMM::PAGE_SIZE - 1))

} // namespace bolt::mem
