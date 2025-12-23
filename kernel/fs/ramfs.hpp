#pragma once
/* ===========================================================================
 * BOLT OS - RAM Filesystem (RAMFS)
 * ===========================================================================
 * Simple in-memory filesystem for basic file operations
 * =========================================================================== */

#include "../lib/types.hpp"

namespace bolt::fs {

constexpr usize MAX_FILENAME = 32;
constexpr usize MAX_FILES = 64;
constexpr usize MAX_FILE_SIZE = 4096;

enum class FileType : u8 {
    Empty = 0,
    File,
    Directory
};

struct FileEntry {
    char name[MAX_FILENAME];
    FileType type;
    u32 size;
    u32 parent_index;    // Index of parent directory (0 = root)
    u8 data[MAX_FILE_SIZE];
};

struct FileHandle {
    u32 file_index;
    u32 position;
    bool valid;
};

class RAMFS {
public:
    static void init();
    
    // File operations
    static FileHandle open(const char* path);
    static void close(FileHandle& handle);
    static i32 read(FileHandle& handle, void* buffer, u32 size);
    static i32 write(FileHandle& handle, const void* data, u32 size);
    static bool seek(FileHandle& handle, u32 position);
    
    // File management
    static bool create(const char* path, FileType type = FileType::File);
    static bool remove(const char* path);
    static bool exists(const char* path);
    static u32 get_size(const char* path);
    
    // Directory operations
    static bool mkdir(const char* path);
    static i32 list_dir(const char* path, char names[][MAX_FILENAME], u32 max_entries);
    
    // Get file info
    static FileType get_type(const char* path);
    
    // Stats
    static u32 get_file_count();
    static u32 get_used_space();
    static u32 get_free_space();
    
private:
    static FileEntry files[MAX_FILES];
    static u32 file_count;
    
    static i32 find_file(const char* path);
    static i32 find_free_slot();
    static i32 find_parent(const char* path, char* filename);
    static void parse_path(const char* path, char* parent, char* name);
};

} // namespace bolt::fs
