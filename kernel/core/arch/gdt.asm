; ===========================================================================
; BOLT OS - GDT Flush (Reload Segment Registers)
; ===========================================================================
; Uses linker script section: .text for kernel code
; ===========================================================================

[bits 32]

section .text

global gdt_flush

gdt_flush:
    mov eax, [esp + 4]      ; Get GDT pointer
    lgdt [eax]              ; Load GDT
    
    ; Reload segment registers
    mov ax, 0x10            ; Kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to reload CS
    jmp 0x08:.flush
    
.flush:
    ret
