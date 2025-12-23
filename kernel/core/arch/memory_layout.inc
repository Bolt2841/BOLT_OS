; ===========================================================================
; BOLT OS - Memory Layout Constants
; ===========================================================================
; This file contains memory addresses that must match kernel/linker.ld
; Include this file in all bootloader assembly files
;
; These values MUST match the linker script (kernel/linker.ld)
; ===========================================================================

; Kernel load address (where bootloader loads the kernel)
KERNEL_LOAD_ADDR    equ 0x10000     ; Must match KERNEL_VIRTUAL_BASE in linker.ld

; Temporary stack for bootloader (before kernel sets up its own)
BOOT_STACK_ADDR     equ 0x90000     ; Safe area below EBDA

; Memory info locations (set by bootloader, read by kernel)
MEMINFO_ADDR        equ 0x500       ; Total memory in bytes
MEMMAP_ADDR         equ 0x504       ; E820 memory map entries

; VESA info locations (set by bootloader, read by kernel)
VESA_WIDTH          equ 0x600       ; Screen width (2 bytes)
VESA_HEIGHT         equ 0x602       ; Screen height (2 bytes)
VESA_BPP            equ 0x604       ; Bits per pixel (1 byte)
VESA_PITCH          equ 0x606       ; Bytes per scanline (2 bytes)
VESA_FRAMEBUF       equ 0x608       ; Framebuffer address (4 bytes)
VESA_ENABLED        equ 0x60C       ; VESA mode active flag (1 byte)

; GDT Segment Selectors
GDT_NULL_SEL        equ 0x00        ; Null segment
GDT_CODE_SEL        equ 0x08        ; Kernel code segment
GDT_DATA_SEL        equ 0x10        ; Kernel data segment

; Bootloader locations
BOOT_LOAD_ADDR      equ 0x7C00      ; Where BIOS loads boot sector
BOOT_SECTOR_SIZE    equ 512         ; Boot sector size

; Kernel loading
KERNEL_LOAD_SEG     equ 0x1000      ; Segment for loading kernel (0x1000:0 = 0x10000)
