; ===========================================================================
; BOLT OS - Interrupt Service Routines (Assembly Stubs)
; ===========================================================================
; These stubs save CPU state and call C handlers
;
; Uses linker script section: .text.isr for interrupt handlers
; ===========================================================================

[bits 32]

section .text.isr

; Macro for ISRs that don't push error codes
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; Dummy error code
    push dword %1       ; Interrupt number
    jmp isr_common
%endmacro

; Macro for ISRs that push error codes
%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1       ; Interrupt number (error code already pushed)
    jmp isr_common
%endmacro

; Macro for IRQs
%macro IRQ 2
global irq%1
irq%1:
    push dword 0        ; Dummy error code
    push dword %2       ; Interrupt number
    jmp irq_common
%endmacro

; CPU Exceptions
ISR_NOERR 0     ; Division by zero
ISR_NOERR 1     ; Debug
ISR_NOERR 2     ; NMI
ISR_NOERR 3     ; Breakpoint
ISR_NOERR 4     ; Overflow
ISR_NOERR 5     ; Bound range exceeded
ISR_NOERR 6     ; Invalid opcode
ISR_NOERR 7     ; Device not available
ISR_ERR   8     ; Double fault
ISR_NOERR 9     ; Coprocessor segment overrun
ISR_ERR   10    ; Invalid TSS
ISR_ERR   11    ; Segment not present
ISR_ERR   12    ; Stack-segment fault
ISR_ERR   13    ; General protection fault
ISR_ERR   14    ; Page fault
ISR_NOERR 15    ; Reserved
ISR_NOERR 16    ; x87 FPU error
ISR_ERR   17    ; Alignment check
ISR_NOERR 18    ; Machine check
ISR_NOERR 19    ; SIMD floating-point
ISR_NOERR 20    ; Virtualization
ISR_NOERR 21    ; Reserved
ISR_NOERR 22    ; Reserved
ISR_NOERR 23    ; Reserved
ISR_NOERR 24    ; Reserved
ISR_NOERR 25    ; Reserved
ISR_NOERR 26    ; Reserved
ISR_NOERR 27    ; Reserved
ISR_NOERR 28    ; Reserved
ISR_NOERR 29    ; Reserved
ISR_ERR   30    ; Security exception
ISR_NOERR 31    ; Reserved

; Hardware IRQs (remapped to 32-47)
IRQ 0, 32       ; Timer
IRQ 1, 33       ; Keyboard
IRQ 2, 34       ; Cascade
IRQ 3, 35       ; COM2
IRQ 4, 36       ; COM1
IRQ 5, 37       ; LPT2
IRQ 6, 38       ; Floppy
IRQ 7, 39       ; LPT1
IRQ 8, 40       ; RTC
IRQ 9, 41       ; Free
IRQ 10, 42      ; Free
IRQ 11, 43      ; Free
IRQ 12, 44      ; Mouse
IRQ 13, 45      ; FPU
IRQ 14, 46      ; ATA Primary
IRQ 15, 47      ; ATA Secondary

; External C handlers
extern isr_handler
extern irq_handler

; Common ISR handler
isr_common:
    pusha               ; Save all registers
    
    mov ax, ds
    push eax            ; Save data segment
    
    mov ax, 0x10        ; Load kernel data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp            ; Pass pointer to stack frame
    call isr_handler
    add esp, 4
    
    pop eax             ; Restore data segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa                ; Restore registers
    add esp, 8          ; Clean up error code and interrupt number
    iret

; Common IRQ handler
irq_common:
    pusha
    
    mov ax, ds
    push eax
    
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    push esp
    call irq_handler
    add esp, 4
    
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    popa
    add esp, 8
    iret
