/* ===========================================================================
 * BOLT OS - PS/2 Mouse Driver Implementation
 * =========================================================================== */

#include "mouse.hpp"
#include "../../core/sys/io.hpp"
#include "../../core/arch/idt.hpp"
#include "../serial/serial.hpp"

namespace bolt::drivers {

MouseState Mouse::state = {0, 0, false, false, false, 0};
u8 Mouse::packet[4] = {0, 0, 0, 0};
u8 Mouse::packet_index = 0;
u8 Mouse::packet_size = 3;
bool Mouse::has_wheel = false;
i32 Mouse::screen_width = 320;   // Default (updated by set_bounds)
i32 Mouse::screen_height = 200;

void Mouse::wait_write() {
    u32 timeout = 100000;
    while (timeout--) {
        if ((io::inb(STATUS_PORT) & 0x02) == 0) return;
    }
    // Timeout is expected on some systems, continue anyway
}

void Mouse::wait_read() {
    u32 timeout = 100000;
    while (timeout--) {
        if (io::inb(STATUS_PORT) & 0x01) return;
    }
}

void Mouse::write_command(u8 cmd) {
    wait_write();
    io::outb(COMMAND_PORT, cmd);
}

void Mouse::write_data(u8 data) {
    wait_write();
    io::outb(DATA_PORT, data);
}

u8 Mouse::read_data() {
    wait_read();
    return io::inb(DATA_PORT);
}

void Mouse::init() {
    DBG("MOUSE", "Initializing PS/2 mouse...");
    
    // Enable auxiliary device (mouse)
    write_command(0xA8);
    
    // Enable interrupts
    write_command(0x20);    // Get compaq status
    u8 status = read_data();
    status |= 0x02;         // Enable IRQ12
    status &= ~0x20;        // Enable mouse clock
    write_command(0x60);    // Set compaq status
    write_data(status);
    
    // Tell mouse to use default settings
    write_command(0xD4);
    write_data(0xF6);
    read_data();  // ACK
    
    // Enable scroll wheel (IntelliMouse protocol)
    // Send magic sequence: set sample rate 200, 100, 80
    write_command(0xD4); write_data(0xF3); read_data(); // Set sample rate
    write_command(0xD4); write_data(200);  read_data();
    write_command(0xD4); write_data(0xF3); read_data();
    write_command(0xD4); write_data(100);  read_data();
    write_command(0xD4); write_data(0xF3); read_data();
    write_command(0xD4); write_data(80);   read_data();
    
    // Get device ID
    write_command(0xD4);
    write_data(0xF2);
    read_data();  // ACK
    u8 device_id = read_data();
    
    if (device_id == 3 || device_id == 4) {
        has_wheel = true;
        packet_size = 4;
        Serial::write("[MOUSE] Scroll wheel detected (ID=");
        Serial::write_dec(device_id);
        Serial::writeln(")");
    } else {
        has_wheel = false;
        packet_size = 3;
        DBG("MOUSE", "Standard mouse (no wheel)");
    }
    
    // Enable packet streaming
    write_command(0xD4);
    write_data(0xF4);
    read_data();  // ACK
    
    // Set initial position to center
    state.x = screen_width / 2;
    state.y = screen_height / 2;
    state.scroll_delta = 0;
    
    // Register interrupt handler
    IDT::register_handler(IDT::IRQ_MOUSE, [](InterruptFrame*) {
        Mouse::handle_interrupt();
    });
    
    DBG_OK("MOUSE", "PS/2 mouse ready");
}

// Poll for mouse data - check if data is available and process it
void Mouse::poll() {
    u8 status = io::inb(STATUS_PORT);
    
    // Check if data available (bit 0) and from mouse (bit 5)
    while ((status & 0x01) && (status & 0x20)) {
        u8 data = io::inb(DATA_PORT);
        
        packet[packet_index++] = data;
        
        if (packet_index >= packet_size) {
            packet_index = 0;
            
            u8 flags = packet[0];
            
            // Check for valid packet (bit 3 should always be set)
            if (flags & 0x08) {
                i32 dx = packet[1];
                i32 dy = packet[2];
                
                if (flags & 0x10) dx |= 0xFFFFFF00;
                if (flags & 0x20) dy |= 0xFFFFFF00;
                
                state.x += dx;
                state.y -= dy;
                
                if (state.x < 0) state.x = 0;
                if (state.y < 0) state.y = 0;
                if (state.x >= screen_width) state.x = screen_width - 1;
                if (state.y >= screen_height) state.y = screen_height - 1;
                
                state.left_button   = (flags & 0x01) != 0;
                state.right_button  = (flags & 0x02) != 0;
                state.middle_button = (flags & 0x04) != 0;
                
                if (has_wheel && packet_size == 4) {
                    i8 scroll = static_cast<i8>(packet[3]);
                    state.scroll_delta += scroll;
                }
            }
        }
        
        status = io::inb(STATUS_PORT);
    }
}

void Mouse::handle_interrupt() {
    u8 data = io::inb(DATA_PORT);
    
    packet[packet_index++] = data;
    
    if (packet_index >= packet_size) {
        packet_index = 0;
        
        // Parse packet
        u8 flags = packet[0];
        
        // Check for valid packet (bit 3 should always be set)
        if (!(flags & 0x08)) {
            return;
        }
        
        // Get movement deltas
        i32 dx = packet[1];
        i32 dy = packet[2];
        
        // Handle sign extension
        if (flags & 0x10) dx |= 0xFFFFFF00;
        if (flags & 0x20) dy |= 0xFFFFFF00;
        
        // Update position
        state.x += dx;
        state.y -= dy;  // Y is inverted
        
        // Clamp to screen bounds
        if (state.x < 0) state.x = 0;
        if (state.y < 0) state.y = 0;
        if (state.x >= screen_width) state.x = screen_width - 1;
        if (state.y >= screen_height) state.y = screen_height - 1;
        
        // Update buttons
        state.left_button   = (flags & 0x01) != 0;
        state.right_button  = (flags & 0x02) != 0;
        state.middle_button = (flags & 0x04) != 0;
        
        // Handle scroll wheel (4th byte)
        if (has_wheel && packet_size == 4) {
            i8 scroll = static_cast<i8>(packet[3]);
            state.scroll_delta += scroll;  // Accumulate scroll
        }
    }
}

void Mouse::set_bounds(i32 width, i32 height) {
    screen_width = width;
    screen_height = height;
    
    // Re-center mouse
    state.x = width / 2;
    state.y = height / 2;
}

MouseState Mouse::get_state() {
    return state;
}

i32 Mouse::get_x() {
    return state.x;
}

i32 Mouse::get_y() {
    return state.y;
}

bool Mouse::left_pressed() {
    return state.left_button;
}

bool Mouse::right_pressed() {
    return state.right_button;
}

bool Mouse::middle_pressed() {
    return state.middle_button;
}

i8 Mouse::get_scroll_delta() {
    i8 delta = state.scroll_delta;
    state.scroll_delta = 0;  // Clear after reading
    return delta;
}

} // namespace bolt::drivers
