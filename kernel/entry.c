/* ===========================================================================
 * BOLT OS - Minimal C Entry Point
 * ===========================================================================
 * This file provides the absolute minimum to get into C++ as fast as possible.
 * All real functionality lives in C++ with namespaces.
 * =========================================================================== */

/* External C++ entry point */
extern void kernel_main(void);

/* Global constructors (for C++ static objects) */
typedef void (*constructor_t)(void);
extern constructor_t __init_array_start[];
extern constructor_t __init_array_end[];

static void call_constructors(void) {
    for (constructor_t* ctor = __init_array_start; ctor < __init_array_end; ctor++) {
        (*ctor)();
    }
}

/* ===========================================================================
 * Kernel Entry Point - Called from bootloader
 * =========================================================================== */
__attribute__((section(".text.entry")))
void _start(void) {
    /* Call C++ global constructors */
    call_constructors();
    
    /* Jump to C++ kernel */
    kernel_main();
    
    /* Halt if kernel returns */
    while (1) {
        __asm__ volatile("hlt");
    }
}
