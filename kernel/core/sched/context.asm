; ===========================================================================
; BOLT OS - Context Switch Assembly
; ===========================================================================
; Low-level context switch between tasks
; ===========================================================================

[BITS 32]

section .text

; void switch_context(u32* old_esp, u32 new_esp)
; Saves current context to old_esp and switches to new_esp
global switch_context
switch_context:
    ; Save current context
    pushfd              ; Push EFLAGS
    push gs
    push fs
    push es
    push ds
    pusha               ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    
    ; Get parameters from stack (after our pushes)
    ; [ESP + 48] = old_esp pointer
    ; [ESP + 52] = new_esp
    mov eax, [esp + 48] ; old_esp pointer
    mov [eax], esp      ; Save current ESP to *old_esp
    
    ; Switch to new stack
    mov esp, [esp + 52] ; Load new ESP
    
    ; Restore new context
    popa                ; Pop EDI, ESI, EBP, (ESP ignored), EBX, EDX, ECX, EAX
    pop ds
    pop es
    pop fs
    pop gs
    popfd               ; Pop EFLAGS
    
    ret                 ; Return to new task


; Task entry wrapper - ensures tasks start with clean state
global task_entry_wrapper
task_entry_wrapper:
    ; Re-enable interrupts (may have been disabled during switch)
    sti
    
    ; The actual entry point is on the stack, jump to it
    ret
