/* ===========================================================================
 * BOLT OS - Virtual File System (VFS)
 * 
 * Provides a unified interface to all filesystems through mount points.
 * RAMFS is used as fallback when no real storage is available.
 * =========================================================================== */

#ifndef BOLT_STORAGE_VFS_HPP
#define BOLT_STORAGE_VFS_HPP

#include "../core/types.hpp"
#include "block.hpp"

namespace bolt::storage {

// Forward declarations
class Filesystem;

// ===========================================================================
// File System Types
// ===========================================================================

enum class FilesystemType : u8 {
    Unknown = 0,
    FAT12,
    FAT16,
    FAT32,
    exFAT,
    NTFS,
    ext2,
    ext3,
    ext4,
    ISO9660,    // CD-ROM
    UDF,        // DVD/Blu-ray
    RAMFS,      // In-memory filesystem
    DevFS,      // Device filesystem (/dev)
    ProcFS,     // Process filesystem (/proc)
    TmpFS       // Temporary filesystem
};

// ===========================================================================
// File Types and Flags
// ===========================================================================

enum class FileType : u8 {
    Unknown = 0,
    Regular,
    Directory,
    Symlink,
    BlockDevice,
    CharDevice,
    Pipe,
    Socket
};

enum class FileMode : u16 {
    Read      = 0x001,
    Write     = 0x002,
    ReadWrite = 0x003,
    Append    = 0x008,
    Create    = 0x010,
    Truncate  = 0x020,
    Exclusive = 0x040,
    Directory = 0x100
};

// Allow bitwise operations on FileMode
inline FileMode operator|(FileMode a, FileMode b) {
    return static_cast<FileMode>(static_cast<u16>(a) | static_cast<u16>(b));
}
inline FileMode operator&(FileMode a, FileMode b) {
    return static_cast<FileMode>(static_cast<u16>(a) & static_cast<u16>(b));
}
inline bool has_flag(FileMode mode, FileMode flag) {
    return (static_cast<u16>(mode) & static_cast<u16>(flag)) != 0;
}

enum class SeekMode : u8 {
    Set,      // From beginning
    Current,  // From current position
    End       // From end
};

// ===========================================================================
// File Info / Directory Entry
// ===========================================================================

struct FileInfo {
    char        name[256];
    FileType    type;
    u64         size;
    u32         inode;          // Filesystem-specific identifier
    u32         permissions;    // Unix-style permissions
    u32         uid;            // User ID
    u32         gid;            // Group ID
    u64         created;        // Timestamp
    u64         modified;
    u64         accessed;
    u8          attributes;     // DOS/FAT attributes
    
    void clear() {
        for (int i = 0; i < 256; i++) name[i] = 0;
        type = FileType::Unknown;
        size = 0;
        inode = 0;
        permissions = 0;
        uid = 0;
        gid = 0;
        created = 0;
        modified = 0;
        accessed = 0;
        attributes = 0;
    }
    
    bool is_directory() const { return type == FileType::Directory; }
    bool is_file() const { return type == FileType::Regular; }
    bool is_hidden() const { return attributes & 0x02; }
    bool is_system() const { return attributes & 0x04; }
    bool is_readonly() const { return attributes & 0x01; }
};

// ===========================================================================
// File Descriptor
// ===========================================================================

struct FileDescriptor {
    static constexpr u32 INVALID = 0xFFFFFFFF;
    
    u32         fd;             // File descriptor number
    Filesystem* fs;             // Owning filesystem
    u64         position;       // Current read/write position
    u64         size;           // File size
    u32         inode;          // Filesystem-specific identifier
    FileMode    mode;           // Open mode
    FileType    type;           // File type
    void*       fs_data;        // Filesystem-specific data
    bool        valid;          // Is this descriptor valid?
    
    void clear() {
        fd = INVALID;
        fs = nullptr;
        position = 0;
        size = 0;
        inode = 0;
        mode = FileMode::Read;
        type = FileType::Unknown;
        fs_data = nullptr;
        valid = false;
    }
};

// ===========================================================================
// VFS Result Codes
// ===========================================================================

enum class VFSResult : u8 {
    Success = 0,
    NotFound,
    AccessDenied,
    AlreadyExists,
    NotEmpty,
    IsDirectory,
    NotDirectory,
    NoSpace,
    InvalidPath,
    InvalidArgument,
    TooManyOpen,
    BadDescriptor,
    NotMounted,
    AlreadyMounted,
    ReadOnly,
    IOError,
    Unsupported,
    Busy,
    CrossDevice,
    NameTooLong,
    NoFilesystem
};

const char* vfs_result_string(VFSResult result);

// ===========================================================================
// Mount Point
// ===========================================================================

struct MountPoint {
    char            path[64];       // Mount path (e.g., "/", "/mnt/usb")
    Filesystem*     fs;             // Mounted filesystem
    BlockDevice*    device;         // Underlying device (null for virtual FS)
    FilesystemType  fs_type;
    bool            read_only;
    bool            active;
    
    void clear() {
        for (int i = 0; i < 64; i++) path[i] = 0;
        fs = nullptr;
        device = nullptr;
        fs_type = FilesystemType::Unknown;
        read_only = false;
        active = false;
    }
};

// ===========================================================================
// Abstract Filesystem Interface
// ===========================================================================

class Filesystem {
public:
    virtual ~Filesystem() = default;
    
    // Get filesystem type
    virtual FilesystemType type() const = 0;
    
    // Get filesystem name
    virtual const char* name() const = 0;
    
    // Mount/unmount
    virtual VFSResult mount(BlockDevice* device, const char* mount_point) = 0;
    virtual VFSResult unmount() = 0;
    
    // File operations
    virtual VFSResult open(const char* path, FileMode mode, FileDescriptor& fd) = 0;
    virtual VFSResult close(FileDescriptor& fd) = 0;
    virtual VFSResult read(FileDescriptor& fd, void* buffer, u64 size, u64& bytes_read) = 0;
    virtual VFSResult write(FileDescriptor& fd, const void* buffer, u64 size, u64& bytes_written) = 0;
    virtual VFSResult seek(FileDescriptor& fd, i64 offset, SeekMode mode) = 0;
    
    // Directory operations
    virtual VFSResult opendir(const char* path, FileDescriptor& fd) = 0;
    virtual VFSResult readdir(FileDescriptor& fd, FileInfo& info) = 0;
    virtual VFSResult closedir(FileDescriptor& fd) = 0;
    virtual VFSResult mkdir(const char* path) = 0;
    virtual VFSResult rmdir(const char* path) = 0;
    
    // File management
    virtual VFSResult stat(const char* path, FileInfo& info) = 0;
    virtual VFSResult unlink(const char* path) = 0;
    virtual VFSResult rename(const char* old_path, const char* new_path) = 0;
    
    // Filesystem info
    virtual u64 total_space() const = 0;
    virtual u64 free_space() const = 0;
    virtual u64 used_space() const { return total_space() - free_space(); }
    
    // Sync all pending writes
    virtual VFSResult sync() = 0;
    
    // Is filesystem mounted?
    bool is_mounted() const { return mounted; }
    
protected:
    bool mounted = false;
    const char* mount_path = nullptr;
    BlockDevice* device = nullptr;
};

// ===========================================================================
// Virtual File System Manager
// ===========================================================================

class VFS {
public:
    static constexpr u32 MAX_MOUNTS = 16;
    static constexpr u32 MAX_OPEN_FILES = 64;
    
    // Initialize VFS
    static void init();
    
    // Mount filesystem at path
    static VFSResult mount(const char* device_name, const char* mount_point, 
                           FilesystemType fs_type = FilesystemType::Unknown);
    
    // Mount filesystem with explicit filesystem instance
    static VFSResult mount(Filesystem* fs, const char* mount_point);
    
    // Unmount filesystem at path
    static VFSResult unmount(const char* mount_point);
    
    // File operations (path-based)
    static VFSResult open(const char* path, FileMode mode, u32& fd);
    static VFSResult close(u32 fd);
    static VFSResult read(u32 fd, void* buffer, u64 size, u64& bytes_read);
    static VFSResult write(u32 fd, const void* buffer, u64 size, u64& bytes_written);
    static VFSResult seek(u32 fd, i64 offset, SeekMode mode);
    
    // Directory operations
    static VFSResult opendir(const char* path, u32& fd);
    static VFSResult readdir(u32 fd, FileInfo& info);
    static VFSResult closedir(u32 fd);
    static VFSResult mkdir(const char* path);
    static VFSResult rmdir(const char* path);
    
    // File management
    static VFSResult stat(const char* path, FileInfo& info);
    static VFSResult unlink(const char* path);
    static VFSResult rename(const char* old_path, const char* new_path);
    
    // Check if path exists
    static bool exists(const char* path);
    static bool is_directory(const char* path);
    static bool is_file(const char* path);
    
    // Get info about a mount point
    static MountPoint* get_mount(const char* path);
    static u32 get_mount_count();
    static MountPoint* get_mount_by_index(u32 index);
    
    // List all mounts
    static void print_mounts();
    
    // Sync all filesystems
    static VFSResult sync_all();
    
    // Get root filesystem
    static Filesystem* get_root_fs();
    
    // Is VFS ready?
    static bool is_ready();
    
private:
    // Find filesystem and relative path for given absolute path
    static Filesystem* resolve_path(const char* path, char* relative_path);
    
    // Find mount point for path
    static MountPoint* find_mount_for_path(const char* path);
    
    // Allocate/free file descriptor
    static u32 alloc_fd();
    static void free_fd(u32 fd);
    
    // Storage
    static MountPoint mounts[MAX_MOUNTS];
    static FileDescriptor file_descriptors[MAX_OPEN_FILES];
    static u32 mount_count;
    static u32 open_file_count;
    static bool initialized;
};

// ===========================================================================
// Path Utilities
// ===========================================================================

namespace path {
    // Normalize path (remove . and .., ensure starts with /)
    void normalize(const char* input, char* output, usize output_size);
    
    // Join two path components
    void join(const char* base, const char* part, char* output, usize output_size);
    
    // Get parent directory
    void dirname(const char* path, char* output, usize output_size);
    
    // Get filename from path
    void basename(const char* path, char* output, usize output_size);
    
    // Get file extension
    const char* extension(const char* path);
    
    // Check if path is absolute
    bool is_absolute(const char* path);
    
    // Check if path is root
    bool is_root(const char* path);
}

} // namespace bolt::storage

#endif // BOLT_STORAGE_VFS_HPP
