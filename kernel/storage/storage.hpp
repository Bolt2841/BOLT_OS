/* ===========================================================================
 * BOLT OS - Storage Subsystem
 * 
 * Main header for the storage subsystem - includes all components.
 * =========================================================================== */

#ifndef BOLT_STORAGE_HPP
#define BOLT_STORAGE_HPP

// Core storage components
#include "block.hpp"
#include "partition.hpp"
#include "vfs.hpp"
#include "detect.hpp"
#include "ramfs.hpp"
#include "ata_device.hpp"

namespace bolt::storage {

// ===========================================================================
// Storage Subsystem Initialization
// ===========================================================================

enum class StorageInitResult {
    Success,            // All systems operational
    DegradedRAMFS,      // Fell back to RAMFS only
    PartialSuccess,     // Some devices available
    Failed              // Complete failure
};

class Storage {
public:
    // Initialize the entire storage subsystem
    // This is the main entry point - call once during kernel init
    static StorageInitResult init();
    
    // Check if storage is ready
    static bool is_ready();
    
    // Get initialization result
    static StorageInitResult get_init_result();
    
    // Get root filesystem
    static Filesystem* get_root_fs();
    
    // Check if running on RAMFS fallback
    static bool is_ramfs_fallback();
    
    // Print storage status
    static void print_status();
    
    // Sync all filesystems
    static VFSResult sync();
    
    // Mount a device at a path
    static VFSResult mount(const char* device, const char* path);
    
    // Unmount a path
    static VFSResult unmount(const char* path);
    
private:
    // Internal initialization phases
    static bool init_block_devices();
    static bool init_partitions();
    static bool init_vfs();
    static bool mount_root();
    static bool mount_ramfs_fallback();
    
    // State
    static StorageInitResult init_result;
    static bool initialized;
    static bool using_ramfs;
};

// ===========================================================================
// Convenience Functions
// ===========================================================================

// Quick file operations (use VFS internally)
bool file_exists(const char* path);
bool is_directory(const char* path);
u64  file_size(const char* path);

// Read entire file into buffer (must be pre-allocated)
// Returns bytes read, or 0 on error
u64 read_file(const char* path, void* buffer, u64 max_size);

// Write buffer to file (creates if doesn't exist)
// Returns bytes written, or 0 on error
u64 write_file(const char* path, const void* buffer, u64 size);

// Create directory
bool create_directory(const char* path);

// Delete file
bool delete_file(const char* path);

// List directory contents (callback-based)
using DirCallback = void (*)(const FileInfo& info, void* user_data);
bool list_directory(const char* path, DirCallback callback, void* user_data);

} // namespace bolt::storage

#endif // BOLT_STORAGE_HPP
