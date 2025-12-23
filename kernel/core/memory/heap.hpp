#pragma once
/* ===========================================================================
 * BOLT OS - Memory Operations
 * =========================================================================== */

#include "../../lib/types.hpp"
#include "../sys/config.hpp"

namespace bolt::mem {

// Basic memory operations
inline void* memset(void* dest, int val, usize count) {
    u8* d = static_cast<u8*>(dest);
    while (count--) *d++ = static_cast<u8>(val);
    return dest;
}

inline void* memcpy(void* dest, const void* src, usize count) {
    u8* d = static_cast<u8*>(dest);
    const u8* s = static_cast<const u8*>(src);
    while (count--) *d++ = *s++;
    return dest;
}

inline int memcmp(const void* s1, const void* s2, usize count) {
    const u8* p1 = static_cast<const u8*>(s1);
    const u8* p2 = static_cast<const u8*>(s2);
    while (count--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

// Simple heap allocator
class Heap {
public:
    // Memory info location (set by bootloader)
    static constexpr u32 MEMINFO_ADDR = 0x500;
    
    // Use config values for heap layout
    static constexpr u32 HEAP_START = config::HEAP_START;
    static constexpr u32 MIN_HEAP_SIZE = config::HEAP_SIZE;
    static constexpr u32 MAX_HEAP_SIZE = config::MAX_MEMORY;
    
    static void init();
    static void* alloc(usize size);
    static void* alloc_zeroed(usize size);
    static void free(void* ptr);
    static usize get_used();
    static usize get_free();
    static usize get_total();
    static usize get_total_system_memory();
    
private:
    struct Block {
        u32 size;
        bool used;
        Block* next;
    };
    
    static Block* head;
    static u32 heap_used;
    static u32 heap_size;
    static u32 total_system_memory;
};

} // namespace bolt::mem

// Global operators for new/delete
void* operator new(bolt::usize size);
void* operator new[](bolt::usize size);
void operator delete(void* ptr) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete(void* ptr, bolt::usize) noexcept;
void operator delete[](void* ptr, bolt::usize) noexcept;
