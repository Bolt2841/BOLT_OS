; ==============================================================================
; BOLT OS - Unified Bootloader (Compact)
; ==============================================================================
; Supports: HDD (LBA), CD-ROM (LBA)
;
; Build options:
;   -DCDROM_MODE : Force CD-ROM mode (2048-byte sectors, LBA 21)
; ==============================================================================

[BITS 16]
[ORG 0x7C00]

%include "kernel/core/arch/memory_layout.inc"

BOOT_DRIVE_ADDR equ 0x610
BOOT_MODE_ADDR  equ 0x614

    jmp short start
    nop
kernel_sectors: db 64           ; Patched by build script

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov [BOOT_DRIVE_ADDR], dl
    mov byte [VESA_ENABLED], 0

%ifdef CDROM_MODE
    mov byte [BOOT_MODE_ADDR], 2    ; CD mode
%else
    mov byte [BOOT_MODE_ADDR], 1    ; HDD mode
%endif

    ; Print "BOLT"
    mov si, msg
    call puts

    ; Load kernel via LBA
    mov word [dap_seg], KERNEL_LOAD_SEG
    mov word [dap_off], 0

%ifdef CDROM_MODE
    mov dword [dap_lba], 21         ; CD: kernel at sector 21
    xor cx, cx
    mov cl, [kernel_sectors]
    add cx, 3
    shr cx, 2                       ; Convert to 2048-byte sectors
%else
    mov dword [dap_lba], 1          ; HDD: kernel at sector 1
    xor cx, cx
    mov cl, [kernel_sectors]
%endif

.load:
    test cx, cx
    jz .done
    push cx
    mov word [dap_cnt], 1
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc err

    ; Advance buffer
    mov ax, [dap_off]
%ifdef CDROM_MODE
    add ax, 2048
%else
    add ax, 512
%endif
    jnz .nowrap
    mov ax, [dap_seg]
    add ax, 0x1000
    mov [dap_seg], ax
    xor ax, ax
.nowrap:
    mov [dap_off], ax
    inc dword [dap_lba]
    pop cx
    dec cx
    jmp .load

.done:
    call detect_mem
    call setup_vesa
    
    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Protected mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm

err:
    mov si, errs
    call puts
    jmp $

puts:
    lodsb
    test al, al
    jz .d
    mov ah, 0x0E
    int 0x10
    jmp puts
.d: ret

detect_mem:
    pushad
    xor ax, ax
    mov es, ax
    xor ebx, ebx
    mov di, MEMMAP_ADDR + 4
    xor bp, bp
.lp:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc .dn
    cmp eax, 0x534D4150
    jne .dn
    cmp dword [es:di+16], 1
    jne .nx
    inc bp
    add di, 24
.nx:
    test ebx, ebx
    jz .dn
    jmp .lp
.dn:
    mov [MEMMAP_ADDR], bp
    mov cx, bp
    test cx, cx
    jz .fb
    mov si, MEMMAP_ADDR + 4
    xor eax, eax
.sm:
    add eax, [si+8]
    add si, 24
    loop .sm
    mov [MEMINFO_ADDR], eax
    jmp .ed
.fb:
    mov dword [MEMINFO_ADDR], 0x10000000
    mov word [MEMMAP_ADDR], 0
.ed:
    popad
    ret

setup_vesa:
    pusha
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .t2
    mov cx, 0x118
    jmp .gi
.t2:
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .fl
    mov cx, 0x115
.gi:
    mov ax, 0x4F01
    mov di, 0x800
    int 0x10
    mov ax, [0x812]
    mov [VESA_WIDTH], ax
    mov ax, [0x814]
    mov [VESA_HEIGHT], ax
    mov al, [0x819]
    mov [VESA_BPP], al
    mov ax, [0x810]
    mov [VESA_PITCH], ax
    mov eax, [0x828]
    mov [VESA_FRAMEBUF], eax
    mov byte [VESA_ENABLED], 1
.fl:
    popa
    ret

msg:  db "BOLT", 13, 10, 0
errs: db "ERR", 0
boot_drive: db 0

align 4
dap:    db 16, 0
dap_cnt: dw 1
dap_off: dw 0
dap_seg: dw 0x1000
dap_lba: dd 1, 0

align 8
gdt:    dq 0
        dw 0xFFFF, 0, 0x9A00, 0x00CF
        dw 0xFFFF, 0, 0x9200, 0x00CF
gdt_desc: dw 23
          dd gdt

[BITS 32]
pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, BOOT_STACK_ADDR
    jmp KERNEL_LOAD_ADDR

%ifdef CDROM_MODE
    times 2048 - ($ - $$) db 0
%else
    times 510 - ($ - $$) db 0
    dw 0xAA55
%endif
