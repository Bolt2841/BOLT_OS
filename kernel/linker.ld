/* BOLT OS Linker Script */
ENTRY(_start)

SECTIONS
{
    . = 0x10000;
    
    .text.entry : { *(.text.entry) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    
    .data : { *(.data .data.*) }
    
    /* C++ global constructors */
    .init_array : {
        __init_array_start = .;
        *(.init_array)
        *(.ctors)
        __init_array_end = .;
    }
    
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    /DISCARD/ : { 
        *(.comment) 
        *(.eh_frame) 
        *(.note*) 
    }
}
