/* ===========================================================================
 * BOLT OS - Task/Process Management Implementation
 * ===========================================================================
 * Cooperative and preemptive multitasking support.
 * =========================================================================== */

#include "task.hpp"
#include "../memory/pmm.hpp"
#include "../memory/heap.hpp"
#include "../sys/log.hpp"
#include "../arch/idt.hpp"
#include "../../drivers/timer/pit.hpp"
#include "../../lib/string.hpp"

namespace bolt::sched {

using namespace bolt::mem;
using namespace bolt::log;
using namespace bolt::drivers;

// Static member definitions
Task TaskManager::tasks[MAX_TASKS];
Task* TaskManager::task_list = nullptr;
Task* TaskManager::current_task = nullptr;
u32 TaskManager::next_pid = 1;
TaskStats TaskManager::stats = {0, 0, 0, 0, 0, 0};

// External assembly function for context switch
extern "C" void switch_context(u32* old_esp, u32 new_esp);

void TaskManager::init() {
    // Clear all task slots
    for (u32 i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TaskState::Dead;
        tasks[i].pid = 0;
        tasks[i].next = nullptr;
        tasks[i].prev = nullptr;
    }
    
    // Create the kernel/idle task (PID 0)
    // This represents the initial kernel thread that's already running
    Task* kernel_task = &tasks[0];
    kernel_task->pid = 0;
    kernel_task->ppid = 0;
    str::cpy(kernel_task->name, "kernel");
    kernel_task->state = TaskState::Running;
    kernel_task->priority = Priority::Idle;
    kernel_task->time_slice = DEFAULT_TIME_SLICE;
    kernel_task->total_time = 0;
    kernel_task->stack_base = 0;  // Using existing kernel stack
    kernel_task->stack_top = 0;
    kernel_task->esp = 0;  // Will be filled during first context switch
    kernel_task->page_directory = 0;  // Using kernel page directory
    kernel_task->exit_code = 0;
    kernel_task->next = kernel_task;  // Circular list
    kernel_task->prev = kernel_task;
    
    task_list = kernel_task;
    current_task = kernel_task;
    
    stats.total_tasks = 1;
    stats.running_tasks = 1;
    
    LOG_INFO("Task manager initialized");
}

Task* TaskManager::alloc_task() {
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TaskState::Dead) {
            return &tasks[i];
        }
    }
    return nullptr;
}

void TaskManager::free_task(Task* task) {
    // Free the stack if we allocated one
    if (task->stack_base != 0) {
        // For now we use Heap, later could use PMM pages
        Heap::free(reinterpret_cast<void*>(task->stack_base));
    }
    
    // Remove from task list
    if (task->next == task) {
        // Only task in list (shouldn't happen - kernel task should stay)
        task_list = nullptr;
    } else {
        task->prev->next = task->next;
        task->next->prev = task->prev;
        if (task_list == task) {
            task_list = task->next;
        }
    }
    
    task->state = TaskState::Dead;
    task->pid = 0;
    
    stats.total_tasks--;
}

void TaskManager::setup_stack(Task* task, void (*entry)()) {
    // Allocate stack
    void* stack = Heap::alloc(TASK_STACK_SIZE);
    if (!stack) {
        return;
    }
    
    task->stack_base = reinterpret_cast<u32>(stack);
    task->stack_top = task->stack_base + TASK_STACK_SIZE;
    
    // Set up initial stack frame for context switch
    // The stack needs to look like it was interrupted and saved
    u32* sp = reinterpret_cast<u32*>(task->stack_top);
    
    // Push a fake "return address" for when the task exits
    // This points to exit() so tasks that return from their entry point exit cleanly
    *(--sp) = reinterpret_cast<u32>(TaskManager::exit);  // Return address (will call exit(0))
    
    // Push the entry point (where execution will start)
    *(--sp) = reinterpret_cast<u32>(entry);  // EIP for iret or ret
    
    // Push EFLAGS (interrupts enabled)
    *(--sp) = 0x202;  // EFLAGS: IF=1 (interrupts enabled)
    
    // Push segment registers (all kernel segments for now)
    *(--sp) = 0x10;  // GS
    *(--sp) = 0x10;  // FS  
    *(--sp) = 0x10;  // ES
    *(--sp) = 0x10;  // DS
    
    // Push general registers (pusha order reversed)
    *(--sp) = 0;     // EAX
    *(--sp) = 0;     // ECX
    *(--sp) = 0;     // EDX
    *(--sp) = 0;     // EBX
    *(--sp) = 0;     // ESP (ignored)
    *(--sp) = 0;     // EBP
    *(--sp) = 0;     // ESI
    *(--sp) = 0;     // EDI
    
    task->esp = reinterpret_cast<u32>(sp);
}

u32 TaskManager::create(const char* name, void (*entry)(), Priority priority) {
    Task* task = alloc_task();
    if (!task) {
        LOG_ERROR("Failed to allocate task slot");
        return 0;
    }
    
    // Initialize task
    task->pid = next_pid++;
    task->ppid = current_task ? current_task->pid : 0;
    str::cpy(task->name, name);
    task->state = TaskState::Ready;
    task->priority = priority;
    task->time_slice = DEFAULT_TIME_SLICE;
    task->total_time = 0;
    task->wake_time = 0;
    task->page_directory = 0;  // Share kernel page directory for now
    task->exit_code = 0;
    task->wait_queue_next = nullptr;
    
    // Set up the stack
    setup_stack(task, entry);
    
    if (task->stack_base == 0) {
        LOG_ERROR("Failed to allocate task stack");
        task->state = TaskState::Dead;
        return 0;
    }
    
    // Add to task list (insert after current)
    if (task_list) {
        task->next = task_list->next;
        task->prev = task_list;
        task_list->next->prev = task;
        task_list->next = task;
    } else {
        task->next = task;
        task->prev = task;
        task_list = task;
    }
    
    stats.total_tasks++;
    stats.ready_tasks++;
    
    LOGF_DEBUG("Created task '%s' (PID %u)", name, task->pid);
    
    return task->pid;
}

void TaskManager::exit(i32 exit_code) {
    if (!current_task || current_task->pid == 0) {
        // Can't exit kernel task
        return;
    }
    
    LOGF_DEBUG("Task '%s' (PID %u) exiting with code %d", 
               current_task->name, current_task->pid, exit_code);
    
    current_task->exit_code = exit_code;
    current_task->state = TaskState::Zombie;
    
    stats.running_tasks--;
    
    // Switch to another task
    schedule();
    
    // Should never reach here
    while (true) {
        asm volatile("hlt");
    }
}

bool TaskManager::kill(u32 pid) {
    if (pid == 0) {
        return false;  // Can't kill kernel task
    }
    
    Task* task = get_task(pid);
    if (!task || task->state == TaskState::Dead) {
        return false;
    }
    
    LOGF_DEBUG("Killing task '%s' (PID %u)", task->name, pid);
    
    // Update stats based on old state
    switch (task->state) {
        case TaskState::Running:
            stats.running_tasks--;
            break;
        case TaskState::Ready:
            stats.ready_tasks--;
            break;
        case TaskState::Blocked:
            stats.blocked_tasks--;
            break;
        case TaskState::Sleeping:
            stats.sleeping_tasks--;
            break;
        default:
            break;
    }
    
    if (task == current_task) {
        task->state = TaskState::Zombie;
        schedule();
    } else {
        free_task(task);
    }
    
    return true;
}

Task* TaskManager::get_task(u32 pid) {
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TaskState::Dead) {
            return &tasks[i];
        }
    }
    return nullptr;
}

void TaskManager::block() {
    if (!current_task) return;
    
    current_task->state = TaskState::Blocked;
    stats.running_tasks--;
    stats.blocked_tasks++;
    
    schedule();
}

void TaskManager::unblock(u32 pid) {
    Task* task = get_task(pid);
    if (task && task->state == TaskState::Blocked) {
        task->state = TaskState::Ready;
        task->time_slice = DEFAULT_TIME_SLICE;
        stats.blocked_tasks--;
        stats.ready_tasks++;
    }
}

void TaskManager::sleep(u32 ms) {
    if (!current_task) return;
    
    // Calculate wake time in ticks (assuming 1000Hz timer = 1 tick per ms)
    u32 ticks = ms;
    if (ticks == 0) ticks = 1;
    
    current_task->wake_time = PIT::get_ticks() + ticks;
    current_task->state = TaskState::Sleeping;
    stats.running_tasks--;
    stats.sleeping_tasks++;
    
    schedule();
}

void TaskManager::yield() {
    if (!current_task) return;
    
    // Reset time slice and give up CPU
    current_task->time_slice = 0;
    schedule();
}

TaskStats TaskManager::get_stats() {
    return stats;
}

void TaskManager::tick() {
    if (!current_task) return;
    
    // Update current task's time
    current_task->total_time++;
    
    // Decrease time slice
    if (current_task->time_slice > 0) {
        current_task->time_slice--;
    }
    
    // Check sleeping tasks
    u32 current_ticks = PIT::get_ticks();
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TaskState::Sleeping) {
            if (current_ticks >= tasks[i].wake_time) {
                tasks[i].state = TaskState::Ready;
                tasks[i].time_slice = DEFAULT_TIME_SLICE;
                stats.sleeping_tasks--;
                stats.ready_tasks++;
            }
        }
    }
    
    // Preempt if time slice expired
    if (current_task->time_slice == 0) {
        schedule();
    }
}

Task* TaskManager::pick_next() {
    if (!task_list) return nullptr;
    
    // Simple round-robin: start from current and find next ready task
    Task* start = current_task ? current_task->next : task_list;
    Task* task = start;
    
    do {
        if (task->state == TaskState::Ready) {
            return task;
        }
        task = task->next;
    } while (task != start);
    
    // No ready task found, return kernel/idle task (PID 0)
    return &tasks[0];
}

void TaskManager::schedule() {
    if (!current_task) return;
    
    // Clean up zombie tasks
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TaskState::Zombie && &tasks[i] != current_task) {
            free_task(&tasks[i]);
        }
    }
    
    // Find next task to run
    Task* next = pick_next();
    
    if (next == current_task && current_task->state == TaskState::Running) {
        // No switch needed
        current_task->time_slice = DEFAULT_TIME_SLICE;
        return;
    }
    
    // Update states
    if (current_task->state == TaskState::Running) {
        current_task->state = TaskState::Ready;
        stats.running_tasks--;
        stats.ready_tasks++;
    }
    
    Task* old_task = current_task;
    current_task = next;
    
    if (next->state == TaskState::Ready) {
        stats.ready_tasks--;
    }
    next->state = TaskState::Running;
    next->time_slice = DEFAULT_TIME_SLICE;
    stats.running_tasks++;
    
    stats.context_switches++;
    
    // Perform the actual context switch
    if (old_task != next) {
        switch_context(&old_task->esp, next->esp);
    }
}

void TaskManager::idle_task() {
    while (true) {
        asm volatile("hlt");  // Wait for interrupt
    }
}

} // namespace bolt::sched
