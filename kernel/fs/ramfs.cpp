/* ===========================================================================
 * BOLT OS - RAM Filesystem Implementation
 * =========================================================================== */

#include "ramfs.hpp"
#include "../core/memory/heap.hpp"
#include "../lib/string.hpp"

namespace bolt::fs {

FileEntry RAMFS::files[MAX_FILES];
u32 RAMFS::file_count = 0;

void RAMFS::init() {
    // Clear all file entries
    mem::memset(files, 0, sizeof(files));
    file_count = 0;
    
    // Create root directory
    str::cpy(files[0].name, "/");
    files[0].type = FileType::Directory;
    files[0].size = 0;
    files[0].parent_index = 0;
    file_count = 1;
}

i32 RAMFS::find_file(const char* path) {
    for (u32 i = 0; i < MAX_FILES; i++) {
        if (files[i].type != FileType::Empty) {
            // Simple path comparison (full path stored in name for now)
            if (str::cmp(files[i].name, path) == 0) {
                return static_cast<i32>(i);
            }
        }
    }
    return -1;
}

i32 RAMFS::find_free_slot() {
    for (u32 i = 0; i < MAX_FILES; i++) {
        if (files[i].type == FileType::Empty) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

FileHandle RAMFS::open(const char* path) {
    FileHandle handle = {0, 0, false};
    
    i32 index = find_file(path);
    if (index >= 0 && files[index].type == FileType::File) {
        handle.file_index = static_cast<u32>(index);
        handle.position = 0;
        handle.valid = true;
    }
    
    return handle;
}

void RAMFS::close(FileHandle& handle) {
    handle.valid = false;
}

i32 RAMFS::read(FileHandle& handle, void* buffer, u32 size) {
    if (!handle.valid) return -1;
    
    FileEntry& file = files[handle.file_index];
    
    u32 available = file.size - handle.position;
    u32 to_read = (size < available) ? size : available;
    
    if (to_read > 0) {
        mem::memcpy(buffer, &file.data[handle.position], to_read);
        handle.position += to_read;
    }
    
    return static_cast<i32>(to_read);
}

i32 RAMFS::write(FileHandle& handle, const void* data, u32 size) {
    if (!handle.valid) return -1;
    
    FileEntry& file = files[handle.file_index];
    
    u32 space_left = MAX_FILE_SIZE - handle.position;
    u32 to_write = (size < space_left) ? size : space_left;
    
    if (to_write > 0) {
        mem::memcpy(&file.data[handle.position], data, to_write);
        handle.position += to_write;
        
        if (handle.position > file.size) {
            file.size = handle.position;
        }
    }
    
    return static_cast<i32>(to_write);
}

bool RAMFS::seek(FileHandle& handle, u32 position) {
    if (!handle.valid) return false;
    
    if (position <= files[handle.file_index].size) {
        handle.position = position;
        return true;
    }
    
    return false;
}

bool RAMFS::create(const char* path, FileType type) {
    if (find_file(path) >= 0) return false;  // Already exists
    
    i32 slot = find_free_slot();
    if (slot < 0) return false;  // No free slots
    
    str::cpy(files[slot].name, path);
    files[slot].type = type;
    files[slot].size = 0;
    files[slot].parent_index = 0;
    mem::memset(files[slot].data, 0, MAX_FILE_SIZE);
    
    file_count++;
    return true;
}

bool RAMFS::remove(const char* path) {
    i32 index = find_file(path);
    if (index <= 0) return false;  // Can't delete root or non-existent
    
    // Don't delete non-empty directories
    if (files[index].type == FileType::Directory) {
        for (u32 i = 0; i < MAX_FILES; i++) {
            if (files[i].parent_index == static_cast<u32>(index) && 
                files[i].type != FileType::Empty) {
                return false;
            }
        }
    }
    
    files[index].type = FileType::Empty;
    files[index].name[0] = '\0';
    file_count--;
    
    return true;
}

bool RAMFS::exists(const char* path) {
    return find_file(path) >= 0;
}

u32 RAMFS::get_size(const char* path) {
    i32 index = find_file(path);
    if (index < 0) return 0;
    return files[index].size;
}

bool RAMFS::mkdir(const char* path) {
    return create(path, FileType::Directory);
}

i32 RAMFS::list_dir(const char* path, char names[][MAX_FILENAME], u32 max_entries) {
    i32 dir_index = find_file(path);
    if (dir_index < 0) return -1;
    if (files[dir_index].type != FileType::Directory) return -1;
    
    u32 count = 0;
    usize path_len = str::len(path);
    
    // Normalize: ensure path doesn't end with / (except root)
    char normalized_path[MAX_FILENAME];
    str::cpy(normalized_path, path);
    if (path_len > 1 && normalized_path[path_len - 1] == '/') {
        normalized_path[path_len - 1] = '\0';
        path_len--;
    }
    
    for (u32 i = 1; i < MAX_FILES && count < max_entries; i++) {
        if (files[i].type != FileType::Empty) {
            const char* name = files[i].name;
            
            // For root directory
            if (path_len == 1 && normalized_path[0] == '/') {
                // Check if it's a direct child of root (one slash, at start)
                if (name[0] == '/') {
                    int slashes = 0;
                    for (const char* p = name; *p; p++) {
                        if (*p == '/') slashes++;
                    }
                    if (slashes == 1) {
                        str::cpy(names[count], name + 1);  // Skip leading /
                        count++;
                    }
                }
            }
            // For subdirectories
            else {
                // Check if file starts with "path/"
                if (str::ncmp(name, normalized_path, path_len) == 0 && name[path_len] == '/') {
                    // Check it's a direct child (no more slashes after our prefix)
                    const char* rest = name + path_len + 1;
                    bool has_more_slashes = false;
                    for (const char* p = rest; *p; p++) {
                        if (*p == '/') {
                            has_more_slashes = true;
                            break;
                        }
                    }
                    if (!has_more_slashes && rest[0] != '\0') {
                        str::cpy(names[count], rest);
                        count++;
                    }
                }
            }
        }
    }
    
    return static_cast<i32>(count);
}

FileType RAMFS::get_type(const char* path) {
    i32 index = find_file(path);
    if (index < 0) return FileType::Empty;
    return files[index].type;
}

u32 RAMFS::get_file_count() {
    return file_count;
}

u32 RAMFS::get_used_space() {
    u32 total = 0;
    for (u32 i = 0; i < MAX_FILES; i++) {
        if (files[i].type == FileType::File) {
            total += files[i].size;
        }
    }
    return total;
}

u32 RAMFS::get_free_space() {
    return (MAX_FILES - file_count) * MAX_FILE_SIZE;
}

} // namespace bolt::fs
