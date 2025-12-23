/* ===========================================================================
 * BOLT OS - Virtual File System Implementation
 * =========================================================================== */

#include "vfs.hpp"
#include "detect.hpp"
#include "../drivers/serial/serial.hpp"
#include "../drivers/video/console.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
MountPoint VFS::mounts[MAX_MOUNTS];
FileDescriptor VFS::file_descriptors[MAX_OPEN_FILES];
u32 VFS::mount_count = 0;
u32 VFS::open_file_count = 0;
bool VFS::initialized = false;

// ===========================================================================
// VFS Result Strings
// ===========================================================================

const char* vfs_result_string(VFSResult result) {
    switch (result) {
        case VFSResult::Success:        return "Success";
        case VFSResult::NotFound:       return "Not found";
        case VFSResult::AccessDenied:   return "Access denied";
        case VFSResult::AlreadyExists:  return "Already exists";
        case VFSResult::NotEmpty:       return "Directory not empty";
        case VFSResult::IsDirectory:    return "Is a directory";
        case VFSResult::NotDirectory:   return "Not a directory";
        case VFSResult::NoSpace:        return "No space left";
        case VFSResult::InvalidPath:    return "Invalid path";
        case VFSResult::InvalidArgument: return "Invalid argument";
        case VFSResult::TooManyOpen:    return "Too many open files";
        case VFSResult::BadDescriptor:  return "Bad file descriptor";
        case VFSResult::NotMounted:     return "Not mounted";
        case VFSResult::AlreadyMounted: return "Already mounted";
        case VFSResult::ReadOnly:       return "Read-only filesystem";
        case VFSResult::IOError:        return "I/O error";
        case VFSResult::Unsupported:    return "Operation not supported";
        case VFSResult::Busy:           return "Device busy";
        case VFSResult::CrossDevice:    return "Cross-device link";
        case VFSResult::NameTooLong:    return "Name too long";
        case VFSResult::NoFilesystem:   return "No filesystem";
        default:                        return "Unknown error";
    }
}

// ===========================================================================
// VFS Initialization
// ===========================================================================

void VFS::init() {
    DBG_LOADING("VFS", "Initializing Virtual File System...");
    
    // Clear mount points
    for (u32 i = 0; i < MAX_MOUNTS; i++) {
        mounts[i].clear();
    }
    
    // Clear file descriptors
    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        file_descriptors[i].clear();
    }
    
    mount_count = 0;
    open_file_count = 0;
    initialized = true;
    
    DBG_OK("VFS", "VFS initialized");
}

bool VFS::is_ready() {
    return initialized;
}

// ===========================================================================
// Mounting
// ===========================================================================

VFSResult VFS::mount(const char* device_name, const char* mount_point, FilesystemType fs_type) {
    if (!initialized) return VFSResult::NotMounted;
    if (!mount_point) return VFSResult::InvalidPath;
    
    DBG_LOADING("VFS", "Mounting device...");
    
    // Check for existing mount at this path
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].active && str::cmp(mounts[i].path, mount_point) == 0) {
            DBG_WARN("VFS", "Already mounted at this path");
            return VFSResult::AlreadyMounted;
        }
    }
    
    // Find free mount slot
    if (mount_count >= MAX_MOUNTS) {
        DBG_FAIL("VFS", "No free mount slots");
        return VFSResult::TooManyOpen;
    }
    
    // Get device
    BlockDevice* device = nullptr;
    if (device_name) {
        device = BlockDeviceManager::get_device_by_name(device_name);
        if (!device) {
            Serial::write("[VFS] Device not found: ");
            Serial::writeln(device_name);
            return VFSResult::NotFound;
        }
    }
    
    // Auto-detect filesystem if not specified
    if (fs_type == FilesystemType::Unknown && device) {
        fs_type = FilesystemDetector::detect(device);
        Serial::write("[VFS]   Detected: ");
        Serial::writeln(FilesystemDetector::type_name(fs_type));
    }
    
    // Create filesystem instance
    Filesystem* fs = FilesystemDetector::create_filesystem(fs_type);
    if (!fs) {
        DBG_FAIL("VFS", "Failed to create filesystem instance");
        return VFSResult::NoFilesystem;
    }
    
    // Mount the filesystem
    VFSResult result = fs->mount(device, mount_point);
    if (result != VFSResult::Success) {
        Serial::write("[VFS] Mount failed: ");
        Serial::writeln(vfs_result_string(result));
        delete fs;
        return result;
    }
    
    // Add to mount table
    MountPoint& mp = mounts[mount_count++];
    str::cpy(mp.path, mount_point);
    mp.fs = fs;
    mp.device = device;
    mp.fs_type = fs_type;
    mp.read_only = false;
    mp.active = true;
    
    Serial::write("[VFS] Mounted ");
    Serial::write(fs->name());
    Serial::write(" at ");
    Serial::writeln(mount_point);
    
    return VFSResult::Success;
}

VFSResult VFS::mount(Filesystem* fs, const char* mount_point) {
    if (!initialized) return VFSResult::NotMounted;
    if (!fs || !mount_point) return VFSResult::InvalidArgument;
    
    // Check for existing mount
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].active && str::cmp(mounts[i].path, mount_point) == 0) {
            return VFSResult::AlreadyMounted;
        }
    }
    
    if (mount_count >= MAX_MOUNTS) {
        return VFSResult::TooManyOpen;
    }
    
    // Mount filesystem
    VFSResult result = fs->mount(nullptr, mount_point);
    if (result != VFSResult::Success) {
        return result;
    }
    
    // Add to mount table
    MountPoint& mp = mounts[mount_count++];
    str::cpy(mp.path, mount_point);
    mp.fs = fs;
    mp.device = nullptr;
    mp.fs_type = fs->type();
    mp.read_only = false;
    mp.active = true;
    
    return VFSResult::Success;
}

VFSResult VFS::unmount(const char* mount_point) {
    if (!initialized) return VFSResult::NotMounted;
    if (!mount_point) return VFSResult::InvalidPath;
    
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].active && str::cmp(mounts[i].path, mount_point) == 0) {
            // Check for open files on this mount
            for (u32 j = 0; j < MAX_OPEN_FILES; j++) {
                if (file_descriptors[j].valid && file_descriptors[j].fs == mounts[i].fs) {
                    return VFSResult::Busy;
                }
            }
            
            // Unmount filesystem
            if (mounts[i].fs) {
                mounts[i].fs->unmount();
                delete mounts[i].fs;
            }
            
            mounts[i].clear();
            
            // Compact mount array
            for (u32 j = i; j < mount_count - 1; j++) {
                mounts[j] = mounts[j + 1];
            }
            mounts[--mount_count].clear();
            
            DBG("VFS", "Filesystem unmounted");
            return VFSResult::Success;
        }
    }
    
    return VFSResult::NotMounted;
}

// ===========================================================================
// Path Resolution
// ===========================================================================

MountPoint* VFS::find_mount_for_path(const char* path) {
    if (!path || path[0] != '/') return nullptr;
    
    MountPoint* best_match = nullptr;
    usize best_len = 0;
    
    // Find longest matching mount point
    for (u32 i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        
        usize mp_len = str::len(mounts[i].path);
        
        // Check if path starts with mount point
        if (str::ncmp(path, mounts[i].path, mp_len) == 0) {
            // Ensure we match at directory boundary
            if (path[mp_len] == '\0' || path[mp_len] == '/' || mp_len == 1) {
                if (mp_len > best_len) {
                    best_len = mp_len;
                    best_match = &mounts[i];
                }
            }
        }
    }
    
    return best_match;
}

Filesystem* VFS::resolve_path(const char* path, char* relative_path) {
    MountPoint* mp = find_mount_for_path(path);
    if (!mp) return nullptr;
    
    // Calculate relative path
    usize mp_len = str::len(mp->path);
    if (mp_len == 1) {  // Root mount
        str::cpy(relative_path, path);
    } else {
        if (path[mp_len] == '\0') {
            relative_path[0] = '/';
            relative_path[1] = '\0';
        } else {
            str::cpy(relative_path, path + mp_len);
        }
    }
    
    return mp->fs;
}

// ===========================================================================
// File Descriptor Management
// ===========================================================================

u32 VFS::alloc_fd() {
    for (u32 i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_descriptors[i].valid) {
            file_descriptors[i].fd = i;
            file_descriptors[i].valid = true;
            open_file_count++;
            return i;
        }
    }
    return FileDescriptor::INVALID;
}

void VFS::free_fd(u32 fd) {
    if (fd < MAX_OPEN_FILES && file_descriptors[fd].valid) {
        file_descriptors[fd].clear();
        if (open_file_count > 0) open_file_count--;
    }
}

// ===========================================================================
// File Operations
// ===========================================================================

VFSResult VFS::open(const char* path, FileMode mode, u32& fd) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    u32 new_fd = alloc_fd();
    if (new_fd == FileDescriptor::INVALID) {
        return VFSResult::TooManyOpen;
    }
    
    VFSResult result = fs->open(relative, mode, file_descriptors[new_fd]);
    if (result != VFSResult::Success) {
        free_fd(new_fd);
        return result;
    }
    
    file_descriptors[new_fd].fs = fs;
    fd = new_fd;
    return VFSResult::Success;
}

VFSResult VFS::close(u32 fd) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    
    FileDescriptor& desc = file_descriptors[fd];
    VFSResult result = VFSResult::Success;
    
    if (desc.fs) {
        result = desc.fs->close(desc);
    }
    
    free_fd(fd);
    return result;
}

VFSResult VFS::read(u32 fd, void* buffer, u64 size, u64& bytes_read) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    if (!buffer) return VFSResult::InvalidArgument;
    
    FileDescriptor& desc = file_descriptors[fd];
    if (!desc.fs) return VFSResult::IOError;
    
    return desc.fs->read(desc, buffer, size, bytes_read);
}

VFSResult VFS::write(u32 fd, const void* buffer, u64 size, u64& bytes_written) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    if (!buffer) return VFSResult::InvalidArgument;
    
    FileDescriptor& desc = file_descriptors[fd];
    if (!desc.fs) return VFSResult::IOError;
    
    if (!has_flag(desc.mode, FileMode::Write)) {
        return VFSResult::AccessDenied;
    }
    
    return desc.fs->write(desc, buffer, size, bytes_written);
}

VFSResult VFS::seek(u32 fd, i64 offset, SeekMode mode) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    
    FileDescriptor& desc = file_descriptors[fd];
    if (!desc.fs) return VFSResult::IOError;
    
    return desc.fs->seek(desc, offset, mode);
}

// ===========================================================================
// Directory Operations
// ===========================================================================

VFSResult VFS::opendir(const char* path, u32& fd) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    u32 new_fd = alloc_fd();
    if (new_fd == FileDescriptor::INVALID) {
        return VFSResult::TooManyOpen;
    }
    
    VFSResult result = fs->opendir(relative, file_descriptors[new_fd]);
    if (result != VFSResult::Success) {
        free_fd(new_fd);
        return result;
    }
    
    file_descriptors[new_fd].fs = fs;
    file_descriptors[new_fd].type = FileType::Directory;
    fd = new_fd;
    return VFSResult::Success;
}

VFSResult VFS::readdir(u32 fd, FileInfo& info) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    
    FileDescriptor& desc = file_descriptors[fd];
    if (desc.type != FileType::Directory) {
        return VFSResult::NotDirectory;
    }
    if (!desc.fs) return VFSResult::IOError;
    
    return desc.fs->readdir(desc, info);
}

VFSResult VFS::closedir(u32 fd) {
    if (!initialized) return VFSResult::NotMounted;
    if (fd >= MAX_OPEN_FILES || !file_descriptors[fd].valid) {
        return VFSResult::BadDescriptor;
    }
    
    FileDescriptor& desc = file_descriptors[fd];
    VFSResult result = VFSResult::Success;
    
    if (desc.fs) {
        result = desc.fs->closedir(desc);
    }
    
    free_fd(fd);
    return result;
}

VFSResult VFS::mkdir(const char* path) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    return fs->mkdir(relative);
}

VFSResult VFS::rmdir(const char* path) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    return fs->rmdir(relative);
}

// ===========================================================================
// File Management
// ===========================================================================

VFSResult VFS::stat(const char* path, FileInfo& info) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    return fs->stat(relative, info);
}

VFSResult VFS::unlink(const char* path) {
    if (!initialized) return VFSResult::NotMounted;
    if (!path) return VFSResult::InvalidPath;
    
    char relative[256];
    Filesystem* fs = resolve_path(path, relative);
    if (!fs) return VFSResult::NotFound;
    
    return fs->unlink(relative);
}

VFSResult VFS::rename(const char* old_path, const char* new_path) {
    if (!initialized) return VFSResult::NotMounted;
    if (!old_path || !new_path) return VFSResult::InvalidPath;
    
    char old_rel[256], new_rel[256];
    Filesystem* old_fs = resolve_path(old_path, old_rel);
    Filesystem* new_fs = resolve_path(new_path, new_rel);
    
    if (!old_fs) return VFSResult::NotFound;
    if (old_fs != new_fs) return VFSResult::CrossDevice;
    
    return old_fs->rename(old_rel, new_rel);
}

// ===========================================================================
// Utility Functions
// ===========================================================================

bool VFS::exists(const char* path) {
    FileInfo info;
    return stat(path, info) == VFSResult::Success;
}

bool VFS::is_directory(const char* path) {
    FileInfo info;
    if (stat(path, info) != VFSResult::Success) return false;
    return info.type == FileType::Directory;
}

bool VFS::is_file(const char* path) {
    FileInfo info;
    if (stat(path, info) != VFSResult::Success) return false;
    return info.type == FileType::Regular;
}

MountPoint* VFS::get_mount(const char* path) {
    return find_mount_for_path(path);
}

u32 VFS::get_mount_count() {
    return mount_count;
}

MountPoint* VFS::get_mount_by_index(u32 index) {
    if (index >= mount_count) return nullptr;
    return &mounts[index];
}

Filesystem* VFS::get_root_fs() {
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].active && str::cmp(mounts[i].path, "/") == 0) {
            return mounts[i].fs;
        }
    }
    return nullptr;
}

void VFS::print_mounts() {
    Console::set_color(Color::Yellow);
    Console::println("=== Mount Points ===");
    Console::set_color(Color::LightGray);
    
    if (mount_count == 0) {
        Console::println("  No filesystems mounted");
        return;
    }
    
    for (u32 i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        
        Console::print("  ");
        Console::set_color(Color::LightCyan);
        Console::print(mounts[i].path);
        Console::set_color(Color::LightGray);
        
        // Pad to column
        usize len = str::len(mounts[i].path);
        for (usize j = len; j < 16; j++) Console::print(" ");
        
        Console::print(" -> ");
        Console::print(mounts[i].fs ? mounts[i].fs->name() : "???");
        
        if (mounts[i].device) {
            Console::print(" on /dev/");
            Console::print(mounts[i].device->get_info().name);
        }
        
        if (mounts[i].read_only) {
            Console::set_color(Color::DarkGray);
            Console::print(" (ro)");
            Console::set_color(Color::LightGray);
        }
        
        Console::println("");
    }
}

VFSResult VFS::sync_all() {
    if (!initialized) return VFSResult::NotMounted;
    
    VFSResult result = VFSResult::Success;
    
    for (u32 i = 0; i < mount_count; i++) {
        if (mounts[i].active && mounts[i].fs) {
            VFSResult r = mounts[i].fs->sync();
            if (r != VFSResult::Success) {
                result = r;  // Return last error
            }
        }
    }
    
    return result;
}

// ===========================================================================
// Path Utilities
// ===========================================================================

namespace path {

void normalize(const char* input, char* output, usize output_size) {
    if (!input || !output || output_size == 0) return;
    
    char temp[256];
    usize temp_pos = 0;
    
    // Ensure starts with /
    if (input[0] != '/') {
        temp[temp_pos++] = '/';
    }
    
    usize i = 0;
    while (input[i] && temp_pos < sizeof(temp) - 1) {
        if (input[i] == '/') {
            // Skip multiple slashes
            while (input[i] == '/') i++;
            
            // Add single slash if not at end
            if (input[i] && temp_pos > 0 && temp[temp_pos-1] != '/') {
                temp[temp_pos++] = '/';
            }
        } else if (input[i] == '.' && (input[i+1] == '/' || input[i+1] == '\0')) {
            // Skip "."
            i++;
        } else if (input[i] == '.' && input[i+1] == '.' && 
                   (input[i+2] == '/' || input[i+2] == '\0')) {
            // Handle ".." - go up one level
            i += 2;
            if (temp_pos > 1) {
                temp_pos--;  // Remove trailing /
                while (temp_pos > 0 && temp[temp_pos-1] != '/') {
                    temp_pos--;
                }
            }
        } else {
            // Copy character
            temp[temp_pos++] = input[i++];
        }
    }
    
    // Remove trailing slash (unless root)
    if (temp_pos > 1 && temp[temp_pos-1] == '/') {
        temp_pos--;
    }
    
    // Ensure non-empty
    if (temp_pos == 0) {
        temp[temp_pos++] = '/';
    }
    
    temp[temp_pos] = '\0';
    
    // Copy to output
    usize copy_len = temp_pos < output_size - 1 ? temp_pos : output_size - 1;
    for (usize j = 0; j < copy_len; j++) {
        output[j] = temp[j];
    }
    output[copy_len] = '\0';
}

void join(const char* base, const char* part, char* output, usize output_size) {
    if (!output || output_size == 0) return;
    
    char temp[512];
    temp[0] = '\0';
    
    if (base) {
        str::cpy(temp, base);
    }
    
    usize len = str::len(temp);
    
    // Add separator if needed
    if (len > 0 && temp[len-1] != '/' && part && part[0] != '/') {
        temp[len++] = '/';
        temp[len] = '\0';
    }
    
    if (part) {
        str::cat(temp, part);
    }
    
    normalize(temp, output, output_size);
}

void dirname(const char* path, char* output, usize output_size) {
    if (!path || !output || output_size == 0) return;
    
    usize len = str::len(path);
    
    // Find last slash
    usize last_slash = 0;
    for (usize i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }
    
    if (last_slash == 0) {
        // Root or no directory
        output[0] = '/';
        output[1] = '\0';
    } else {
        usize copy_len = last_slash < output_size - 1 ? last_slash : output_size - 1;
        for (usize i = 0; i < copy_len; i++) {
            output[i] = path[i];
        }
        output[copy_len] = '\0';
    }
}

void basename(const char* path, char* output, usize output_size) {
    if (!path || !output || output_size == 0) return;
    
    usize len = str::len(path);
    
    // Skip trailing slashes
    while (len > 0 && path[len-1] == '/') len--;
    
    // Find last component
    usize start = 0;
    for (usize i = 0; i < len; i++) {
        if (path[i] == '/') start = i + 1;
    }
    
    usize copy_len = len - start;
    if (copy_len >= output_size) copy_len = output_size - 1;
    
    for (usize i = 0; i < copy_len; i++) {
        output[i] = path[start + i];
    }
    output[copy_len] = '\0';
}

const char* extension(const char* path) {
    if (!path) return nullptr;
    
    const char* last_dot = nullptr;
    const char* last_slash = nullptr;
    
    for (const char* p = path; *p; p++) {
        if (*p == '.') last_dot = p;
        if (*p == '/') last_slash = p;
    }
    
    // Extension must be after last slash
    if (last_dot && (!last_slash || last_dot > last_slash)) {
        return last_dot + 1;
    }
    
    return nullptr;
}

bool is_absolute(const char* path) {
    return path && path[0] == '/';
}

bool is_root(const char* path) {
    if (!path) return false;
    return (path[0] == '/' && path[1] == '\0');
}

} // namespace path

} // namespace bolt::storage
