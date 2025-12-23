/* ===========================================================================
 * BOLT OS - Memory Operations Implementation
 * =========================================================================== */

#include "heap.hpp"

namespace bolt::mem {

Heap::Block* Heap::head = nullptr;
u32 Heap::heap_used = 0;
u32 Heap::heap_size = 0;
u32 Heap::total_system_memory = 0;

void Heap::init() {
    // Read total memory from bootloader (stored at 0x500)
    total_system_memory = *reinterpret_cast<volatile u32*>(MEMINFO_ADDR);
    
    // Sanity check - if detection failed, assume 16MB
    if (total_system_memory < 0x200000 || total_system_memory > 0x40000000) {
        total_system_memory = 0x1000000;  // 16MB fallback
    }
    
    // Calculate heap size: total memory - 1MB (for kernel, stack, etc.)
    // Leave some room at the top for safety
    heap_size = total_system_memory - HEAP_START - 0x10000;  // Reserve 64KB at top
    
    // Clamp to reasonable limits
    if (heap_size < MIN_HEAP_SIZE) heap_size = MIN_HEAP_SIZE;
    if (heap_size > MAX_HEAP_SIZE) heap_size = MAX_HEAP_SIZE;
    
    // Initialize heap
    head = reinterpret_cast<Block*>(HEAP_START);
    head->size = heap_size - sizeof(Block);
    head->used = false;
    head->next = nullptr;
    heap_used = 0;
}

void* Heap::alloc(usize size) {
    // Align to 8 bytes
    size = (size + 7) & ~7;
    
    Block* block = head;
    while (block) {
        if (!block->used && block->size >= size) {
            // Split block if significantly larger
            if (block->size > size + sizeof(Block) + 32) {
                Block* new_block = reinterpret_cast<Block*>(
                    reinterpret_cast<u8*>(block) + sizeof(Block) + size
                );
                new_block->size = block->size - size - sizeof(Block);
                new_block->used = false;
                new_block->next = block->next;
                
                block->size = size;
                block->next = new_block;
            }
            
            block->used = true;
            heap_used += block->size;
            return reinterpret_cast<u8*>(block) + sizeof(Block);
        }
        block = block->next;
    }
    return nullptr;
}

void* Heap::alloc_zeroed(usize size) {
    void* ptr = alloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void Heap::free(void* ptr) {
    if (!ptr) return;
    
    Block* block = reinterpret_cast<Block*>(
        static_cast<u8*>(ptr) - sizeof(Block)
    );
    block->used = false;
    heap_used -= block->size;
    
    // Coalesce with next block if free
    if (block->next && !block->next->used) {
        block->size += sizeof(Block) + block->next->size;
        block->next = block->next->next;
    }
}

usize Heap::get_used() { return heap_used; }
usize Heap::get_free() { return heap_size - heap_used; }
usize Heap::get_total() { return heap_size; }
usize Heap::get_total_system_memory() { return total_system_memory; }

} // namespace bolt::mem

// Global new/delete operators
void* operator new(bolt::usize size) {
    return bolt::mem::Heap::alloc(size);
}

void* operator new[](bolt::usize size) {
    return bolt::mem::Heap::alloc(size);
}

void operator delete(void* ptr) noexcept {
    bolt::mem::Heap::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    bolt::mem::Heap::free(ptr);
}

void operator delete(void* ptr, bolt::usize) noexcept {
    bolt::mem::Heap::free(ptr);
}

void operator delete[](void* ptr, bolt::usize) noexcept {
    bolt::mem::Heap::free(ptr);
}
