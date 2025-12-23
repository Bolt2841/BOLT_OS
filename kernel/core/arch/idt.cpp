/* ===========================================================================
 * BOLT OS - Interrupt Descriptor Table Implementation
 * =========================================================================== */

#include "idt.hpp"
#include "../sys/io.hpp"
#include "../../drivers/video/vga.hpp"

namespace bolt {

using namespace drivers;

IDTEntry IDT::entries[IDT_ENTRIES];
IDTPointer IDT::pointer;
InterruptHandler IDT::handlers[IDT_ENTRIES];

// PIC ports
constexpr u16 PIC1_COMMAND = 0x20;
constexpr u16 PIC1_DATA    = 0x21;
constexpr u16 PIC2_COMMAND = 0xA0;
constexpr u16 PIC2_DATA    = 0xA1;

void IDT::init() {
    // Clear handlers
    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        handlers[i] = nullptr;
    }
    
    // Set up IDT pointer
    pointer.limit = sizeof(entries) - 1;
    pointer.base = reinterpret_cast<u32>(&entries);
    
    // Clear all entries
    for (u32 i = 0; i < IDT_ENTRIES; i++) {
        set_gate(i, 0, 0, 0);
    }
    
    // Remap PIC to avoid conflicts with CPU exceptions
    remap_pic();
    
    // CPU exceptions (0-31)
    set_gate(0,  reinterpret_cast<u32>(isr0),  0x08, 0x8E);
    set_gate(1,  reinterpret_cast<u32>(isr1),  0x08, 0x8E);
    set_gate(2,  reinterpret_cast<u32>(isr2),  0x08, 0x8E);
    set_gate(3,  reinterpret_cast<u32>(isr3),  0x08, 0x8E);
    set_gate(4,  reinterpret_cast<u32>(isr4),  0x08, 0x8E);
    set_gate(5,  reinterpret_cast<u32>(isr5),  0x08, 0x8E);
    set_gate(6,  reinterpret_cast<u32>(isr6),  0x08, 0x8E);
    set_gate(7,  reinterpret_cast<u32>(isr7),  0x08, 0x8E);
    set_gate(8,  reinterpret_cast<u32>(isr8),  0x08, 0x8E);
    set_gate(9,  reinterpret_cast<u32>(isr9),  0x08, 0x8E);
    set_gate(10, reinterpret_cast<u32>(isr10), 0x08, 0x8E);
    set_gate(11, reinterpret_cast<u32>(isr11), 0x08, 0x8E);
    set_gate(12, reinterpret_cast<u32>(isr12), 0x08, 0x8E);
    set_gate(13, reinterpret_cast<u32>(isr13), 0x08, 0x8E);
    set_gate(14, reinterpret_cast<u32>(isr14), 0x08, 0x8E);
    set_gate(15, reinterpret_cast<u32>(isr15), 0x08, 0x8E);
    set_gate(16, reinterpret_cast<u32>(isr16), 0x08, 0x8E);
    set_gate(17, reinterpret_cast<u32>(isr17), 0x08, 0x8E);
    set_gate(18, reinterpret_cast<u32>(isr18), 0x08, 0x8E);
    set_gate(19, reinterpret_cast<u32>(isr19), 0x08, 0x8E);
    set_gate(20, reinterpret_cast<u32>(isr20), 0x08, 0x8E);
    set_gate(21, reinterpret_cast<u32>(isr21), 0x08, 0x8E);
    set_gate(22, reinterpret_cast<u32>(isr22), 0x08, 0x8E);
    set_gate(23, reinterpret_cast<u32>(isr23), 0x08, 0x8E);
    set_gate(24, reinterpret_cast<u32>(isr24), 0x08, 0x8E);
    set_gate(25, reinterpret_cast<u32>(isr25), 0x08, 0x8E);
    set_gate(26, reinterpret_cast<u32>(isr26), 0x08, 0x8E);
    set_gate(27, reinterpret_cast<u32>(isr27), 0x08, 0x8E);
    set_gate(28, reinterpret_cast<u32>(isr28), 0x08, 0x8E);
    set_gate(29, reinterpret_cast<u32>(isr29), 0x08, 0x8E);
    set_gate(30, reinterpret_cast<u32>(isr30), 0x08, 0x8E);
    set_gate(31, reinterpret_cast<u32>(isr31), 0x08, 0x8E);
    
    // Hardware IRQs (32-47)
    set_gate(32, reinterpret_cast<u32>(irq0),  0x08, 0x8E);
    set_gate(33, reinterpret_cast<u32>(irq1),  0x08, 0x8E);
    set_gate(34, reinterpret_cast<u32>(irq2),  0x08, 0x8E);
    set_gate(35, reinterpret_cast<u32>(irq3),  0x08, 0x8E);
    set_gate(36, reinterpret_cast<u32>(irq4),  0x08, 0x8E);
    set_gate(37, reinterpret_cast<u32>(irq5),  0x08, 0x8E);
    set_gate(38, reinterpret_cast<u32>(irq6),  0x08, 0x8E);
    set_gate(39, reinterpret_cast<u32>(irq7),  0x08, 0x8E);
    set_gate(40, reinterpret_cast<u32>(irq8),  0x08, 0x8E);
    set_gate(41, reinterpret_cast<u32>(irq9),  0x08, 0x8E);
    set_gate(42, reinterpret_cast<u32>(irq10), 0x08, 0x8E);
    set_gate(43, reinterpret_cast<u32>(irq11), 0x08, 0x8E);
    set_gate(44, reinterpret_cast<u32>(irq12), 0x08, 0x8E);
    set_gate(45, reinterpret_cast<u32>(irq13), 0x08, 0x8E);
    set_gate(46, reinterpret_cast<u32>(irq14), 0x08, 0x8E);
    set_gate(47, reinterpret_cast<u32>(irq15), 0x08, 0x8E);
    
    // Load IDT
    asm volatile("lidt %0" : : "m"(pointer));
}

void IDT::set_gate(u8 num, u32 handler, u16 selector, u8 flags) {
    entries[num].base_low  = handler & 0xFFFF;
    entries[num].base_high = (handler >> 16) & 0xFFFF;
    entries[num].selector  = selector;
    entries[num].zero      = 0;
    entries[num].flags     = flags;
}

void IDT::register_handler(u8 interrupt, InterruptHandler handler) {
    handlers[interrupt] = handler;
}

void IDT::enable_interrupts() {
    asm volatile("sti");
}

void IDT::disable_interrupts() {
    asm volatile("cli");
}

void IDT::remap_pic() {
    // Start initialization
    io::outb(PIC1_COMMAND, 0x11);
    io::outb(PIC2_COMMAND, 0x11);
    
    // Set vector offsets
    io::outb(PIC1_DATA, 0x20);  // IRQ 0-7  -> INT 32-39
    io::outb(PIC2_DATA, 0x28);  // IRQ 8-15 -> INT 40-47
    
    // Set cascade
    io::outb(PIC1_DATA, 0x04);
    io::outb(PIC2_DATA, 0x02);
    
    // 8086 mode
    io::outb(PIC1_DATA, 0x01);
    io::outb(PIC2_DATA, 0x01);
    
    // Enable all IRQs
    io::outb(PIC1_DATA, 0x00);
    io::outb(PIC2_DATA, 0x00);
}

} // namespace bolt

// C handler for CPU exceptions
extern "C" void isr_handler(bolt::InterruptFrame* frame) {
    using namespace bolt;
    using namespace bolt::drivers;
    
    if (IDT::handlers[frame->int_no]) {
        IDT::handlers[frame->int_no](frame);
    } else {
        // Unhandled exception - show error
        VGA::set_color(Color::LightRed);
        VGA::print("\n!!! CPU Exception: ");
        VGA::print_dec(frame->int_no);
        VGA::println(" !!!");
        VGA::set_color(Color::LightGray);
        
        // Halt on fatal exceptions
        if (frame->int_no < 32) {
            VGA::println("System halted.");
            asm volatile("cli; hlt");
        }
    }
}

// C handler for hardware IRQs
extern "C" void irq_handler(bolt::InterruptFrame* frame) {
    using namespace bolt;
    
    // Call registered handler
    if (IDT::handlers[frame->int_no]) {
        IDT::handlers[frame->int_no](frame);
    }
    
    // Send EOI (End Of Interrupt)
    if (frame->int_no >= 40) {
        io::outb(0xA0, 0x20);  // PIC2
    }
    io::outb(0x20, 0x20);  // PIC1
}
