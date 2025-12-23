/* ===========================================================================
 * BOLT OS - RAM Filesystem (RAMFS)
 * 
 * In-memory filesystem used as fallback when no real storage is available.
 * Data is stored in RAM and lost on reboot.
 * =========================================================================== */

#ifndef BOLT_STORAGE_RAMFS_HPP
#define BOLT_STORAGE_RAMFS_HPP

#include "../core/types.hpp"
#include "vfs.hpp"

namespace bolt::storage {

// ===========================================================================
// RAMFS Node Types and Structures
// ===========================================================================

struct RAMFSNode {
    static constexpr u32 MAX_NAME = 64;
    static constexpr u32 MAX_CHILDREN = 32;
    static constexpr u32 INVALID_ID = 0xFFFFFFFF;
    
    char        name[MAX_NAME];
    FileType    type;
    u32         id;               // Unique node ID
    u32         parent_id;        // Parent node ID
    u32         children[MAX_CHILDREN];
    u32         child_count;
    
    // File data
    u8*         data;
    u64         size;
    u64         capacity;
    
    // Metadata
    u32         permissions;
    u64         created;
    u64         modified;
    u64         accessed;
    u8          attributes;
    
    void init() {
        for (u32 i = 0; i < MAX_NAME; i++) name[i] = 0;
        type = FileType::Unknown;
        id = INVALID_ID;
        parent_id = INVALID_ID;
        for (u32 i = 0; i < MAX_CHILDREN; i++) children[i] = INVALID_ID;
        child_count = 0;
        data = nullptr;
        size = 0;
        capacity = 0;
        permissions = 0755;
        created = 0;
        modified = 0;
        accessed = 0;
        attributes = 0;
    }
    
    bool is_directory() const { return type == FileType::Directory; }
    bool is_file() const { return type == FileType::Regular; }
    bool is_valid() const { return id != INVALID_ID; }
};

// Directory iteration state
struct RAMFSDirState {
    u32 node_id;
    u32 index;
};

// ===========================================================================
// RAM Filesystem Implementation
// ===========================================================================

class RAMFilesystem : public Filesystem {
public:
    static constexpr u32 MAX_NODES = 256;
    static constexpr u64 MAX_FILE_SIZE = 1024 * 1024;  // 1MB per file
    static constexpr u64 MAX_TOTAL_SIZE = 16 * 1024 * 1024;  // 16MB total
    
    RAMFilesystem();
    virtual ~RAMFilesystem();
    
    // Filesystem interface
    FilesystemType type() const override { return FilesystemType::RAMFS; }
    const char* name() const override { return "ramfs"; }
    
    VFSResult mount(BlockDevice* device, const char* mount_point) override;
    VFSResult unmount() override;
    
    VFSResult open(const char* path, FileMode mode, FileDescriptor& fd) override;
    VFSResult close(FileDescriptor& fd) override;
    VFSResult read(FileDescriptor& fd, void* buffer, u64 size, u64& bytes_read) override;
    VFSResult write(FileDescriptor& fd, const void* buffer, u64 size, u64& bytes_written) override;
    VFSResult seek(FileDescriptor& fd, i64 offset, SeekMode mode) override;
    
    VFSResult opendir(const char* path, FileDescriptor& fd) override;
    VFSResult readdir(FileDescriptor& fd, FileInfo& info) override;
    VFSResult closedir(FileDescriptor& fd) override;
    VFSResult mkdir(const char* path) override;
    VFSResult rmdir(const char* path) override;
    
    VFSResult stat(const char* path, FileInfo& info) override;
    VFSResult unlink(const char* path) override;
    VFSResult rename(const char* old_path, const char* new_path) override;
    
    u64 total_space() const override { return MAX_TOTAL_SIZE; }
    u64 free_space() const override { return MAX_TOTAL_SIZE - used_bytes; }
    
    VFSResult sync() override { return VFSResult::Success; }  // No-op for RAMFS
    
    // RAMFS-specific methods
    u32 file_count() const { return node_count; }
    u32 directory_count() const;
    
private:
    // Node management
    RAMFSNode* alloc_node();
    void free_node(RAMFSNode* node);
    RAMFSNode* get_node(u32 id);
    RAMFSNode* find_node(const char* path);
    RAMFSNode* find_child(RAMFSNode* parent, const char* name);
    
    // Path parsing
    bool split_path(const char* path, char* parent, char* name);
    
    // Data management
    bool resize_file(RAMFSNode* node, u64 new_size);
    
    // Fill FileInfo from node
    void fill_info(RAMFSNode* node, FileInfo& info);
    
    // Storage
    RAMFSNode nodes[MAX_NODES];
    u32 node_count;
    u64 used_bytes;
    u32 next_id;
    
    // Root node (always nodes[0])
    RAMFSNode* root;
};

} // namespace bolt::storage

#endif // BOLT_STORAGE_RAMFS_HPP
