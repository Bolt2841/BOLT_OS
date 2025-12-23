#pragma once
/* ===========================================================================
 * BOLT OS - Task/Process Management
 * ===========================================================================
 * Task Control Block (TCB) and task state management for multitasking.
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::sched {

// Maximum number of tasks
constexpr u32 MAX_TASKS = 64;

// Stack size for each task (4KB)
constexpr u32 TASK_STACK_SIZE = 4096;

// Task states
enum class TaskState : u8 {
    Ready,          // Ready to run
    Running,        // Currently running
    Blocked,        // Waiting for I/O or event
    Sleeping,       // Sleeping for a duration
    Zombie,         // Terminated, waiting for cleanup
    Dead            // Slot available for reuse
};

// Task priority levels
enum class Priority : u8 {
    Idle = 0,       // Lowest priority (idle task)
    Low = 1,
    Normal = 2,
    High = 3,
    Realtime = 4    // Highest priority
};

// CPU register context saved during context switch
struct TaskContext {
    // Pushed by pusha
    u32 edi;
    u32 esi;
    u32 ebp;
    u32 esp_dummy;  // ESP from pusha (ignored)
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;
    
    // Segment registers
    u32 ds;
    u32 es;
    u32 fs;
    u32 gs;
    
    // Pushed by CPU/iret
    u32 eip;
    u32 cs;
    u32 eflags;
    u32 esp;        // Only for privilege level change
    u32 ss;         // Only for privilege level change
} __attribute__((packed));

// Task Control Block (TCB)
struct Task {
    // Task identification
    u32 pid;                    // Process ID
    u32 ppid;                   // Parent process ID
    char name[32];              // Task name
    
    // State
    TaskState state;
    Priority priority;
    
    // Scheduling
    u32 time_slice;             // Remaining time slice (ticks)
    u32 total_time;             // Total CPU time used (ticks)
    u32 wake_time;              // Tick count to wake up (if sleeping)
    
    // Memory
    u32 stack_base;             // Bottom of stack
    u32 stack_top;              // Top of stack (initial ESP)
    u32 esp;                    // Current stack pointer
    u32 page_directory;         // Page directory (for process isolation)
    
    // Links for task list
    Task* next;
    Task* prev;
    
    // Wait queue for blocking operations
    Task* wait_queue_next;
    
    // Exit status
    i32 exit_code;
};

// Task statistics
struct TaskStats {
    u32 total_tasks;
    u32 running_tasks;
    u32 ready_tasks;
    u32 blocked_tasks;
    u32 sleeping_tasks;
    u32 context_switches;
};

class TaskManager {
public:
    // Initialize the task manager
    static void init();
    
    // Create a new task
    // Returns PID or 0 on failure
    static u32 create(const char* name, void (*entry)(), Priority priority = Priority::Normal);
    
    // Terminate current task
    static void exit(i32 exit_code);
    
    // Terminate a specific task
    static bool kill(u32 pid);
    
    // Get current running task
    static Task* current() { return current_task; }
    
    // Get task by PID
    static Task* get_task(u32 pid);
    
    // Block current task (e.g., waiting for I/O)
    static void block();
    
    // Unblock a task
    static void unblock(u32 pid);
    
    // Sleep for a number of milliseconds
    static void sleep(u32 ms);
    
    // Yield CPU to other tasks (cooperative)
    static void yield();
    
    // Get task statistics
    static TaskStats get_stats();
    
    // Get list of all tasks (for ps command)
    static Task* get_task_list() { return task_list; }
    
    // Called by timer interrupt for preemptive scheduling
    static void tick();
    
    // Perform context switch to next ready task
    static void schedule();
    
    // Get next PID
    static u32 get_next_pid() { return next_pid; }

private:
    // Allocate a task slot
    static Task* alloc_task();
    
    // Free a task slot
    static void free_task(Task* task);
    
    // Set up initial stack for new task
    static void setup_stack(Task* task, void (*entry)());
    
    // Pick next task to run
    static Task* pick_next();
    
    // Idle task (runs when no other task is ready)
    static void idle_task();
    
    // Task array
    static Task tasks[MAX_TASKS];
    
    // Task list head (circular doubly-linked list of ready tasks)
    static Task* task_list;
    
    // Currently running task
    static Task* current_task;
    
    // Next available PID
    static u32 next_pid;
    
    // Statistics
    static TaskStats stats;
    
    // Default time slice (in timer ticks)
    static constexpr u32 DEFAULT_TIME_SLICE = 10;  // ~100ms at 100Hz
};

} // namespace bolt::sched
