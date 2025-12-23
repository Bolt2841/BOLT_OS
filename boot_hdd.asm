; ==============================================================================
; BOLT OS - Hard Disk Bootloader with LBA Support
; ==============================================================================
; Compact bootloader using LBA mode (INT 13h extensions)
; ==============================================================================

[BITS 16]
[ORG 0x7C00]

MEMINFO_ADDR    equ 0x500
MEMMAP_ADDR     equ 0x504
VESA_WIDTH      equ 0x600
VESA_HEIGHT     equ 0x602
VESA_BPP        equ 0x604
VESA_PITCH      equ 0x606
VESA_FRAMEBUF   equ 0x608
VESA_ENABLED    equ 0x60C

    jmp short real_start
    nop

kernel_sectors: db 128

real_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov byte [VESA_ENABLED], 0

    ; Load kernel using LBA
    mov si, dap
    mov word [dap_segment], 0x1000
    mov word [dap_offset], 0
    mov dword [dap_lba_lo], 1
    xor cx, cx
    mov cl, [kernel_sectors]

.lba_loop:
    test cx, cx
    jz .load_done
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_fail
    
    mov ax, [dap_offset]
    add ax, 512
    jnz .no_wrap
    mov ax, [dap_segment]
    add ax, 0x1000
    mov [dap_segment], ax
    xor ax, ax
.no_wrap:
    mov [dap_offset], ax
    inc dword [dap_lba_lo]
    dec cx
    jmp .lba_loop

.load_done:
    call detect_memory
    call setup_vesa
    in al, 0x92
    or al, 2
    out 0x92, al
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

disk_fail:
    mov si, err_msg
    mov ah, 0x0E
.pr: lodsb
    test al, al
    jz .ph
    int 0x10
    jmp .pr
.ph: jmp $

err_msg: db "Disk!", 0
boot_drive: db 0

align 4
dap:
    db 16, 0
    dw 1
dap_offset:  dw 0
dap_segment: dw 0x1000
dap_lba_lo:  dd 1
             dd 0

setup_vesa:
    pusha
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .try_115
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, 0x800
    int 0x10
    jmp .store
.try_115:
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .fail
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x800
    int 0x10
.store:
    mov ax, [0x800 + 18]
    mov [VESA_WIDTH], ax
    mov ax, [0x800 + 20]
    mov [VESA_HEIGHT], ax
    mov al, [0x800 + 25]
    mov [VESA_BPP], al
    mov ax, [0x800 + 16]
    mov [VESA_PITCH], ax
    mov eax, [0x800 + 40]
    mov [VESA_FRAMEBUF], eax
    mov byte [VESA_ENABLED], 1
.fail:
    popa
    ret

detect_memory:
    pushad
    xor ax, ax
    mov es, ax
    xor ebx, ebx
    mov di, MEMMAP_ADDR + 4
    xor bp, bp
.loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp dword [es:di + 16], 1
    jne .next
    inc bp
    add di, 24
.next:
    test ebx, ebx
    jz .done
    jmp .loop
.done:
    mov [MEMMAP_ADDR], bp
    mov cx, bp
    test cx, cx
    jz .fallback
    mov si, MEMMAP_ADDR + 4
    xor eax, eax
.sum:
    add eax, [si + 8]
    add si, 24
    loop .sum
    mov [MEMINFO_ADDR], eax
    jmp .end
.fallback:
    mov dword [MEMINFO_ADDR], 0x10000000
    mov word [MEMMAP_ADDR], 0
.end:
    popad
    ret

align 8
gdt_start: dq 0
gdt_code:  dw 0xFFFF, 0
           db 0, 0b10011010, 0b11001111, 0
gdt_data:  dw 0xFFFF, 0
           db 0, 0b10010010, 0b11001111, 0
gdt_end:
gdt_desc:  dw gdt_end - gdt_start - 1
           dd gdt_start

[BITS 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp 0x10000

times 510 - ($ - $$) db 0
dw 0xAA55
