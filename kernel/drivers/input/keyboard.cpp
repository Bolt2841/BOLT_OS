/* ===========================================================================
 * BOLT OS - PS/2 Keyboard Driver Implementation (Extended)
 * =========================================================================== */

#include "keyboard.hpp"
#include "../../core/sys/io.hpp"
#include "../serial/serial.hpp"

namespace bolt::drivers {

bool Keyboard::shift_pressed = false;
bool Keyboard::ctrl_pressed = false;
bool Keyboard::alt_pressed = false;
bool Keyboard::caps_lock = false;
bool Keyboard::extended_key = false;

// US QWERTY scancode table
static const char scancode_table[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_table_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

void Keyboard::init() {
    DBG("KBD", "Initializing PS/2 keyboard...");
    
    // Wait for controller to be ready
    u32 timeout = 10000;
    while ((io::inb(STATUS_PORT) & 0x02) && timeout--);
    
    if (timeout == 0) {
        DBG_WARN("KBD", "Controller busy timeout - continuing anyway");
    }
    
    // Drain keyboard buffer
    timeout = 100;
    while ((io::inb(STATUS_PORT) & 0x01) && timeout--) {
        io::inb(DATA_PORT);
    }
    
    shift_pressed = false;
    ctrl_pressed = false;
    alt_pressed = false;
    caps_lock = false;
    extended_key = false;
    
    DBG_OK("KBD", "PS/2 keyboard ready");
}

bool Keyboard::has_key() {
    u8 status = io::inb(STATUS_PORT);
    // Bit 0 = data available
    // Bit 5 = data is from mouse (AUX), not keyboard
    // Only return true if data is available AND it's NOT from mouse
    return (status & 0x01) && !(status & 0x20);
}

// Drain any pending mouse data so it doesn't block keyboard input
void Keyboard::drain_mouse_data() {
    u8 status = io::inb(STATUS_PORT);
    // If data available AND from mouse, read and discard it
    while ((status & 0x01) && (status & 0x20)) {
        io::inb(DATA_PORT);  // Discard mouse data
        status = io::inb(STATUS_PORT);
    }
}

char Keyboard::getchar() {
    char c;
    while ((c = poll()) == 0) {
        // Drain any mouse data that might be blocking
        drain_mouse_data();
        // Small pause to avoid hammering the port
        for (volatile int i = 0; i < 1000; i++);
    }
    return c;
}

KeyEvent Keyboard::get_event() {
    KeyEvent ev;
    do {
        // Drain any mouse data that might be blocking
        drain_mouse_data();
        // Small pause
        for (volatile int i = 0; i < 1000; i++);
        ev = poll_event();
    } while (ev.ascii == 0 && ev.special == SpecialKey::None);
    return ev;
}

char Keyboard::poll() {
    KeyEvent ev = poll_event();
    return ev.ascii;
}

KeyEvent Keyboard::poll_event() {
    KeyEvent ev = {0, SpecialKey::None, shift_pressed, ctrl_pressed, alt_pressed};
    
    if (!has_key()) return ev;
    
    u8 scancode = io::inb(DATA_PORT);
    
    // Extended key prefix
    if (scancode == 0xE0) {
        extended_key = true;
        return ev;
    }
    
    // Key release
    if (scancode & 0x80) {
        u8 released = scancode & 0x7F;
        
        if (extended_key) {
            extended_key = false;
            return ev;
        }
        
        switch (released) {
            case 0x2A: case 0x36: shift_pressed = false; break;  // Shift
            case 0x1D: ctrl_pressed = false; break;               // Ctrl
            case 0x38: alt_pressed = false; break;                // Alt
        }
        return ev;
    }
    
    // Handle extended keys (arrow keys, etc.)
    if (extended_key) {
        extended_key = false;
        ev.special = scancode_to_special(scancode);
        return ev;
    }
    
    // Handle modifier keys
    switch (scancode) {
        case 0x2A: case 0x36: shift_pressed = true; return ev;   // Shift
        case 0x1D: ctrl_pressed = true; return ev;                // Ctrl
        case 0x38: alt_pressed = true; return ev;                 // Alt
        case 0x3A: caps_lock = !caps_lock; return ev;             // Caps Lock
    }
    
    // Function keys F1-F10
    if (scancode >= 0x3B && scancode <= 0x44) {
        ev.special = static_cast<SpecialKey>(
            static_cast<u8>(SpecialKey::F1) + (scancode - 0x3B)
        );
        return ev;
    }
    
    // F11, F12
    if (scancode == 0x57) { ev.special = SpecialKey::F11; return ev; }
    if (scancode == 0x58) { ev.special = SpecialKey::F12; return ev; }
    
    // Regular ASCII key
    ev.ascii = scancode_to_char(scancode);
    ev.shift = shift_pressed;
    ev.ctrl = ctrl_pressed;
    ev.alt = alt_pressed;
    
    // Debug log when ctrl is held
    if (ctrl_pressed) {
        Serial::log("KBD", LogType::Debug, "Ctrl+key: ascii=");
        char buf[4] = {ev.ascii, 0, 0, 0};
        Serial::log("KBD", LogType::Debug, buf);
    }
    
    return ev;
}

SpecialKey Keyboard::scancode_to_special(u8 scancode) {
    switch (scancode) {
        case 0x48: return SpecialKey::Up;
        case 0x50: return SpecialKey::Down;
        case 0x4B: return SpecialKey::Left;
        case 0x4D: return SpecialKey::Right;
        case 0x47: return SpecialKey::Home;
        case 0x4F: return SpecialKey::End;
        case 0x49: return SpecialKey::PageUp;
        case 0x51: return SpecialKey::PageDown;
        case 0x53: return SpecialKey::Delete;
        case 0x52: return SpecialKey::Insert;
        default: return SpecialKey::None;
    }
}

char Keyboard::scancode_to_char(u8 scancode) {
    if (scancode >= sizeof(scancode_table)) return 0;
    
    char c;
    if (shift_pressed) {
        c = scancode_table_shift[scancode];
    } else {
        c = scancode_table[scancode];
    }
    
    // Apply caps lock to letters
    if (caps_lock && c >= 'a' && c <= 'z') {
        c -= 32;
    } else if (caps_lock && c >= 'A' && c <= 'Z') {
        c += 32;
    }
    
    return c;
}

} // namespace bolt::drivers
