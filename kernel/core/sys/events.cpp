/* ===========================================================================
 * BOLT OS - Event System Implementation
 * =========================================================================== */

#include "events.hpp"
#include "../memory/heap.hpp"

namespace bolt::events {

Event EventQueue::queue[MAX_EVENTS];
usize EventQueue::head = 0;
usize EventQueue::tail = 0;
usize EventQueue::count = 0;

void EventQueue::init() {
    head = 0;
    tail = 0;
    count = 0;
    mem::memset(queue, 0, sizeof(queue));
}

bool EventQueue::push(const Event& event) {
    if (count >= MAX_EVENTS) {
        return false;  // Queue full
    }
    
    queue[tail] = event;
    tail = (tail + 1) % MAX_EVENTS;
    count++;
    
    return true;
}

bool EventQueue::poll(Event& event) {
    if (count == 0) {
        return false;
    }
    
    event = queue[head];
    head = (head + 1) % MAX_EVENTS;
    count--;
    
    return true;
}

bool EventQueue::has_events() {
    return count > 0;
}

void EventQueue::clear() {
    head = 0;
    tail = 0;
    count = 0;
}

Event EventQueue::wait() {
    Event event;
    event.type = EventType::None;
    
    while (!poll(event)) {
        asm volatile("hlt");
    }
    
    return event;
}

Event make_key_event(EventType type, char ascii, u8 scancode, bool shift, bool ctrl, bool alt) {
    Event e;
    e.type = type;
    e.key.ascii = ascii;
    e.key.scancode = scancode;
    e.key.shift = shift;
    e.key.ctrl = ctrl;
    e.key.alt = alt;
    return e;
}

Event make_mouse_move_event(i32 x, i32 y, i32 dx, i32 dy) {
    Event e;
    e.type = EventType::MouseMove;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.dx = dx;
    e.mouse.dy = dy;
    e.mouse.button = MouseButton::None;
    return e;
}

Event make_mouse_button_event(EventType type, MouseButton button, i32 x, i32 y) {
    Event e;
    e.type = type;
    e.mouse.x = x;
    e.mouse.y = y;
    e.mouse.dx = 0;
    e.mouse.dy = 0;
    e.mouse.button = button;
    return e;
}

} // namespace bolt::events
