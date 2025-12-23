/* ===========================================================================
 * BOLT OS - Physical Memory Manager Implementation
 * ===========================================================================
 * Bitmap-based physical page allocator
 * =========================================================================== */

#include "pmm.hpp"
#include "heap.hpp"
#include "../sys/io.hpp"
#include "../sys/system.hpp"

namespace bolt::mem {

// Static member definitions
u8* PMM::bitmap = nullptr;
u32 PMM::bitmap_size = 0;
u64 PMM::total_memory = 0;
u32 PMM::total_pages = 0;
u32 PMM::used_pages = 0;
u32 PMM::free_page_count = 0;

void PMM::init() {
    // First try to get memory from System detection
    if (sys::g_system && sys::g_system->total_memory > 0) {
        total_memory = sys::g_system->total_memory;
    } else {
        // Fallback: Read total memory from bootloader (stored at 0x500 by E820 detection)
        total_memory = *reinterpret_cast<volatile u32*>(MEMMAP_ADDR);
    }
    
    // Sanity check - if detection failed, assume 16MB
    constexpr u64 MIN_REASONABLE_MEMORY = 0x200000;     // 2MB
    constexpr u64 MAX_REASONABLE_MEMORY = 0x100000000;  // 4GB
    constexpr u64 FALLBACK_MEMORY = 0x1000000;          // 16MB
    
    if (total_memory < MIN_REASONABLE_MEMORY || total_memory > MAX_REASONABLE_MEMORY) {
        total_memory = FALLBACK_MEMORY;
    }
    
    // Cap at max supported memory
    if (total_memory > MAX_MEMORY) {
        total_memory = MAX_MEMORY;
    }
    
    // Update System info with actual detected value
    if (sys::g_system) {
        sys::g_system->total_memory = total_memory;
        sys::g_system->usable_memory = total_memory - (4 * 1024 * 1024);
    }
    
    // Calculate total pages
    total_pages = static_cast<u32>(total_memory / PAGE_SIZE);
    
    // Calculate bitmap size (1 bit per page)
    bitmap_size = (total_pages + PAGES_PER_BYTE - 1) / PAGES_PER_BYTE;
    
    // Place bitmap at fixed location (after heap start)
    bitmap = reinterpret_cast<u8*>(BITMAP_START);
    
    // Initialize bitmap: mark ALL pages as used initially
    for (u32 i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF;
    }
    used_pages = total_pages;
    free_page_count = 0;
    
    // Now mark usable memory as free
    // We'll mark memory above 4MB as free (kernel + bitmap + heap below)
    constexpr u32 FREE_MEMORY_START = 0x400000;  // 4MB - everything below is reserved
    u32 free_end = static_cast<u32>(total_memory);
    
    if (free_end > FREE_MEMORY_START) {
        mark_region_free(FREE_MEMORY_START, free_end - FREE_MEMORY_START);
    }
    
    // Ensure first 1MB stays reserved (BIOS, IVT, etc.)
    mark_region_used(0, KERNEL_START);
    
    // Ensure kernel region stays reserved (1MB - 4MB)
    mark_region_used(KERNEL_START, FREE_MEMORY_START - KERNEL_START);
}

u32 PMM::alloc_page() {
    if (free_page_count == 0) {
        return 0;  // Out of memory
    }
    
    u32 frame = find_first_free();
    if (frame == 0xFFFFFFFF) {
        return 0;  // No free page found
    }
    
    bitmap_set(frame);
    used_pages++;
    free_page_count--;
    
    return frame_to_addr(frame);
}

u32 PMM::alloc_pages(u32 count) {
    if (count == 0) return 0;
    if (count == 1) return alloc_page();
    if (free_page_count < count) return 0;
    
    u32 start_frame = find_first_free_sequence(count);
    if (start_frame == 0xFFFFFFFF) {
        return 0;  // No contiguous region found
    }
    
    // Mark all pages as used
    for (u32 i = 0; i < count; i++) {
        bitmap_set(start_frame + i);
    }
    
    used_pages += count;
    free_page_count -= count;
    
    return frame_to_addr(start_frame);
}

void PMM::free_page(u32 phys_addr) {
    if (phys_addr == 0) return;
    
    u32 frame = addr_to_frame(phys_addr);
    if (frame >= total_pages) return;
    
    // Only free if currently used
    if (bitmap_test(frame)) {
        bitmap_clear(frame);
        used_pages--;
        free_page_count++;
    }
}

void PMM::free_page_range(u32 phys_addr, u32 count) {
    for (u32 i = 0; i < count; i++) {
        free_page(phys_addr + i * PAGE_SIZE);
    }
}

bool PMM::is_page_free(u32 phys_addr) {
    u32 frame = addr_to_frame(phys_addr);
    if (frame >= total_pages) return false;
    return !bitmap_test(frame);
}

void PMM::mark_region_used(u32 base, u32 length) {
    u32 start_frame = addr_to_frame(PAGE_ALIGN_DOWN(base));
    u32 end_frame = addr_to_frame(PAGE_ALIGN_UP(base + length));
    
    for (u32 frame = start_frame; frame < end_frame && frame < total_pages; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            used_pages++;
            free_page_count--;
        }
    }
}

void PMM::mark_region_free(u32 base, u32 length) {
    u32 start_frame = addr_to_frame(PAGE_ALIGN_UP(base));
    u32 end_frame = addr_to_frame(PAGE_ALIGN_DOWN(base + length));
    
    for (u32 frame = start_frame; frame < end_frame && frame < total_pages; frame++) {
        if (bitmap_test(frame)) {
            bitmap_clear(frame);
            used_pages--;
            free_page_count++;
        }
    }
}

PhysicalMemoryStats PMM::get_stats() {
    PhysicalMemoryStats stats;
    stats.total_memory = total_memory;
    stats.usable_memory = total_memory;  // Simplified
    stats.used_memory = static_cast<u64>(used_pages) * PAGE_SIZE;
    stats.free_memory = static_cast<u64>(free_page_count) * PAGE_SIZE;
    stats.total_pages = total_pages;
    stats.used_pages = used_pages;
    stats.free_pages = free_page_count;
    return stats;
}

void PMM::bitmap_set(u32 frame) {
    if (frame < total_pages) {
        bitmap[frame / PAGES_PER_BYTE] |= (1 << (frame % PAGES_PER_BYTE));
    }
}

void PMM::bitmap_clear(u32 frame) {
    if (frame < total_pages) {
        bitmap[frame / PAGES_PER_BYTE] &= ~(1 << (frame % PAGES_PER_BYTE));
    }
}

bool PMM::bitmap_test(u32 frame) {
    if (frame >= total_pages) return true;  // Out of range = used
    return (bitmap[frame / PAGES_PER_BYTE] & (1 << (frame % PAGES_PER_BYTE))) != 0;
}

u32 PMM::find_first_free() {
    for (u32 i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFF) {  // At least one free bit
            for (u32 bit = 0; bit < PAGES_PER_BYTE; bit++) {
                u32 frame = i * PAGES_PER_BYTE + bit;
                if (frame < total_pages && !bitmap_test(frame)) {
                    return frame;
                }
            }
        }
    }
    return 0xFFFFFFFF;  // No free page
}

u32 PMM::find_first_free_sequence(u32 count) {
    u32 consecutive = 0;
    u32 start_frame = 0;
    
    for (u32 frame = 0; frame < total_pages; frame++) {
        if (!bitmap_test(frame)) {
            if (consecutive == 0) {
                start_frame = frame;
            }
            consecutive++;
            if (consecutive >= count) {
                return start_frame;
            }
        } else {
            consecutive = 0;
        }
    }
    
    return 0xFFFFFFFF;  // No contiguous region found
}

} // namespace bolt::mem
