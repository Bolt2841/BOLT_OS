/* ===========================================================================
 * BOLT OS - GDT Implementation
 * =========================================================================== */

#include "gdt.hpp"

namespace bolt {

GDTEntry GDT::entries[GDT_ENTRIES];
GDTPointer GDT::pointer;

void GDT::init() {
    pointer.limit = sizeof(entries) - 1;
    pointer.base = reinterpret_cast<u32>(&entries);
    
    // Null segment
    set_gate(0, 0, 0, 0, 0);
    
    // Kernel code segment: base=0, limit=4GB, executable, readable
    set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    
    // Kernel data segment: base=0, limit=4GB, writable
    set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    
    // User code segment: base=0, limit=4GB, executable, readable, ring 3
    set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    
    // User data segment: base=0, limit=4GB, writable, ring 3
    set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    
    // Load GDT
    gdt_flush(reinterpret_cast<u32>(&pointer));
}

void GDT::set_gate(u32 num, u32 base, u32 limit, u8 access, u8 gran) {
    entries[num].base_low    = base & 0xFFFF;
    entries[num].base_middle = (base >> 16) & 0xFF;
    entries[num].base_high   = (base >> 24) & 0xFF;
    
    entries[num].limit_low   = limit & 0xFFFF;
    entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    
    entries[num].access      = access;
}

} // namespace bolt
