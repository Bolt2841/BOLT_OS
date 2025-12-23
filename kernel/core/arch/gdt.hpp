#pragma once
/* ===========================================================================
 * BOLT OS - Global Descriptor Table (GDT)
 * ===========================================================================
 * Defines memory segments for protected mode
 * =========================================================================== */

#include "../../lib/types.hpp"

// External assembly function - uses uint32_t directly to avoid namespace issues
extern "C" void gdt_flush(uint32_t gdtr);

namespace bolt {

struct GDTEntry {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct GDTPointer {
    u16 limit;
    u32 base;
} __attribute__((packed));

class GDT {
public:
    static constexpr u8 KERNEL_CODE = 0x08;
    static constexpr u8 KERNEL_DATA = 0x10;
    static constexpr u8 USER_CODE   = 0x18;
    static constexpr u8 USER_DATA   = 0x20;
    
    static void init();
    
private:
    static constexpr u32 GDT_ENTRIES = 5;
    static GDTEntry entries[GDT_ENTRIES];
    static GDTPointer pointer;
    
    static void set_gate(u32 num, u32 base, u32 limit, u8 access, u8 gran);
};

} // namespace bolt
