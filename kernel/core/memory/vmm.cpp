/* ===========================================================================
 * BOLT OS - Virtual Memory Manager Implementation
 * ===========================================================================
 * x86 paging with page directories and tables
 * =========================================================================== */

#include "vmm.hpp"
#include "pmm.hpp"
#include "heap.hpp"
#include "../sys/io.hpp"
#include "../sys/log.hpp"
#include "../arch/idt.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../drivers/video/vga.hpp"

namespace bolt::mem {

// Static member definitions
PageDirectory* VMM::current_directory = nullptr;
PageDirectory* VMM::kernel_directory = nullptr;
VirtualMemoryStats VMM::stats = {0, 0, 0, 0};

// Assembly helpers for CR0/CR3
extern "C" {
    static inline void load_page_directory(u32 phys_addr) {
        asm volatile("mov %0, %%cr3" : : "r"(phys_addr) : "memory");
    }
    
    static inline u32 read_cr3() {
        u32 val;
        asm volatile("mov %%cr3, %0" : "=r"(val));
        return val;
    }
    
    static inline void enable_paging_cr0() {
        u32 cr0;
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x80000000;  // Set PG bit
        asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }
    
    static inline void disable_paging_cr0() {
        u32 cr0;
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 &= ~0x80000000;  // Clear PG bit
        asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    }
    
    static inline u32 read_cr0() {
        u32 val;
        asm volatile("mov %%cr0, %0" : "=r"(val));
        return val;
    }
    
    static inline u32 read_cr2() {
        u32 val;
        asm volatile("mov %%cr2, %0" : "=r"(val));
        return val;
    }
}

// Fixed memory locations for initial paging structures (below heap at 3MB)
// These MUST be within the identity-mapped region
static constexpr u32 KERNEL_PD_ADDR = 0x300000;     // 3MB - Page Directory
static constexpr u32 KERNEL_PT_START = 0x301000;    // 3MB+4KB - Start of Page Tables
// We need 4 page tables to map 16MB (each PT maps 4MB)

void* VMM::alloc_aligned(u32 size) {
    // Allocate from PMM (already page-aligned)
    u32 pages = (size + PMM::PAGE_SIZE - 1) / PMM::PAGE_SIZE;
    return reinterpret_cast<void*>(PMM::alloc_pages(pages));
}

void VMM::init() {
    // Use fixed addresses for kernel page structures
    // These are in the first 4MB which we'll identity map with the PD itself
    kernel_directory = reinterpret_cast<PageDirectory*>(KERNEL_PD_ADDR);
    
    // Clear the page directory
    for (int i = 0; i < 1024; i++) {
        kernel_directory->entries[i].value = 0;
    }
    
    // Identity map the first 16MB for kernel
    // This maps virtual addresses to same physical addresses
    identity_map_kernel();
    
    // Set as current
    current_directory = kernel_directory;
    
    // Load the page directory (but don't enable paging yet)
    load_page_directory(reinterpret_cast<u32>(kernel_directory));
}

void VMM::identity_map_kernel() {
    // Identity map first 16MB (kernel space)
    // Using 4KB pages for fine-grained control
    // We use fixed page table addresses to avoid chicken-and-egg problem
    
    u32 map_size = 0x1000000;  // 16MB
    u32 num_page_tables = map_size / (1024 * PMM::PAGE_SIZE);  // 4 page tables
    
    for (u32 pt_idx = 0; pt_idx < num_page_tables; pt_idx++) {
        // Use fixed page table addresses
        u32 pt_phys = KERNEL_PT_START + (pt_idx * PMM::PAGE_SIZE);
        PageTable* pt = reinterpret_cast<PageTable*>(pt_phys);
        
        // Fill in the page table entries (identity mapping)
        for (u32 pte_idx = 0; pte_idx < 1024; pte_idx++) {
            u32 phys_addr = (pt_idx * 1024 + pte_idx) * PMM::PAGE_SIZE;
            pt->entries[pte_idx].set(phys_addr, PageFlags::KernelPage);
        }
        
        // Set the page directory entry
        kernel_directory->entries[pt_idx].set(pt_phys, PageFlags::KernelPage);
        stats.page_tables_allocated++;
    }
    
    stats.pages_mapped = map_size / PMM::PAGE_SIZE;
}

void VMM::enable_paging() {
    if (current_directory) {
        load_page_directory(reinterpret_cast<u32>(current_directory));
        enable_paging_cr0();
    }
}

void VMM::disable_paging() {
    disable_paging_cr0();
}

bool VMM::is_paging_enabled() {
    return (read_cr0() & 0x80000000) != 0;
}

PageTable* VMM::get_page_table(u32 virt_addr, bool create) {
    u32 pde_index = VIRT_TO_PDE_INDEX(virt_addr);
    PageDirectoryEntry& pde = current_directory->entries[pde_index];
    
    if (pde.is_present()) {
        return reinterpret_cast<PageTable*>(pde.get_address());
    }
    
    if (!create) {
        return nullptr;
    }
    
    // Allocate new page table
    u32 pt_phys = PMM::alloc_page();
    if (pt_phys == 0) {
        return nullptr;  // Out of memory
    }
    
    // Clear the new page table
    PageTable* pt = reinterpret_cast<PageTable*>(pt_phys);
    for (int i = 0; i < 1024; i++) {
        pt->entries[i].value = 0;
    }
    
    // Set up the page directory entry
    pde.set(pt_phys, PageFlags::Present | PageFlags::ReadWrite);
    
    stats.page_tables_allocated++;
    
    return pt;
}

bool VMM::map_page(u32 virt_addr, u32 phys_addr, u32 flags) {
    // Get or create page table
    PageTable* pt = get_page_table(virt_addr, true);
    if (!pt) {
        return false;  // Failed to allocate page table
    }
    
    u32 pte_index = VIRT_TO_PTE_INDEX(virt_addr);
    pt->entries[pte_index].set(phys_addr, flags);
    
    stats.pages_mapped++;
    
    // Flush TLB for this page
    flush_tlb_page(virt_addr);
    
    return true;
}

void VMM::unmap_page(u32 virt_addr) {
    PageTable* pt = get_page_table(virt_addr, false);
    if (!pt) {
        return;  // Not mapped
    }
    
    u32 pte_index = VIRT_TO_PTE_INDEX(virt_addr);
    pt->entries[pte_index].value = 0;
    
    stats.pages_unmapped++;
    
    // Flush TLB for this page
    flush_tlb_page(virt_addr);
}

bool VMM::map_range(u32 virt_start, u32 phys_start, u32 size, u32 flags) {
    u32 pages = (size + PMM::PAGE_SIZE - 1) / PMM::PAGE_SIZE;
    
    for (u32 i = 0; i < pages; i++) {
        u32 virt = virt_start + i * PMM::PAGE_SIZE;
        u32 phys = phys_start + i * PMM::PAGE_SIZE;
        
        if (!map_page(virt, phys, flags)) {
            // Failed - unmap what we've done so far
            unmap_range(virt_start, i * PMM::PAGE_SIZE);
            return false;
        }
    }
    
    return true;
}

void VMM::unmap_range(u32 virt_start, u32 size) {
    u32 pages = (size + PMM::PAGE_SIZE - 1) / PMM::PAGE_SIZE;
    
    for (u32 i = 0; i < pages; i++) {
        unmap_page(virt_start + i * PMM::PAGE_SIZE);
    }
}

u32 VMM::alloc_page(u32 virt_addr, u32 flags) {
    // Allocate physical page
    u32 phys_addr = PMM::alloc_page();
    if (phys_addr == 0) {
        return 0;  // Out of physical memory
    }
    
    // Map it
    if (!map_page(virt_addr, phys_addr, flags)) {
        PMM::free_page(phys_addr);
        return 0;  // Failed to map
    }
    
    return phys_addr;
}

void VMM::free_page(u32 virt_addr) {
    // Get physical address before unmapping
    u32 phys_addr = get_physical_address(virt_addr);
    
    // Unmap the page
    unmap_page(virt_addr);
    
    // Free physical page
    if (phys_addr) {
        PMM::free_page(phys_addr);
    }
}

u32 VMM::get_physical_address(u32 virt_addr) {
    PageTable* pt = get_page_table(virt_addr, false);
    if (!pt) {
        return 0;  // Not mapped
    }
    
    u32 pte_index = VIRT_TO_PTE_INDEX(virt_addr);
    PageTableEntry& pte = pt->entries[pte_index];
    
    if (!pte.is_present()) {
        return 0;  // Not present
    }
    
    return pte.get_address() + VIRT_TO_PAGE_OFFSET(virt_addr);
}

bool VMM::is_mapped(u32 virt_addr) {
    PageTable* pt = get_page_table(virt_addr, false);
    if (!pt) {
        return false;
    }
    
    u32 pte_index = VIRT_TO_PTE_INDEX(virt_addr);
    return pt->entries[pte_index].is_present();
}

void VMM::flush_tlb_page(u32 virt_addr) {
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void VMM::flush_tlb() {
    // Reload CR3 to flush entire TLB
    u32 cr3 = read_cr3();
    load_page_directory(cr3);
}

void VMM::switch_directory(PageDirectory* dir) {
    current_directory = dir;
    load_page_directory(reinterpret_cast<u32>(dir));
}

PageDirectory* VMM::clone_directory() {
    // Allocate new page directory
    PageDirectory* new_dir = reinterpret_cast<PageDirectory*>(alloc_aligned(sizeof(PageDirectory)));
    if (!new_dir) {
        return nullptr;
    }
    
    // Copy entries (shallow copy - shares page tables)
    for (int i = 0; i < 1024; i++) {
        new_dir->entries[i].value = current_directory->entries[i].value;
    }
    
    return new_dir;
}

void VMM::page_fault_handler(u32 error_code, u32 fault_addr) {
    stats.page_faults++;
    
    // Decode error code
    bool present = error_code & 0x1;       // Page was present
    bool write_op = error_code & 0x2;      // Write operation
    bool user = error_code & 0x4;          // User mode
    bool reserved = error_code & 0x8;      // Reserved bit violation
    bool fetch = error_code & 0x10;        // Instruction fetch
    
    // Output to both serial and VGA for visibility
    bolt::drivers::Serial::write("\n========================================\n");
    bolt::drivers::Serial::write("[PAGE FAULT] Virtual address: 0x");
    bolt::drivers::Serial::write_hex(fault_addr);
    bolt::drivers::Serial::write("\n");
    bolt::drivers::Serial::write("  Type: ");
    bolt::drivers::Serial::write(present ? "PROTECTION VIOLATION" : "PAGE NOT PRESENT");
    bolt::drivers::Serial::write("\n  Operation: ");
    bolt::drivers::Serial::write(write_op ? "WRITE" : "READ");
    if (fetch) bolt::drivers::Serial::write(" (instruction fetch)");
    bolt::drivers::Serial::write("\n  Mode: ");
    bolt::drivers::Serial::write(user ? "USER" : "KERNEL");
    if (reserved) bolt::drivers::Serial::write("\n  RESERVED BIT SET IN PAGE ENTRY!");
    bolt::drivers::Serial::write("\n========================================\n");
    
    // Also show on VGA
    bolt::drivers::VGA::set_color(bolt::drivers::Color::LightRed);
    bolt::drivers::VGA::println("\n!!! PAGE FAULT !!!");
    bolt::drivers::VGA::print("Address: 0x");
    bolt::drivers::VGA::print_hex(fault_addr);
    bolt::drivers::VGA::print(" - ");
    bolt::drivers::VGA::println(present ? "Protection violation" : "Page not present");
    bolt::drivers::VGA::set_color(bolt::drivers::Color::LightGray);
    
    // Halt on page faults - later we can implement demand paging
    bolt::drivers::Serial::write("System halted.\n");
    asm volatile("cli; hlt");
}

// ISR wrapper for page fault (interrupt 14)
static void page_fault_isr(bolt::InterruptFrame* frame) {
    u32 fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));
    VMM::page_fault_handler(frame->err_code, fault_addr);
}

void VMM::register_page_fault_handler() {
    bolt::IDT::register_handler(14, page_fault_isr);
}

} // namespace bolt::mem
