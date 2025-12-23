/*
 * ============================================================================
 *                            BOLT OS Linker Script
 * ============================================================================
 * 
 * Memory Layout:
 *   0x00000000 - 0x000003FF : Real Mode IVT
 *   0x00000400 - 0x000004FF : BIOS Data Area
 *   0x00000500 - 0x00007BFF : Free (conventional memory)
 *   0x00007C00 - 0x00007DFF : Bootloader (512 bytes)
 *   0x00007E00 - 0x0000FFFF : Free (used by bootloader)
 *   0x00010000 - 0x???????? : Kernel loaded here
 *   
 * Target: i686-elf (32-bit x86)
 * 
 */

/* Kernel entry point */
ENTRY(_start)

/* Memory constants */
KERNEL_VIRTUAL_BASE = 0x00010000;
KERNEL_PAGE_SIZE    = 0x1000;

SECTIONS
{
    /* ========================================================================
     * Kernel Load Address
     * ======================================================================== */
    . = KERNEL_VIRTUAL_BASE;
    
    __kernel_start = .;

    /* ========================================================================
     * .text - Executable Code Section
     * ======================================================================== */
    .text ALIGN(KERNEL_PAGE_SIZE) :
    {
        __text_start = .;
        
        /* Entry point must come first */
        *(.text.entry)
        *(.text.boot)
        
        /* Main kernel code */
        *(.text .text.*)
        
        /* Interrupt handlers */
        *(.text.isr)
        *(.text.irq)
        
        __text_end = .;
    }

    /* ========================================================================
     * .rodata - Read-Only Data Section
     * ======================================================================== */
    .rodata ALIGN(KERNEL_PAGE_SIZE) :
    {
        __rodata_start = .;
        
        *(.rodata .rodata.*)
        
        /* String literals */
        *(.rodata.str1.1)
        *(.rodata.str1.4)
        
        __rodata_end = .;
    }

    /* ========================================================================
     * .data - Initialized Data Section
     * ======================================================================== */
    .data ALIGN(KERNEL_PAGE_SIZE) :
    {
        __data_start = .;
        
        *(.data .data.*)
        
        /* Small data sections */
        *(.sdata .sdata.*)
        
        __data_end = .;
    }

    /* ========================================================================
     * C++ Global Constructors/Destructors
     * ======================================================================== */
    
    /* Pre-initialization array (called before constructors) */
    .preinit_array ALIGN(4) :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    }

    /* Initialization array (C++ global constructors) */
    .init_array ALIGN(4) :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP(*(.ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
    }

    /* Finalization array (C++ global destructors) */
    .fini_array ALIGN(4) :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP(*(.dtors))
        PROVIDE_HIDDEN(__fini_array_end = .);
    }

    /* ========================================================================
     * .bss - Uninitialized Data Section (Zero-filled)
     * ======================================================================== */
    .bss ALIGN(KERNEL_PAGE_SIZE) :
    {
        __bss_start = .;
        
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        
        /* Ensure BSS is page-aligned at end */
        . = ALIGN(KERNEL_PAGE_SIZE);
        
        __bss_end = .;
    }

    /* ========================================================================
     * Kernel Stack (16KB, 16-byte aligned for SSE)
     * ======================================================================== */
    .stack ALIGN(16) (NOLOAD) :
    {
        __stack_bottom = .;
        . += 0x4000;  /* 16 KB stack */
        __stack_top = .;
    }

    /* ========================================================================
     * Kernel Heap Region Marker
     * ======================================================================== */
    . = ALIGN(KERNEL_PAGE_SIZE);
    __heap_start = .;
    
    /* Heap grows upward from here */

    /* ========================================================================
     * End of Kernel
     * ======================================================================== */
    __kernel_end = .;

    /* ========================================================================
     * Debug Sections (Optional - Stripped in release builds)
     * ======================================================================== */
    .stab          0 : { *(.stab) }
    .stabstr       0 : { *(.stabstr) }
    .stab.excl     0 : { *(.stab.excl) }
    .stab.exclstr  0 : { *(.stab.exclstr) }
    .stab.index    0 : { *(.stab.index) }
    .stab.indexstr 0 : { *(.stab.indexstr) }
    
    /* DWARF debug sections */
    .debug_info       0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev     0 : { *(.debug_abbrev) }
    .debug_line       0 : { *(.debug_line .debug_line.* .debug_line_end) }
    .debug_frame      0 : { *(.debug_frame) }
    .debug_str        0 : { *(.debug_str) }
    .debug_loc        0 : { *(.debug_loc) }
    .debug_macinfo    0 : { *(.debug_macinfo) }
    .debug_ranges     0 : { *(.debug_ranges) }
    .debug_aranges    0 : { *(.debug_aranges) }

    /* ========================================================================
     * Discarded Sections
     * ======================================================================== */
    /DISCARD/ :
    {
        /* Comments and notes */
        *(.comment)
        *(.note .note.*)
        *(.gnu.hash)
        
        /* Exception handling (not used in kernel) */
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        
        /* Thread-local storage (not used) */
        *(.tdata .tdata.*)
        *(.tbss .tbss.*)
        
        /* ARM-specific (not applicable) */
        *(.ARM.*)
    }
}

/* ============================================================================
 * Exported Symbols for Kernel Use
 * ============================================================================
 * 
 * These symbols can be accessed in C/C++ as:
 *   extern "C" {
 *       extern uint32_t __kernel_start;
 *       extern uint32_t __kernel_end;
 *       extern uint32_t __bss_start;
 *       extern uint32_t __bss_end;
 *       // etc.
 *   }
 * 
 * Use addresses: &__kernel_start, &__kernel_end
 * 
 */

/* Assertions to verify layout */
ASSERT(__text_start == KERNEL_VIRTUAL_BASE, "Kernel must start at KERNEL_VIRTUAL_BASE")
ASSERT(__bss_end > __bss_start, "BSS section is invalid")
