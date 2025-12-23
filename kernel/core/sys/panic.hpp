#pragma once
/* ===========================================================================
 * BOLT OS - Kernel Panic Handler
 * ===========================================================================
 * Handles unrecoverable errors with diagnostic output
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::panic {

// CPU register state
struct Registers {
    u32 eax, ebx, ecx, edx;
    u32 esi, edi, ebp, esp;
    u32 eip, eflags;
    u32 cs, ds, es, fs, gs, ss;
    u32 cr0, cr2, cr3, cr4;
};

// Panic reasons
enum class Reason {
    Unknown,
    OutOfMemory,
    PageFault,
    GeneralProtectionFault,
    DoubleFault,
    StackOverflow,
    DivisionByZero,
    InvalidOpcode,
    AssertionFailed,
    KernelException,
    UserRequest
};

class Panic {
public:
    // Initialize panic handler
    static void init();
    
    // Trigger kernel panic with message
    [[noreturn]] static void panic(const char* message);
    
    // Trigger panic with reason
    [[noreturn]] static void panic(Reason reason, const char* message = nullptr);
    
    // Trigger panic with registers
    [[noreturn]] static void panic_with_regs(const char* message, const Registers& regs);
    
    // Assertion helper
    static void assert_failed(const char* expr, const char* file, u32 line);
    
    // Get reason string
    static const char* reason_to_string(Reason reason);

private:
    // Print panic header
    static void print_header(const char* message);
    
    // Print register dump
    static void print_registers(const Registers& regs);
    
    // Print simple stack trace
    static void print_stack_trace();
    
    // Capture current registers
    static Registers capture_registers();
    
    // Halt the system
    [[noreturn]] static void halt();
    
    static bool initialized;
    static bool panicking;  // Prevent recursive panic
};

// Kernel assertion macro
#define KASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::bolt::panic::Panic::assert_failed(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

// Panic macro
#define KPANIC(msg) ::bolt::panic::Panic::panic(msg)
#define KPANIC_REASON(reason, msg) ::bolt::panic::Panic::panic(reason, msg)

} // namespace bolt::panic
