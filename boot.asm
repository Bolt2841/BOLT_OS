; ==============================================================================
; BOLT OS - Bootloader with VESA Graphics Support
; ==============================================================================

[BITS 16]
[ORG 0x7C00]

; Memory info stored at 0x500
MEMINFO_ADDR    equ 0x500
MEMMAP_ADDR     equ 0x504

; VESA info stored at 0x600  
VESA_WIDTH      equ 0x600       ; 2 bytes
VESA_HEIGHT     equ 0x602       ; 2 bytes
VESA_BPP        equ 0x604       ; 1 byte
VESA_PITCH      equ 0x606       ; 2 bytes
VESA_FRAMEBUF   equ 0x608       ; 4 bytes
VESA_ENABLED    equ 0x60C       ; 1 byte

; =============================================================================
; Entry - Jump over data area
; =============================================================================
    jmp short real_start
    nop

; =============================================================================
; Data area at offset 3 (patched by build script)
; =============================================================================
kernel_sectors: db 64           ; Byte at offset 3: sectors to load (patched by build)

; =============================================================================
; Main bootloader code
; =============================================================================
real_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Clear VESA flag
    mov byte [VESA_ENABLED], 0

    ; Reset disk
    xor ax, ax
    int 0x13

    ; Load kernel: ES:BX = 0x1000:0000, using LBA-to-CHS conversion
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    xor di, di                  ; DI = current LBA (starts at 1)
    inc di
    mov si, 3                   ; SI = retry counter

.read_sector:
    mov al, [kernel_sectors]    ; Check if done
    xor ah, ah
    cmp di, ax
    ja .load_done
    
    ; Convert LBA (DI) to CHS for floppy (18 spt, 2 heads)
    ; Sector = (LBA % 18) + 1
    ; Head = (LBA / 18) % 2
    ; Cylinder = (LBA / 18) / 2
    mov ax, di
    xor dx, dx
    mov cx, 18
    div cx                      ; AX = LBA/18, DX = LBA%18
    push ax                     ; Save LBA/18
    inc dl                      ; Sector = (LBA%18)+1
    mov cl, dl                  ; CL = sector (1-18)
    pop ax
    xor dx, dx
    mov bp, 2
    div bp                      ; AX = cylinder, DX = head
    mov ch, al                  ; CH = cylinder
    mov dh, dl                  ; DH = head
    
    mov ah, 0x02
    mov al, 1
    mov dl, 0
    int 0x13
    jc .retry
    
    add bx, 512                 ; Next buffer position
    jnc .no_wrap
    mov ax, es
    add ax, 0x1000
    mov es, ax
    xor bx, bx
.no_wrap:
    inc di                      ; Next LBA
    mov si, 3                   ; Reset retries
    jmp .read_sector

.retry:
    dec si
    jnz .read_sector
    jmp disk_fail

.load_done:

    ; Detect memory
    call detect_memory

    ; Setup VESA graphics mode
    call setup_vesa

    ; Enable A20
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Enter protected mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

disk_fail:
    mov si, err_disk
    call print_str
    jmp $

print_str:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_str
.done:
    ret

err_disk: db "Disk!", 0

; ------------------------------------------------------------------------------
; Setup VESA - Try 1024x768, fallback to 800x600
; ------------------------------------------------------------------------------
setup_vesa:
    pusha
    
    ; Try mode 0x118 (1024x768x32 with LFB)
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne .try_115
    
    ; Get mode info for 0x118
    mov ax, 0x4F01
    mov cx, 0x118
    mov di, 0x800
    int 0x10
    jmp .store_from_info

.try_115:
    ; Fallback: 800x600x24
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .fail
    
    ; Get mode info for 0x115
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, 0x800
    int 0x10

.store_from_info:
    ; Read actual values from VBE mode info block at 0x800
    ; Offset 18: XResolution (word)
    ; Offset 20: YResolution (word)
    ; Offset 25: BitsPerPixel (byte)
    ; Offset 16: BytesPerScanLine (word)
    ; Offset 40: PhysBasePtr (dword)
    mov ax, [0x800 + 18]       ; XResolution
    mov [VESA_WIDTH], ax
    mov ax, [0x800 + 20]       ; YResolution
    mov [VESA_HEIGHT], ax
    mov al, [0x800 + 25]       ; BitsPerPixel
    mov [VESA_BPP], al
    mov ax, [0x800 + 16]       ; BytesPerScanLine (pitch)
    mov [VESA_PITCH], ax
    mov eax, [0x800 + 40]      ; PhysBasePtr (framebuffer)
    mov [VESA_FRAMEBUF], eax
    mov byte [VESA_ENABLED], 1
    popa
    ret

.fail:
    mov byte [VESA_ENABLED], 0
    popa
    ret

; ------------------------------------------------------------------------------
; Detect memory using E820
; ------------------------------------------------------------------------------
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

; ------------------------------------------------------------------------------
; GDT
; ------------------------------------------------------------------------------
align 8
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0
    db 0, 0b10011010, 0b11001111, 0
gdt_data:
    dw 0xFFFF, 0
    db 0, 0b10010010, 0b11001111, 0
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; ------------------------------------------------------------------------------
; 32-bit Protected Mode
; ------------------------------------------------------------------------------
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

; ------------------------------------------------------------------------------
; Boot signature
; ------------------------------------------------------------------------------
times 510 - ($ - $$) db 0
dw 0xAA55
