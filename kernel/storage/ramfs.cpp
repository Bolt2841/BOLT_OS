/* ===========================================================================
 * BOLT OS - RAM Filesystem Implementation
 * =========================================================================== */

#include "ramfs.hpp"
#include "../drivers/serial/serial.hpp"
#include "../lib/string.hpp"
#include "../core/memory/heap.hpp"

namespace bolt::storage {

using namespace drivers;
using namespace mem;

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

RAMFilesystem::RAMFilesystem() 
    : node_count(0), used_bytes(0), next_id(1), root(nullptr) 
{
    for (u32 i = 0; i < MAX_NODES; i++) {
        nodes[i].init();
    }
}

RAMFilesystem::~RAMFilesystem() {
    if (mounted) {
        unmount();
    }
}

// ===========================================================================
// Mount / Unmount
// ===========================================================================

VFSResult RAMFilesystem::mount(BlockDevice* device, const char* mnt_point) {
    // RAMFS ignores device parameter
    (void)device;
    
    DBG_LOADING("RAMFS", "Mounting RAM filesystem...");
    
    if (mounted) {
        DBG_WARN("RAMFS", "Already mounted");
        return VFSResult::AlreadyMounted;
    }
    
    // Initialize root directory
    root = alloc_node();
    if (!root) {
        DBG_FAIL("RAMFS", "Failed to create root node");
        return VFSResult::NoSpace;
    }
    
    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = FileType::Directory;
    root->parent_id = RAMFSNode::INVALID_ID;
    root->permissions = 0755;
    
    mount_path = mnt_point;
    mounted = true;
    
    DBG_SUCCESS("RAMFS", mnt_point);
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::unmount() {
    if (!mounted) {
        return VFSResult::NotMounted;
    }
    
    DBG_DEBUG("RAMFS", "Unmounting...");
    
    // Free all file data
    for (u32 i = 0; i < MAX_NODES; i++) {
        if (nodes[i].is_valid() && nodes[i].data) {
            Heap::free(nodes[i].data);
            nodes[i].data = nullptr;
        }
        nodes[i].init();
    }
    
    node_count = 0;
    used_bytes = 0;
    next_id = 1;
    root = nullptr;
    mounted = false;
    mount_path = nullptr;
    
    DBG_OK("RAMFS", "Unmounted");
    return VFSResult::Success;
}

// ===========================================================================
// Node Management
// ===========================================================================

RAMFSNode* RAMFilesystem::alloc_node() {
    for (u32 i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].is_valid()) {
            nodes[i].init();
            nodes[i].id = next_id++;
            node_count++;
            return &nodes[i];
        }
    }
    return nullptr;
}

void RAMFilesystem::free_node(RAMFSNode* node) {
    if (!node || !node->is_valid()) return;
    
    // Free file data
    if (node->data) {
        used_bytes -= node->capacity;
        Heap::free(node->data);
    }
    
    // Remove from parent's children list
    if (node->parent_id != RAMFSNode::INVALID_ID) {
        RAMFSNode* parent = get_node(node->parent_id);
        if (parent) {
            for (u32 i = 0; i < parent->child_count; i++) {
                if (parent->children[i] == node->id) {
                    // Shift remaining children
                    for (u32 j = i; j < parent->child_count - 1; j++) {
                        parent->children[j] = parent->children[j + 1];
                    }
                    parent->child_count--;
                    break;
                }
            }
        }
    }
    
    node->init();
    node_count--;
}

RAMFSNode* RAMFilesystem::get_node(u32 id) {
    if (id == RAMFSNode::INVALID_ID) return nullptr;
    
    for (u32 i = 0; i < MAX_NODES; i++) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
    }
    return nullptr;
}

RAMFSNode* RAMFilesystem::find_child(RAMFSNode* parent, const char* name) {
    if (!parent || !parent->is_directory()) return nullptr;
    
    for (u32 i = 0; i < parent->child_count; i++) {
        RAMFSNode* child = get_node(parent->children[i]);
        if (child && str::cmp(child->name, name) == 0) {
            return child;
        }
    }
    return nullptr;
}

RAMFSNode* RAMFilesystem::find_node(const char* path) {
    if (!path || !root) return nullptr;
    
    // Handle root
    if (path[0] == '/' && path[1] == '\0') {
        return root;
    }
    
    // Normalize path
    char normalized[256];
    bolt::storage::path::normalize(path, normalized, sizeof(normalized));
    
    // Traverse path components
    RAMFSNode* current = root;
    const char* p = normalized;
    
    if (*p == '/') p++;  // Skip leading slash
    
    while (*p) {
        // Extract component
        char component[RAMFSNode::MAX_NAME];
        u32 len = 0;
        
        while (*p && *p != '/' && len < RAMFSNode::MAX_NAME - 1) {
            component[len++] = *p++;
        }
        component[len] = '\0';
        
        if (len == 0) {
            if (*p == '/') p++;
            continue;
        }
        
        // Find child
        current = find_child(current, component);
        if (!current) {
            return nullptr;
        }
        
        if (*p == '/') p++;
    }
    
    return current;
}

bool RAMFilesystem::split_path(const char* path, char* parent_path, char* name) {
    if (!path || !parent_path || !name) return false;
    
    char normalized[256];
    bolt::storage::path::normalize(path, normalized, sizeof(normalized));
    
    bolt::storage::path::dirname(normalized, parent_path, 256);
    bolt::storage::path::basename(normalized, name, RAMFSNode::MAX_NAME);
    
    return name[0] != '\0';
}

// ===========================================================================
// File Operations
// ===========================================================================

VFSResult RAMFilesystem::open(const char* path, FileMode mode, FileDescriptor& fd) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(path);
    
    // Create if needed
    if (!node && has_flag(mode, FileMode::Create)) {
        char parent_path[256];
        char name[RAMFSNode::MAX_NAME];
        
        if (!split_path(path, parent_path, name)) {
            return VFSResult::InvalidPath;
        }
        
        RAMFSNode* parent = find_node(parent_path);
        if (!parent || !parent->is_directory()) {
            return VFSResult::NotFound;
        }
        
        if (parent->child_count >= RAMFSNode::MAX_CHILDREN) {
            return VFSResult::NoSpace;
        }
        
        // Allocate new node
        node = alloc_node();
        if (!node) {
            return VFSResult::NoSpace;
        }
        
        str::cpy(node->name, name);
        node->type = FileType::Regular;
        node->parent_id = parent->id;
        node->permissions = 0644;
        
        // Add to parent
        parent->children[parent->child_count++] = node->id;
    }
    
    if (!node) {
        return VFSResult::NotFound;
    }
    
    if (node->is_directory()) {
        return VFSResult::IsDirectory;
    }
    
    // Truncate if requested
    if (has_flag(mode, FileMode::Truncate)) {
        if (node->data) {
            used_bytes -= node->capacity;
            Heap::free(node->data);
            node->data = nullptr;
            node->size = 0;
            node->capacity = 0;
        }
    }
    
    // Set up file descriptor
    fd.position = has_flag(mode, FileMode::Append) ? node->size : 0;
    fd.size = node->size;
    fd.inode = node->id;
    fd.mode = mode;
    fd.type = FileType::Regular;
    fd.fs_data = node;
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::close(FileDescriptor& fd) {
    fd.fs_data = nullptr;
    return VFSResult::Success;
}

VFSResult RAMFilesystem::read(FileDescriptor& fd, void* buffer, u64 size, u64& bytes_read) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = static_cast<RAMFSNode*>(fd.fs_data);
    if (!node) return VFSResult::BadDescriptor;
    
    bytes_read = 0;
    
    if (fd.position >= node->size) {
        return VFSResult::Success;  // EOF
    }
    
    u64 available = node->size - fd.position;
    u64 to_read = size < available ? size : available;
    
    if (to_read > 0 && node->data) {
        u8* src = node->data + fd.position;
        u8* dst = static_cast<u8*>(buffer);
        for (u64 i = 0; i < to_read; i++) {
            dst[i] = src[i];
        }
        fd.position += to_read;
        bytes_read = to_read;
    }
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::write(FileDescriptor& fd, const void* buffer, u64 size, u64& bytes_written) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = static_cast<RAMFSNode*>(fd.fs_data);
    if (!node) return VFSResult::BadDescriptor;
    
    if (!has_flag(fd.mode, FileMode::Write)) {
        return VFSResult::AccessDenied;
    }
    
    bytes_written = 0;
    
    if (size == 0) return VFSResult::Success;
    
    // Check size limits
    u64 new_end = fd.position + size;
    if (new_end > MAX_FILE_SIZE) {
        size = MAX_FILE_SIZE - fd.position;
        if (size == 0) return VFSResult::NoSpace;
        new_end = fd.position + size;
    }
    
    // Expand file if needed
    if (new_end > node->capacity) {
        if (!resize_file(node, new_end)) {
            return VFSResult::NoSpace;
        }
    }
    
    // Write data
    const u8* src = static_cast<const u8*>(buffer);
    u8* dst = node->data + fd.position;
    for (u64 i = 0; i < size; i++) {
        dst[i] = src[i];
    }
    
    fd.position += size;
    if (fd.position > node->size) {
        node->size = fd.position;
    }
    fd.size = node->size;
    bytes_written = size;
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::seek(FileDescriptor& fd, i64 offset, SeekMode mode) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = static_cast<RAMFSNode*>(fd.fs_data);
    if (!node) return VFSResult::BadDescriptor;
    
    i64 new_pos;
    
    switch (mode) {
        case SeekMode::Set:
            new_pos = offset;
            break;
        case SeekMode::Current:
            new_pos = static_cast<i64>(fd.position) + offset;
            break;
        case SeekMode::End:
            new_pos = static_cast<i64>(node->size) + offset;
            break;
        default:
            return VFSResult::InvalidArgument;
    }
    
    if (new_pos < 0) {
        return VFSResult::InvalidArgument;
    }
    
    fd.position = static_cast<u64>(new_pos);
    return VFSResult::Success;
}

bool RAMFilesystem::resize_file(RAMFSNode* node, u64 new_size) {
    // Round up to 4KB blocks
    u64 new_cap = (new_size + 4095) & ~4095ULL;
    if (new_cap > MAX_FILE_SIZE) new_cap = MAX_FILE_SIZE;
    
    // Check total memory limit
    u64 delta = new_cap - node->capacity;
    if (used_bytes + delta > MAX_TOTAL_SIZE) {
        return false;
    }
    
    u8* new_data = static_cast<u8*>(Heap::alloc(new_cap));
    if (!new_data) {
        return false;
    }
    
    // Copy existing data
    if (node->data && node->size > 0) {
        u64 copy_size = node->size < new_cap ? node->size : new_cap;
        for (u64 i = 0; i < copy_size; i++) {
            new_data[i] = node->data[i];
        }
        Heap::free(node->data);
    }
    
    // Zero new space
    for (u64 i = node->size; i < new_cap; i++) {
        new_data[i] = 0;
    }
    
    used_bytes -= node->capacity;
    used_bytes += new_cap;
    node->data = new_data;
    node->capacity = new_cap;
    
    return true;
}

// ===========================================================================
// Directory Operations
// ===========================================================================

VFSResult RAMFilesystem::opendir(const char* path, FileDescriptor& fd) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(path);
    if (!node) return VFSResult::NotFound;
    
    if (!node->is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    // Allocate directory state
    RAMFSDirState* state = static_cast<RAMFSDirState*>(Heap::alloc(sizeof(RAMFSDirState)));
    if (!state) return VFSResult::NoSpace;
    
    state->node_id = node->id;
    state->index = 0;
    
    fd.inode = node->id;
    fd.type = FileType::Directory;
    fd.fs_data = state;
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::readdir(FileDescriptor& fd, FileInfo& info) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSDirState* state = static_cast<RAMFSDirState*>(fd.fs_data);
    if (!state) return VFSResult::BadDescriptor;
    
    RAMFSNode* dir = get_node(state->node_id);
    if (!dir || !dir->is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    if (state->index >= dir->child_count) {
        return VFSResult::NotFound;  // End of directory
    }
    
    RAMFSNode* child = get_node(dir->children[state->index++]);
    if (!child) {
        return VFSResult::IOError;
    }
    
    fill_info(child, info);
    return VFSResult::Success;
}

VFSResult RAMFilesystem::closedir(FileDescriptor& fd) {
    if (fd.fs_data) {
        Heap::free(fd.fs_data);
        fd.fs_data = nullptr;
    }
    return VFSResult::Success;
}

VFSResult RAMFilesystem::mkdir(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    // Check if already exists
    if (find_node(path)) {
        return VFSResult::AlreadyExists;
    }
    
    char parent_path[256];
    char name[RAMFSNode::MAX_NAME];
    
    if (!split_path(path, parent_path, name)) {
        return VFSResult::InvalidPath;
    }
    
    RAMFSNode* parent = find_node(parent_path);
    if (!parent || !parent->is_directory()) {
        return VFSResult::NotFound;
    }
    
    if (parent->child_count >= RAMFSNode::MAX_CHILDREN) {
        return VFSResult::NoSpace;
    }
    
    RAMFSNode* node = alloc_node();
    if (!node) return VFSResult::NoSpace;
    
    str::cpy(node->name, name);
    node->type = FileType::Directory;
    node->parent_id = parent->id;
    node->permissions = 0755;
    
    parent->children[parent->child_count++] = node->id;
    
    return VFSResult::Success;
}

VFSResult RAMFilesystem::rmdir(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(path);
    if (!node) return VFSResult::NotFound;
    
    if (!node->is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    // Can't remove root
    if (node == root) {
        return VFSResult::AccessDenied;
    }
    
    // Must be empty
    if (node->child_count > 0) {
        return VFSResult::NotEmpty;
    }
    
    free_node(node);
    return VFSResult::Success;
}

// ===========================================================================
// File Management
// ===========================================================================

void RAMFilesystem::fill_info(RAMFSNode* node, FileInfo& info) {
    info.clear();
    str::cpy(info.name, node->name);
    info.type = node->type;
    info.size = node->size;
    info.inode = node->id;
    info.permissions = node->permissions;
    info.created = node->created;
    info.modified = node->modified;
    info.accessed = node->accessed;
    info.attributes = node->attributes;
}

VFSResult RAMFilesystem::stat(const char* path, FileInfo& info) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(path);
    if (!node) return VFSResult::NotFound;
    
    fill_info(node, info);
    return VFSResult::Success;
}

VFSResult RAMFilesystem::unlink(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(path);
    if (!node) return VFSResult::NotFound;
    
    if (node->is_directory()) {
        return VFSResult::IsDirectory;
    }
    
    free_node(node);
    return VFSResult::Success;
}

VFSResult RAMFilesystem::rename(const char* old_path, const char* new_path) {
    if (!mounted) return VFSResult::NotMounted;
    
    RAMFSNode* node = find_node(old_path);
    if (!node) return VFSResult::NotFound;
    
    // Can't rename root
    if (node == root) {
        return VFSResult::AccessDenied;
    }
    
    // Check destination doesn't exist
    if (find_node(new_path)) {
        return VFSResult::AlreadyExists;
    }
    
    char new_parent_path[256];
    char new_name[RAMFSNode::MAX_NAME];
    
    if (!split_path(new_path, new_parent_path, new_name)) {
        return VFSResult::InvalidPath;
    }
    
    RAMFSNode* new_parent = find_node(new_parent_path);
    if (!new_parent || !new_parent->is_directory()) {
        return VFSResult::NotFound;
    }
    
    // Remove from old parent
    RAMFSNode* old_parent = get_node(node->parent_id);
    if (old_parent) {
        for (u32 i = 0; i < old_parent->child_count; i++) {
            if (old_parent->children[i] == node->id) {
                for (u32 j = i; j < old_parent->child_count - 1; j++) {
                    old_parent->children[j] = old_parent->children[j + 1];
                }
                old_parent->child_count--;
                break;
            }
        }
    }
    
    // Add to new parent
    if (new_parent->child_count >= RAMFSNode::MAX_CHILDREN) {
        // Rollback
        if (old_parent) {
            old_parent->children[old_parent->child_count++] = node->id;
        }
        return VFSResult::NoSpace;
    }
    
    new_parent->children[new_parent->child_count++] = node->id;
    node->parent_id = new_parent->id;
    str::cpy(node->name, new_name);
    
    return VFSResult::Success;
}

u32 RAMFilesystem::directory_count() const {
    u32 count = 0;
    for (u32 i = 0; i < MAX_NODES; i++) {
        if (nodes[i].is_valid() && nodes[i].is_directory()) {
            count++;
        }
    }
    return count;
}

} // namespace bolt::storage
