#pragma once
/* ===========================================================================
 * BOLT OS - Event System
 * ===========================================================================
 * Unified event handling for keyboard, mouse, and system events
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::events {

enum class EventType : u8 {
    None = 0,
    KeyPress,
    KeyRelease,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseScroll,
    Timer,
    Quit
};

enum class MouseButton : u8 {
    None = 0,
    Left,
    Right,
    Middle
};

struct KeyEventData {
    char ascii;
    u8 scancode;
    bool shift;
    bool ctrl;
    bool alt;
};

struct MouseEventData {
    i32 x;
    i32 y;
    i32 dx;
    i32 dy;
    MouseButton button;
};

struct Event {
    EventType type;
    union {
        KeyEventData key;
        MouseEventData mouse;
        u32 timer_ticks;
    };
};

class EventQueue {
public:
    static constexpr usize MAX_EVENTS = 64;
    
    static void init();
    
    // Add event to queue
    static bool push(const Event& event);
    
    // Get next event (returns false if queue empty)
    static bool poll(Event& event);
    
    // Check if events are available
    static bool has_events();
    
    // Clear all events
    static void clear();
    
    // Block until event available
    static Event wait();
    
private:
    static Event queue[MAX_EVENTS];
    static usize head;
    static usize tail;
    static usize count;
};

// Helper functions to create events
Event make_key_event(EventType type, char ascii, u8 scancode, bool shift, bool ctrl, bool alt);
Event make_mouse_move_event(i32 x, i32 y, i32 dx, i32 dy);
Event make_mouse_button_event(EventType type, MouseButton button, i32 x, i32 y);

} // namespace bolt::events
