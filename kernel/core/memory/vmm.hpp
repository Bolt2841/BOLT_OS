#pragma once
/* ===========================================================================
 * BOLT OS - Virtual Memory Manager (VMM)
 * ===========================================================================
 * x86 paging implementation with page directories and page tables.
 * Manages virtual-to-physical address mapping.
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "pmm.hpp"

namespace bolt::mem {

// Page flags (x86 specific)
namespace PageFlags {
    constexpr u32 Present       = 1 << 0;   // Page is present in memory
    constexpr u32 ReadWrite     = 1 << 1;   // Page is writable (0 = read-only)
    constexpr u32 User          = 1 << 2;   // User-mode accessible
    constexpr u32 WriteThrough  = 1 << 3;   // Write-through caching
    constexpr u32 CacheDisable  = 1 << 4;   // Disable caching
    constexpr u32 Accessed      = 1 << 5;   // Page has been accessed
    constexpr u32 Dirty         = 1 << 6;   // Page has been written to
    constexpr u32 PageSize      = 1 << 7;   // 4MB page (in PDE only)
    constexpr u32 Global        = 1 << 8;   // Global page (not flushed on CR3 reload)
    
    // Common flag combinations
    constexpr u32 KernelPage    = Present | ReadWrite;              // Kernel RW
    constexpr u32 KernelPageRO  = Present;                          // Kernel RO
    constexpr u32 UserPage      = Present | ReadWrite | User;       // User RW
    constexpr u32 UserPageRO    = Present | User;                   // User RO
}

// Page directory entry (PDE)
struct PageDirectoryEntry {
    u32 value;
    
    void set(u32 phys_addr, u32 flags) {
        value = (phys_addr & 0xFFFFF000) | (flags & 0xFFF);
    }
    
    u32 get_address() const { return value & 0xFFFFF000; }
    u32 get_flags() const { return value & 0xFFF; }
    bool is_present() const { return value & PageFlags::Present; }
    bool is_large() const { return value & PageFlags::PageSize; }
} __attribute__((packed));

// Page table entry (PTE)
struct PageTableEntry {
    u32 value;
    
    void set(u32 phys_addr, u32 flags) {
        value = (phys_addr & 0xFFFFF000) | (flags & 0xFFF);
    }
    
    u32 get_address() const { return value & 0xFFFFF000; }
    u32 get_flags() const { return value & 0xFFF; }
    bool is_present() const { return value & PageFlags::Present; }
} __attribute__((packed));

// Page directory (1024 entries, covers 4GB)
struct PageDirectory {
    PageDirectoryEntry entries[1024];
} __attribute__((aligned(4096)));

// Page table (1024 entries, covers 4MB)
struct PageTable {
    PageTableEntry entries[1024];
} __attribute__((aligned(4096)));

// Virtual address structure
struct VirtualAddress {
    u32 value;
    
    VirtualAddress(u32 addr) : value(addr) {}
    
    u32 pde_index() const { return (value >> 22) & 0x3FF; }      // Top 10 bits
    u32 pte_index() const { return (value >> 12) & 0x3FF; }      // Middle 10 bits
    u32 page_offset() const { return value & 0xFFF; }            // Bottom 12 bits
};

// VMM statistics
struct VirtualMemoryStats {
    u32 page_faults;
    u32 pages_mapped;
    u32 pages_unmapped;
    u32 page_tables_allocated;
};

class VMM {
public:
    // Initialize virtual memory manager
    static void init();
    
    // Enable paging
    static void enable_paging();
    
    // Disable paging
    static void disable_paging();
    
    // Check if paging is enabled
    static bool is_paging_enabled();
    
    // Map a single page (virtual -> physical)
    // Returns true on success
    static bool map_page(u32 virt_addr, u32 phys_addr, u32 flags = PageFlags::KernelPage);
    
    // Unmap a single page
    static void unmap_page(u32 virt_addr);
    
    // Map a range of pages
    static bool map_range(u32 virt_start, u32 phys_start, u32 size, u32 flags = PageFlags::KernelPage);
    
    // Unmap a range of pages
    static void unmap_range(u32 virt_start, u32 size);
    
    // Allocate and map a page (gets physical page from PMM)
    static u32 alloc_page(u32 virt_addr, u32 flags = PageFlags::KernelPage);
    
    // Unmap and free a page (returns physical page to PMM)
    static void free_page(u32 virt_addr);
    
    // Get physical address for virtual address
    // Returns 0 if not mapped
    static u32 get_physical_address(u32 virt_addr);
    
    // Check if virtual address is mapped
    static bool is_mapped(u32 virt_addr);
    
    // Flush TLB for a specific page
    static void flush_tlb_page(u32 virt_addr);
    
    // Flush entire TLB
    static void flush_tlb();
    
    // Get current page directory
    static PageDirectory* get_current_directory() { return current_directory; }
    
    // Switch page directory (for process switching)
    static void switch_directory(PageDirectory* dir);
    
    // Clone current page directory (for fork)
    static PageDirectory* clone_directory();
    
    // Get VMM statistics
    static VirtualMemoryStats get_stats() { return stats; }
    
    // Page fault handler (called from IDT)
    static void page_fault_handler(u32 error_code, u32 fault_addr);
    
    // Register page fault handler with IDT
    static void register_page_fault_handler();

private:
    // Get or create page table for a virtual address
    static PageTable* get_page_table(u32 virt_addr, bool create = false);
    
    // Allocate a page-aligned structure
    static void* alloc_aligned(u32 size);
    
    // Current page directory
    static PageDirectory* current_directory;
    static PageDirectory* kernel_directory;
    
    // Statistics
    static VirtualMemoryStats stats;
    
    // Identity map the first N megabytes
    static void identity_map_kernel();
};

// Helper macros
#define VIRT_TO_PDE_INDEX(addr) (((u32)(addr)) >> 22)
#define VIRT_TO_PTE_INDEX(addr) ((((u32)(addr)) >> 12) & 0x3FF)
#define VIRT_TO_PAGE_OFFSET(addr) (((u32)(addr)) & 0xFFF)

} // namespace bolt::mem
