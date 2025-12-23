#pragma once
/* ===========================================================================
 * BOLT OS - Interrupt Descriptor Table (IDT)
 * ===========================================================================
 * Handles CPU exceptions and hardware interrupts (IRQs)
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt {

// IDT entry (gate descriptor)
struct IDTEntry {
    u16 base_low;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 base_high;
} __attribute__((packed));

// IDT pointer for LIDT instruction
struct IDTPointer {
    u16 limit;
    u32 base;
} __attribute__((packed));

// Interrupt frame pushed by CPU - also used externally so define early
struct InterruptFrame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha
    uint32_t int_no, err_code;                         // interrupt number and error
    uint32_t eip, cs, eflags, useresp, ss;            // pushed by CPU
} __attribute__((packed));

// Interrupt handler function type
using InterruptHandler = void (*)(InterruptFrame*);

class IDT {
public:
    static constexpr u32 IDT_ENTRIES = 256;
    
    // IRQ numbers (remapped to 32-47)
    static constexpr u8 IRQ_TIMER    = 32;
    static constexpr u8 IRQ_KEYBOARD = 33;
    static constexpr u8 IRQ_CASCADE  = 34;
    static constexpr u8 IRQ_COM2     = 35;
    static constexpr u8 IRQ_COM1     = 36;
    static constexpr u8 IRQ_LPT2     = 37;
    static constexpr u8 IRQ_FLOPPY   = 38;
    static constexpr u8 IRQ_LPT1     = 39;
    static constexpr u8 IRQ_RTC      = 40;
    static constexpr u8 IRQ_FREE1    = 41;
    static constexpr u8 IRQ_FREE2    = 42;
    static constexpr u8 IRQ_FREE3    = 43;
    static constexpr u8 IRQ_MOUSE    = 44;
    static constexpr u8 IRQ_FPU      = 45;
    static constexpr u8 IRQ_ATA1     = 46;
    static constexpr u8 IRQ_ATA2     = 47;
    
    static void init();
    static void set_gate(u8 num, u32 handler, u16 selector, u8 flags);
    static void register_handler(u8 interrupt, InterruptHandler handler);
    
    static void enable_interrupts();
    static void disable_interrupts();
    
    // Handler array (public for ISR/IRQ stubs)
    static InterruptHandler handlers[IDT_ENTRIES];
    
private:
    static IDTEntry entries[IDT_ENTRIES];
    static IDTPointer pointer;
    
    static void remap_pic();
};

} // namespace bolt

// External ISR stubs (defined in assembly)
extern "C" {
    void isr0();  void isr1();  void isr2();  void isr3();
    void isr4();  void isr5();  void isr6();  void isr7();
    void isr8();  void isr9();  void isr10(); void isr11();
    void isr12(); void isr13(); void isr14(); void isr15();
    void isr16(); void isr17(); void isr18(); void isr19();
    void isr20(); void isr21(); void isr22(); void isr23();
    void isr24(); void isr25(); void isr26(); void isr27();
    void isr28(); void isr29(); void isr30(); void isr31();
    
    void irq0();  void irq1();  void irq2();  void irq3();
    void irq4();  void irq5();  void irq6();  void irq7();
    void irq8();  void irq9();  void irq10(); void irq11();
    void irq12(); void irq13(); void irq14(); void irq15();
    
    void isr_handler(bolt::InterruptFrame* frame);
    void irq_handler(bolt::InterruptFrame* frame);
}
