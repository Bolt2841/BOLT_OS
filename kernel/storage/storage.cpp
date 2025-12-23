/* ===========================================================================
 * BOLT OS - Storage Subsystem Implementation
 * =========================================================================== */

#include "storage.hpp"
#include "../drivers/serial/serial.hpp"
#include "../drivers/video/console.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
StorageInitResult Storage::init_result = StorageInitResult::Failed;
bool Storage::initialized = false;
bool Storage::using_ramfs = false;

// ===========================================================================
// Storage Initialization
// ===========================================================================

StorageInitResult Storage::init() {
    Serial::writeln("");
    Serial::writeln("========================================");
    Serial::writeln("  BOLT OS Storage Subsystem Init");
    Serial::writeln("========================================");
    
    initialized = false;
    using_ramfs = false;
    init_result = StorageInitResult::Failed;
    
    // Phase 1: Initialize managers
    DBG_LOADING("STORAGE", "Phase 1: Initializing managers...");
    BlockDeviceManager::init();
    PartitionManager::init();
    FilesystemRegistry::init();
    FilesystemDetector::init();
    VFS::init();
    
    // Phase 2: Initialize block devices (ATA drives)
    DBG_LOADING("STORAGE", "Phase 2: Detecting block devices...");
    bool has_devices = init_block_devices();
    
    // Phase 3: Scan for partitions
    if (has_devices) {
        DBG_LOADING("STORAGE", "Phase 3: Scanning partitions...");
        init_partitions();
    }
    
    // Phase 4: Initialize VFS and mount root
    DBG_LOADING("STORAGE", "Phase 4: Mounting filesystems...");
    bool root_mounted = init_vfs();
    
    // Determine result
    if (root_mounted && !using_ramfs) {
        init_result = StorageInitResult::Success;
        Serial::writeln("");
        DBG_OK("STORAGE", "Storage subsystem ready (real storage)");
    } else if (root_mounted && using_ramfs) {
        init_result = StorageInitResult::DegradedRAMFS;
        Serial::writeln("");
        DBG_WARN("STORAGE", "Storage subsystem ready (RAMFS fallback)");
    } else if (has_devices) {
        init_result = StorageInitResult::PartialSuccess;
        Serial::writeln("");
        DBG_WARN("STORAGE", "Storage partially initialized");
    } else {
        init_result = StorageInitResult::Failed;
        Serial::writeln("");
        DBG_FAIL("STORAGE", "Storage initialization failed");
    }
    
    initialized = true;
    
    Serial::writeln("========================================");
    Serial::writeln("");
    
    return init_result;
}

bool Storage::init_block_devices() {
    // Create block devices for ATA drives
    ATADeviceManager::create_devices();
    
    // Log results
    u32 total = BlockDeviceManager::get_device_count();
    DBG_DEBUG("STORAGE", total > 0 ? "Block devices detected" : "No block devices found");
    
    return total > 0;
}

bool Storage::init_partitions() {
    u32 device_count = BlockDeviceManager::get_device_count();
    u32 total_parts = 0;
    
    for (u32 i = 0; i < device_count; i++) {
        BlockDevice* dev = BlockDeviceManager::get_device(i);
        if (!dev) continue;
        
        // Skip partition devices (don't scan partitions for partitions)
        if (dev->get_info().type == DeviceType::Partition) continue;
        
        // Scan this device
        u32 parts = PartitionManager::scan_device(dev);
        total_parts += parts;
    }
    
    DBG_DEBUG("STORAGE", total_parts > 0 ? "Partitions found" : "No partitions found");
    
    return true;
}

bool Storage::init_vfs() {
    // Try to mount root filesystem from real storage
    if (!mount_root()) {
        // Fall back to RAMFS
        DBG_WARNING("STORAGE", "No bootable storage, using RAMFS...");
        return mount_ramfs_fallback();
    }
    return true;
}

bool Storage::mount_root() {
    // Strategy:
    // 1. Look for first HDD with FAT32 partition
    // 2. Look for first HDD without partitions but with FAT32
    // 3. Give up
    
    u32 device_count = BlockDeviceManager::get_device_count();
    
    // First pass: Look for FAT32 partition
    DBG_DEBUG("STORAGE", "Looking for FAT32 partition...");
    for (u32 i = 0; i < device_count; i++) {
        BlockDevice* dev = BlockDeviceManager::get_device(i);
        if (!dev) continue;
        
        // Only check partitions
        if (dev->get_info().type != DeviceType::Partition) continue;
        
        // Detect filesystem
        FilesystemType fs_type = FilesystemDetector::detect(dev);
        if (fs_type == FilesystemType::FAT32) {
            DBG_SUCCESS("STORAGE", "Found FAT32 partition");
            
            // Try to mount
            VFSResult result = VFS::mount(dev->get_info().name, "/", fs_type);
            if (result == VFSResult::Success) {
                using_ramfs = false;
                return true;
            }
            DBG_ERROR("STORAGE", "Mount failed");
        }
    }
    
    // Second pass: Look for FAT32 on whole disk (no partitions)
    DBG_DEBUG("STORAGE", "Looking for FAT32 on whole disk...");
    for (u32 i = 0; i < device_count; i++) {
        BlockDevice* dev = BlockDeviceManager::get_device(i);
        if (!dev) continue;
        
        // Only check raw disks
        DeviceType type = dev->get_info().type;
        if (type != DeviceType::ATA_HDD && type != DeviceType::ATA_SSD) continue;
        
        // Detect filesystem
        FilesystemType fs_type = FilesystemDetector::detect(dev);
        
        if (fs_type == FilesystemType::FAT32) {
            DBG_SUCCESS("STORAGE", "Found FAT32 on disk");
            
            VFSResult result = VFS::mount(dev->get_info().name, "/", fs_type);
            if (result == VFSResult::Success) {
                using_ramfs = false;
                return true;
            }
            DBG_ERROR("STORAGE", "Mount failed");
        }
    }
    
    return false;
}

bool Storage::mount_ramfs_fallback() {
    // Create RAMFS instance
    RAMFilesystem* ramfs = new RAMFilesystem();
    if (!ramfs) {
        DBG_FAIL("STORAGE", "Failed to create RAMFS");
        return false;
    }
    
    // Mount as root
    VFSResult result = VFS::mount(ramfs, "/");
    if (result != VFSResult::Success) {
        Serial::write("[STORAGE] RAMFS mount failed: ");
        Serial::writeln(vfs_result_string(result));
        delete ramfs;
        return false;
    }
    
    using_ramfs = true;
    
    // Create essential directories
    VFS::mkdir("/tmp");
    VFS::mkdir("/home");
    VFS::mkdir("/etc");
    VFS::mkdir("/var");
    VFS::mkdir("/mnt");
    
    DBG_OK("STORAGE", "RAMFS mounted as root");
    return true;
}

// ===========================================================================
// Status and Query Functions
// ===========================================================================

bool Storage::is_ready() {
    return initialized && VFS::is_ready();
}

StorageInitResult Storage::get_init_result() {
    return init_result;
}

Filesystem* Storage::get_root_fs() {
    return VFS::get_root_fs();
}

bool Storage::is_ramfs_fallback() {
    return using_ramfs;
}

void Storage::print_status() {
    Console::set_color(Color::Yellow);
    Console::println("=== Storage Status ===");
    Console::set_color(Color::LightGray);
    
    // Init result
    Console::print("  Status: ");
    switch (init_result) {
        case StorageInitResult::Success:
            Console::set_color(Color::LightGreen);
            Console::println("Ready (real storage)");
            break;
        case StorageInitResult::DegradedRAMFS:
            Console::set_color(Color::Yellow);
            Console::println("Degraded (RAMFS fallback)");
            break;
        case StorageInitResult::PartialSuccess:
            Console::set_color(Color::Yellow);
            Console::println("Partial (some devices)");
            break;
        case StorageInitResult::Failed:
            Console::set_color(Color::LightRed);
            Console::println("Failed");
            break;
    }
    Console::set_color(Color::LightGray);
    
    Console::println("");
    
    // Block devices
    BlockDeviceManager::print_devices();
    
    Console::println("");
    
    // Mount points
    VFS::print_mounts();
}

VFSResult Storage::sync() {
    return VFS::sync_all();
}

VFSResult Storage::mount(const char* device, const char* path) {
    return VFS::mount(device, path);
}

VFSResult Storage::unmount(const char* path) {
    return VFS::unmount(path);
}

// ===========================================================================
// Convenience Functions
// ===========================================================================

bool file_exists(const char* path) {
    return VFS::exists(path);
}

bool is_directory(const char* path) {
    return VFS::is_directory(path);
}

u64 file_size(const char* path) {
    FileInfo info;
    if (VFS::stat(path, info) != VFSResult::Success) {
        return 0;
    }
    return info.size;
}

u64 read_file(const char* path, void* buffer, u64 max_size) {
    u32 fd;
    if (VFS::open(path, FileMode::Read, fd) != VFSResult::Success) {
        return 0;
    }
    
    u64 bytes_read = 0;
    VFS::read(fd, buffer, max_size, bytes_read);
    VFS::close(fd);
    
    return bytes_read;
}

u64 write_file(const char* path, const void* buffer, u64 size) {
    u32 fd;
    FileMode mode = FileMode::Write | FileMode::Create | FileMode::Truncate;
    if (VFS::open(path, mode, fd) != VFSResult::Success) {
        return 0;
    }
    
    u64 bytes_written = 0;
    VFS::write(fd, buffer, size, bytes_written);
    VFS::close(fd);
    
    return bytes_written;
}

bool create_directory(const char* path) {
    return VFS::mkdir(path) == VFSResult::Success;
}

bool delete_file(const char* path) {
    return VFS::unlink(path) == VFSResult::Success;
}

bool list_directory(const char* path, DirCallback callback, void* user_data) {
    if (!callback) return false;
    
    u32 fd;
    if (VFS::opendir(path, fd) != VFSResult::Success) {
        return false;
    }
    
    FileInfo info;
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        callback(info, user_data);
    }
    
    VFS::closedir(fd);
    return true;
}

} // namespace bolt::storage
