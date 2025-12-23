/* ===========================================================================
 * BOLT OS - Minimal C Entry Point
 * ===========================================================================
 * This file provides the absolute minimum to get into C++ as fast as possible.
 * All real functionality lives in C++ with namespaces.
 * 
 * Linker script symbols used:
 *   __init_array_start, __init_array_end  - C++ constructors
 *   __fini_array_start, __fini_array_end  - C++ destructors
 *   __preinit_array_start, __preinit_array_end - Pre-init functions
 *   __bss_start, __bss_end                - BSS section (zeroed by bootloader)
 *   __kernel_start, __kernel_end          - Kernel bounds
 *   __heap_start                          - Heap region start
 * =========================================================================== */

#include <stdint.h>

/* External C++ entry point */
extern void kernel_main(void);

/* ===========================================================================
 * Linker Script Symbols
 * =========================================================================== */

/* Constructors/Destructors */
typedef void (*init_func_t)(void);
extern init_func_t __preinit_array_start[];
extern init_func_t __preinit_array_end[];
extern init_func_t __init_array_start[];
extern init_func_t __init_array_end[];
extern init_func_t __fini_array_start[];
extern init_func_t __fini_array_end[];

/* Memory regions (use &symbol to get address) */
extern uint32_t __kernel_start;
extern uint32_t __kernel_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern uint32_t __heap_start;
extern uint32_t __stack_bottom;
extern uint32_t __stack_top;

/* ===========================================================================
 * Initialization Functions
 * =========================================================================== */

static void call_preinit_array(void) {
    for (init_func_t* func = __preinit_array_start; func < __preinit_array_end; func++) {
        if (*func) (*func)();
    }
}

static void call_constructors(void) {
    for (init_func_t* func = __init_array_start; func < __init_array_end; func++) {
        if (*func) (*func)();
    }
}

static void call_destructors(void) {
    /* Call in reverse order */
    for (init_func_t* func = __fini_array_end - 1; func >= __fini_array_start; func--) {
        if (*func) (*func)();
    }
}

/* ===========================================================================
 * Kernel Entry Point - Called from bootloader
 * =========================================================================== */
__attribute__((section(".text.entry"), noreturn))
void _start(void) {
    /* Zero BSS section */
    uint32_t* bss = &__bss_start;
    while (bss < &__bss_end) {
        *bss++ = 0;
    }
    
    /* Set stack to linker-defined location (16KB, 16-byte aligned) */
    __asm__ volatile(
        "mov %0, %%esp"
        :
        : "r"(&__stack_top)
    );
    
    /* Call pre-initialization functions */
    call_preinit_array();
    
    /* Call C++ global constructors */
    call_constructors();
    
    /* Jump to C++ kernel */
    kernel_main();
    
    /* If kernel returns, call destructors */
    call_destructors();
    
    /* Halt forever */
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
