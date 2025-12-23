/* ===========================================================================
 * BOLT OS - Kernel Panic Handler Implementation
 * ===========================================================================
 * Provides diagnostic information on fatal errors
 * =========================================================================== */

#include "panic.hpp"
#include "../../drivers/video/vga.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../lib/string.hpp"

namespace bolt::panic {

using namespace drivers;

// Static members
bool Panic::initialized = false;
bool Panic::panicking = false;

void Panic::init() {
    initialized = true;
}

[[noreturn]] void Panic::panic(const char* message) {
    // Prevent recursive panic
    if (panicking) {
        halt();
    }
    panicking = true;
    
    // Disable interrupts
    asm volatile("cli");
    
    // Print panic info
    print_header(message);
    
    // Capture and print registers
    Registers regs = capture_registers();
    print_registers(regs);
    
    // Print stack trace
    print_stack_trace();
    
    halt();
}

[[noreturn]] void Panic::panic(Reason reason, const char* message) {
    if (panicking) {
        halt();
    }
    panicking = true;
    
    asm volatile("cli");
    
    // Build message with reason
    char full_msg[256];
    str::cpy(full_msg, reason_to_string(reason));
    if (message) {
        str::cat(full_msg, ": ");
        str::cat(full_msg, message);
    }
    
    print_header(full_msg);
    
    Registers regs = capture_registers();
    print_registers(regs);
    print_stack_trace();
    
    halt();
}

[[noreturn]] void Panic::panic_with_regs(const char* message, const Registers& regs) {
    if (panicking) {
        halt();
    }
    panicking = true;
    
    asm volatile("cli");
    
    print_header(message);
    print_registers(regs);
    print_stack_trace();
    
    halt();
}

void Panic::assert_failed(const char* expr, const char* file, u32 line) {
    if (panicking) {
        halt();
    }
    panicking = true;
    
    asm volatile("cli");
    
    // Build assertion message
    char msg[256];
    str::cpy(msg, "Assertion failed: ");
    str::cat(msg, expr);
    
    print_header(msg);
    
    // Print location
    VGA::set_color(Color::LightCyan);
    VGA::print("  Location: ");
    VGA::print(file);
    VGA::print(":");
    VGA::print_dec(static_cast<i32>(line));
    VGA::println();
    
    Serial::write("  Location: ");
    Serial::write(file);
    Serial::write(":");
    char line_str[16];
    int idx = 0;
    u32 l = line;
    if (l == 0) {
        line_str[idx++] = '0';
    } else {
        char tmp[12];
        int tmp_idx = 0;
        while (l > 0) {
            tmp[tmp_idx++] = '0' + (l % 10);
            l /= 10;
        }
        while (tmp_idx > 0) {
            line_str[idx++] = tmp[--tmp_idx];
        }
    }
    line_str[idx] = '\0';
    Serial::write(line_str);
    Serial::write("\r\n");
    
    Registers regs = capture_registers();
    print_registers(regs);
    print_stack_trace();
    
    halt();
}

const char* Panic::reason_to_string(Reason reason) {
    switch (reason) {
        case Reason::Unknown:               return "Unknown Error";
        case Reason::OutOfMemory:          return "Out of Memory";
        case Reason::PageFault:            return "Page Fault";
        case Reason::GeneralProtectionFault: return "General Protection Fault";
        case Reason::DoubleFault:          return "Double Fault";
        case Reason::StackOverflow:        return "Stack Overflow";
        case Reason::DivisionByZero:       return "Division by Zero";
        case Reason::InvalidOpcode:        return "Invalid Opcode";
        case Reason::AssertionFailed:      return "Assertion Failed";
        case Reason::KernelException:      return "Kernel Exception";
        case Reason::UserRequest:          return "User Requested Panic";
        default:                           return "Unknown Reason";
    }
}

void Panic::print_header(const char* message) {
    // VGA output
    VGA::set_color(Color::White, Color::Red);
    VGA::println();
    VGA::println("================================================================================");
    VGA::println("                           KERNEL PANIC                                        ");
    VGA::println("================================================================================");
    VGA::set_color(Color::LightRed);
    VGA::println();
    VGA::print("  ");
    VGA::println(message);
    VGA::println();
    
    // Serial output
    Serial::write("\r\n");
    Serial::write("================================================================================\r\n");
    Serial::write("                           KERNEL PANIC                                        \r\n");
    Serial::write("================================================================================\r\n");
    Serial::write("\r\n  ");
    Serial::write(message);
    Serial::write("\r\n\r\n");
}

void Panic::print_registers(const Registers& regs) {
    VGA::set_color(Color::Yellow);
    VGA::println("  Register Dump:");
    VGA::set_color(Color::LightCyan);
    
    // Helper to print hex value
    auto print_reg = [](const char* name, u32 val) {
        VGA::print("    ");
        VGA::print(name);
        VGA::print("=0x");
        VGA::print_hex(val);
        
        Serial::write("    ");
        Serial::write(name);
        Serial::write("=0x");
        Serial::write_hex(val);
    };
    
    // General purpose registers
    print_reg("EAX", regs.eax);
    print_reg(" EBX", regs.ebx);
    print_reg(" ECX", regs.ecx);
    print_reg(" EDX", regs.edx);
    VGA::println();
    Serial::write("\r\n");
    
    print_reg("ESI", regs.esi);
    print_reg(" EDI", regs.edi);
    print_reg(" EBP", regs.ebp);
    print_reg(" ESP", regs.esp);
    VGA::println();
    Serial::write("\r\n");
    
    // Instruction pointer and flags
    print_reg("EIP", regs.eip);
    print_reg(" EFLAGS", regs.eflags);
    VGA::println();
    Serial::write("\r\n");
    
    // Control registers
    VGA::set_color(Color::Yellow);
    VGA::println("  Control Registers:");
    VGA::set_color(Color::LightCyan);
    Serial::write("  Control Registers:\r\n");
    
    print_reg("CR0", regs.cr0);
    print_reg(" CR2", regs.cr2);
    print_reg(" CR3", regs.cr3);
    VGA::println();
    Serial::write("\r\n");
    
    VGA::println();
}

void Panic::print_stack_trace() {
    VGA::set_color(Color::Yellow);
    VGA::println("  Stack Trace:");
    VGA::set_color(Color::LightCyan);
    Serial::write("  Stack Trace:\r\n");
    
    // Get current EBP
    u32 ebp;
    asm volatile("mov %%ebp, %0" : "=r"(ebp));
    
    // Walk the stack frames
    int frame = 0;
    while (ebp && frame < 10) {
        u32* frame_ptr = reinterpret_cast<u32*>(ebp);
        
        // frame_ptr[0] = saved EBP
        // frame_ptr[1] = return address
        u32 ret_addr = frame_ptr[1];
        
        if (ret_addr == 0) break;
        
        VGA::print("    #");
        VGA::print_dec(frame);
        VGA::print(": 0x");
        VGA::print_hex(ret_addr);
        VGA::println();
        
        Serial::write("    #");
        char num[4];
        num[0] = '0' + frame;
        num[1] = '\0';
        Serial::write(num);
        Serial::write(": 0x");
        Serial::write_hex(ret_addr);
        Serial::write("\r\n");
        
        // Move to previous frame
        ebp = frame_ptr[0];
        frame++;
    }
    
    VGA::println();
}

Registers Panic::capture_registers() {
    Registers regs;
    
    // Capture general purpose registers
    asm volatile(
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        : "=m"(regs.eax), "=m"(regs.ebx), "=m"(regs.ecx), "=m"(regs.edx)
    );
    
    asm volatile(
        "mov %%esi, %0\n"
        "mov %%edi, %1\n"
        "mov %%ebp, %2\n"
        "mov %%esp, %3\n"
        : "=m"(regs.esi), "=m"(regs.edi), "=m"(regs.ebp), "=m"(regs.esp)
    );
    
    // Get EIP (return address approximation)
    regs.eip = 0;  // Can't easily get current EIP
    
    // Get EFLAGS
    asm volatile("pushf; pop %0" : "=r"(regs.eflags));
    
    // Segment registers
    asm volatile("mov %%cs, %0" : "=r"(regs.cs));
    asm volatile("mov %%ds, %0" : "=r"(regs.ds));
    asm volatile("mov %%es, %0" : "=r"(regs.es));
    asm volatile("mov %%fs, %0" : "=r"(regs.fs));
    asm volatile("mov %%gs, %0" : "=r"(regs.gs));
    asm volatile("mov %%ss, %0" : "=r"(regs.ss));
    
    // Control registers
    asm volatile("mov %%cr0, %0" : "=r"(regs.cr0));
    asm volatile("mov %%cr2, %0" : "=r"(regs.cr2));
    asm volatile("mov %%cr3, %0" : "=r"(regs.cr3));
    // CR4 may not be available on all CPUs
    regs.cr4 = 0;
    
    return regs;
}

[[noreturn]] void Panic::halt() {
    VGA::set_color(Color::White, Color::Red);
    VGA::println("  System halted. Please restart your computer.");
    Serial::write("  System halted. Please restart your computer.\r\n");
    
    // Halt forever
    while (true) {
        asm volatile("cli; hlt");
    }
}

} // namespace bolt::panic
