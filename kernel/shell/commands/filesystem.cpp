/* ===========================================================================
 * BOLT OS - Filesystem Commands Implementation (VFS-based)
 * 
 * All filesystem commands now use VFS, which routes to the mounted
 * filesystem (FAT32 on disk, or RAMFS fallback).
 * =========================================================================== */

#include "filesystem.hpp"
#include "../shell.hpp"
#include "../../drivers/video/console.hpp"
#include "../../drivers/video/framebuffer.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../drivers/input/keyboard.hpp"
#include "../../storage/vfs.hpp"
#include "../../lib/string.hpp"

namespace bolt::shell::cmd {

using namespace drivers;
using namespace storage;

void ls(int argc, char** argv) {
    DBG("CMD", "ls: Listing directory");
    
    char path[128];
    if (argc > 1) {
        Shell::resolve_path(argv[1], path);
    } else {
        Shell::resolve_path(".", path);
    }
    
    Serial::log("CMD", LogType::Debug, "  Path: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open directory via VFS
    u32 fd = 0;
    VFSResult result = VFS::opendir(path, fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "opendir failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::print("Error: ");
        Console::println(vfs_result_string(result));
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG_OK("CMD", "Directory opened");
    
    Console::set_color(Color::LightCyan);
    Console::print("Contents of ");
    Console::println(path);
    Console::set_color(Color::LightGray);
    
    // Read directory entries
    FileInfo info;
    i32 count = 0;
    
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        Console::print("  ");
        
        if (info.is_directory()) {
            Console::set_color(Color::LightBlue);
            Console::print(info.name);
            Console::println("/");
        } else {
            Console::set_color(Color::White);
            Console::print(info.name);
            Console::print("  (");
            Console::print_dec(static_cast<i32>(info.size));
            Console::println(" bytes)");
        }
        count++;
    }
    
    VFS::closedir(fd);
    
    if (count == 0) {
        Console::println("  (empty)");
    }
    
    Console::set_color(Color::LightGray);
    
    Serial::log("CMD", LogType::Success, "Listed ", path);
}

void cd(int argc, char** argv) {
    DBG("CMD", "cd: Changing directory");
    
    if (argc < 2) {
        Serial::log("CMD", LogType::Debug, "  Target: ", "/");
        Shell::set_cwd("/");
        return;
    }
    
    const char* target = argv[1];
    char newpath[128];
    
    Serial::log("CMD", LogType::Debug, "  Target: ", target);
    
    if (str::cmp(target, "/") == 0) {
        Shell::set_cwd("/");
        DBG_OK("CMD", "Changed to /");
        return;
    }
    
    if (str::cmp(target, "..") == 0) {
        char cwd[128];
        Shell::get_cwd(cwd);
        usize len = str::len(cwd);
        if (len <= 1) return;
        
        if (cwd[len - 1] == '/') {
            cwd[len - 1] = '\0';
            len--;
        }
        
        while (len > 0 && cwd[len - 1] != '/') {
            len--;
        }
        
        if (len == 0) {
            Shell::set_cwd("/");
        } else {
            cwd[len] = '\0';
            Shell::set_cwd(cwd);
        }
        
        Shell::get_cwd(cwd);
        Serial::log("CMD", LogType::Success, "Changed to ", cwd);
        return;
    }
    
    if (str::cmp(target, ".") == 0) {
        return;
    }
    
    Shell::resolve_path(target, newpath);
    Serial::log("CMD", LogType::Debug, "  Resolved: ", newpath);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Check if path exists and is a directory
    if (!VFS::exists(newpath)) {
        Serial::log("CMD", LogType::Error, "Path not found: ", newpath);
        Console::set_color(Color::LightRed);
        Console::print("No such directory: ");
        Console::println(newpath);
        Console::set_color(Color::LightGray);
        return;
    }
    
    if (!VFS::is_directory(newpath)) {
        Serial::log("CMD", LogType::Error, "Not a directory: ", newpath);
        Console::set_color(Color::LightRed);
        Console::print("Not a directory: ");
        Console::println(newpath);
        Console::set_color(Color::LightGray);
        return;
    }
    
    Shell::set_cwd(newpath);
    Serial::log("CMD", LogType::Success, "Changed to ", newpath);
}

void pwd() {
    DBG("CMD", "pwd: Print working directory");
    
    char cwd[128];
    Shell::get_cwd(cwd);
    
    Serial::log("CMD", LogType::Debug, "  CWD: ", cwd);
    Console::println(cwd);
}

void cat(int argc, char** argv) {
    DBG("CMD", "cat: Display file contents");
    
    if (argc < 2) {
        Console::println("Usage: cat <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  File: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Check if file exists
    if (!VFS::exists(path)) {
        Serial::log("CMD", LogType::Error, "File not found: ", path);
        Console::set_color(Color::LightRed);
        Console::print("File not found: ");
        Console::println(path);
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open file
    u32 fd = 0;
    VFSResult result = VFS::open(path, FileMode::Read, fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "open failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Error opening file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG_OK("CMD", "File opened");
    
    // Read and display file contents
    char buffer[256];
    u64 bytes_read = 0;
    u64 total_read = 0;
    
    while (VFS::read(fd, buffer, 255, bytes_read) == VFSResult::Success && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        Console::print(buffer);
        total_read += bytes_read;
    }
    Console::println("");
    
    VFS::close(fd);
    
    Serial::log("CMD", LogType::Success, "Displayed file: ", path);
}

void write(int argc, char** argv) {
    DBG("CMD", "write: Write to file");
    
    if (argc < 3) {
        Console::println("Usage: write <filename> <text>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  File: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open file for writing (create if doesn't exist)
    u32 fd = 0;
    VFSResult result = VFS::open(path, FileMode::Write | FileMode::Create, fd);
    
    if (result != VFSResult::Success) {
        Serial::log("CMD", LogType::Error, "open failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Error opening file for writing");
        Console::set_color(Color::LightGray);
        return;
    }
    
    DBG_OK("CMD", "File opened for writing");
    
    // Write all arguments as text
    u64 bytes_written = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2) {
            VFS::write(fd, " ", 1, bytes_written);
        }
        VFS::write(fd, argv[i], str::len(argv[i]), bytes_written);
    }
    VFS::write(fd, "\n", 1, bytes_written);
    
    VFS::close(fd);
    
    Console::set_color(Color::LightGreen);
    Console::println("Written.");
    Console::set_color(Color::LightGray);
    
    Serial::log("CMD", LogType::Success, "Written to: ", path);
}

void touch(int argc, char** argv) {
    DBG("CMD", "touch: Create file");
    
    if (argc < 2) {
        Console::println("Usage: touch <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  File: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    if (VFS::exists(path)) {
        DBG_WARN("CMD", "File already exists");
        Console::println("File already exists.");
        return;
    }
    
    // Create empty file by opening with Create flag
    u32 fd = 0;
    VFSResult result = VFS::open(path, FileMode::Write | FileMode::Create, fd);
    
    if (result == VFSResult::Success) {
        VFS::close(fd);
        Console::set_color(Color::LightGreen);
        Console::println("File created.");
        Serial::log("CMD", LogType::Success, "Created: ", path);
    } else {
        Serial::log("CMD", LogType::Error, "create failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to create file.");
    }
    Console::set_color(Color::LightGray);
}

void rm(int argc, char** argv) {
    DBG("CMD", "rm: Remove file");
    
    if (argc < 2) {
        Console::println("Usage: rm <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  File: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    VFSResult result = VFS::unlink(path);
    
    if (result == VFSResult::Success) {
        Console::set_color(Color::LightGreen);
        Console::println("Removed.");
        Serial::log("CMD", LogType::Success, "Removed: ", path);
    } else {
        Serial::log("CMD", LogType::Error, "unlink failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to remove.");
    }
    Console::set_color(Color::LightGray);
}

void mkdir_cmd(int argc, char** argv) {
    DBG("CMD", "mkdir: Create directory");
    
    if (argc < 2) {
        Console::println("Usage: mkdir <dirname>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  Directory: ", path);
    
    // Check if VFS is ready
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    VFSResult result = VFS::mkdir(path);
    
    if (result == VFSResult::Success) {
        Console::set_color(Color::LightGreen);
        Console::println("Directory created.");
        Serial::log("CMD", LogType::Success, "Created dir: ", path);
    } else {
        Serial::log("CMD", LogType::Error, "mkdir failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to create directory.");
    }
    Console::set_color(Color::LightGray);
}

// ===========================================================================
// Advanced Filesystem Commands
// ===========================================================================

void rmdir_cmd(int argc, char** argv) {
    DBG("CMD", "rmdir: Remove directory");
    
    if (argc < 2) {
        Console::println("Usage: rmdir <dirname>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    Serial::log("CMD", LogType::Debug, "  Directory: ", path);
    
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Check if it's a directory
    if (!VFS::is_directory(path)) {
        Console::set_color(Color::LightRed);
        Console::println("Not a directory");
        Console::set_color(Color::LightGray);
        return;
    }
    
    VFSResult result = VFS::rmdir(path);
    
    if (result == VFSResult::Success) {
        Console::set_color(Color::LightGreen);
        Console::println("Directory removed.");
        Serial::log("CMD", LogType::Success, "Removed dir: ", path);
    } else if (result == VFSResult::NotEmpty) {
        Serial::log("CMD", LogType::Error, "rmdir failed: directory not empty");
        Console::set_color(Color::LightRed);
        Console::println("Directory not empty.");
    } else {
        Serial::log("CMD", LogType::Error, "rmdir failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Failed to remove directory.");
    }
    Console::set_color(Color::LightGray);
}

void cp(int argc, char** argv) {
    DBG("CMD", "cp: Copy file");
    
    if (argc < 3) {
        Console::println("Usage: cp <source> <destination>");
        return;
    }
    
    char src_path[128], dst_path[128];
    Shell::resolve_path(argv[1], src_path);
    Shell::resolve_path(argv[2], dst_path);
    
    Serial::log("CMD", LogType::Debug, "  Source: ", src_path);
    Serial::log("CMD", LogType::Debug, "  Dest: ", dst_path);
    
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Check source exists
    if (!VFS::exists(src_path)) {
        Console::set_color(Color::LightRed);
        Console::println("Source file not found");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Can't copy directories (yet)
    if (VFS::is_directory(src_path)) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot copy directories (not implemented)");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open source for reading
    u32 src_fd = 0;
    VFSResult result = VFS::open(src_path, FileMode::Read, src_fd);
    if (result != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot open source file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Open destination for writing
    u32 dst_fd = 0;
    result = VFS::open(dst_path, FileMode::Write | FileMode::Create | FileMode::Truncate, dst_fd);
    if (result != VFSResult::Success) {
        VFS::close(src_fd);
        Console::set_color(Color::LightRed);
        Console::println("Cannot create destination file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Copy in chunks
    char buffer[512];
    u64 bytes_read = 0, bytes_written = 0;
    u64 total_copied = 0;
    
    while (VFS::read(src_fd, buffer, sizeof(buffer), bytes_read) == VFSResult::Success && bytes_read > 0) {
        VFS::write(dst_fd, buffer, bytes_read, bytes_written);
        total_copied += bytes_written;
    }
    
    VFS::close(src_fd);
    VFS::close(dst_fd);
    
    Console::set_color(Color::LightGreen);
    Console::print("Copied ");
    Console::print_dec(static_cast<i32>(total_copied));
    Console::println(" bytes.");
    Console::set_color(Color::LightGray);
    
    Serial::log("CMD", LogType::Success, "Copied file");
}

void mv(int argc, char** argv) {
    DBG("CMD", "mv: Move/rename file");
    
    if (argc < 3) {
        Console::println("Usage: mv <source> <destination>");
        return;
    }
    
    char src_path[128], dst_path[128];
    Shell::resolve_path(argv[1], src_path);
    Shell::resolve_path(argv[2], dst_path);
    
    Serial::log("CMD", LogType::Debug, "  Source: ", src_path);
    Serial::log("CMD", LogType::Debug, "  Dest: ", dst_path);
    
    if (!VFS::is_ready()) {
        DBG_FAIL("CMD", "VFS not ready");
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Check source exists
    if (!VFS::exists(src_path)) {
        Console::set_color(Color::LightRed);
        Console::println("Source not found");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Try rename first (for same filesystem)
    VFSResult result = VFS::rename(src_path, dst_path);
    
    if (result == VFSResult::Success) {
        Console::set_color(Color::LightGreen);
        Console::println("Moved.");
        Serial::log("CMD", LogType::Success, "Moved/renamed");
    } else if (result == VFSResult::Unsupported) {
        // Fallback: copy + delete (for files only)
        if (!VFS::is_directory(src_path)) {
            // Simulate with cp + rm
            char* fake_argv[3] = {argv[0], argv[1], argv[2]};
            cp(3, fake_argv);
            
            result = VFS::unlink(src_path);
            if (result == VFSResult::Success) {
                Console::set_color(Color::LightGreen);
                Console::println("Moved (via copy).");
            }
        } else {
            Console::set_color(Color::LightRed);
            Console::println("Cannot move directories across filesystems");
        }
    } else {
        Serial::log("CMD", LogType::Error, "mv failed: ", vfs_result_string(result));
        Console::set_color(Color::LightRed);
        Console::println("Move failed.");
    }
    Console::set_color(Color::LightGray);
}

void stat_cmd(int argc, char** argv) {
    DBG("CMD", "stat: File statistics");
    
    if (argc < 2) {
        Console::println("Usage: stat <path>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    FileInfo info;
    VFSResult result = VFS::stat(path, info);
    
    if (result != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::print("Cannot stat: ");
        Console::println(path);
        Console::set_color(Color::LightGray);
        return;
    }
    
    Console::set_color(Color::LightCyan);
    Console::print("  File: ");
    Console::set_color(Color::White);
    Console::println(path);
    
    Console::set_color(Color::LightCyan);
    Console::print("  Type: ");
    Console::set_color(Color::White);
    if (info.is_directory()) {
        Console::println("Directory");
    } else {
        Console::println("Regular file");
    }
    
    Console::set_color(Color::LightCyan);
    Console::print("  Size: ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(info.size));
    Console::println(" bytes");
    
    Console::set_color(Color::LightCyan);
    Console::print(" Inode: ");
    Console::set_color(Color::White);
    Console::print_dec(static_cast<i32>(info.inode));
    Console::println("");
    
    Console::set_color(Color::LightCyan);
    Console::print(" Attrs: ");
    Console::set_color(Color::White);
    if (info.attributes & 0x01) Console::print("R");
    if (info.attributes & 0x02) Console::print("H");
    if (info.attributes & 0x04) Console::print("S");
    if (info.attributes & 0x10) Console::print("D");
    if (info.attributes & 0x20) Console::print("A");
    Console::println("");
    
    Console::set_color(Color::LightGray);
}

// Helper for tree command
static void tree_recurse(const char* path, int depth, int* file_count, int* dir_count, bool* last_at_depth) {
    if (depth > 8) return;  // Prevent infinite recursion
    
    u32 fd = 0;
    if (VFS::opendir(path, fd) != VFSResult::Success) return;
    
    // First, count entries to know when we're at the last one
    FileInfo info;
    int entry_count = 0;
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        if (str::cmp(info.name, ".") == 0 || str::cmp(info.name, "..") == 0) continue;
        entry_count++;
    }
    VFS::closedir(fd);
    
    // Reopen and iterate
    if (VFS::opendir(path, fd) != VFSResult::Success) return;
    
    int current_entry = 0;
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        // Skip . and ..
        if (str::cmp(info.name, ".") == 0 || str::cmp(info.name, "..") == 0) continue;
        
        current_entry++;
        bool is_last = (current_entry == entry_count);
        
        // Print indent with proper tree lines
        for (int i = 0; i < depth; i++) {
            if (last_at_depth[i]) {
                Console::print("    ");      // No more siblings at this level
            } else {
                Console::print("|   ");      // More siblings coming
            }
        }
        
        // Print branch
        if (is_last) {
            Console::print("`-- ");          // Last item uses corner
        } else {
            Console::print("|-- ");          // More items follow
        }
        
        if (info.is_directory()) {
            Console::set_color(Color::LightBlue);
            Console::println(info.name);
            (*dir_count)++;
            
            // Build full path for recursion
            char subpath[256];
            str::cpy(subpath, path);
            if (subpath[str::len(subpath) - 1] != '/') str::cat(subpath, "/");
            str::cat(subpath, info.name);
            
            last_at_depth[depth] = is_last;
            tree_recurse(subpath, depth + 1, file_count, dir_count, last_at_depth);
        } else {
            Console::set_color(Color::White);
            Console::println(info.name);
            (*file_count)++;
        }
    }
    
    VFS::closedir(fd);
    Console::set_color(Color::LightGray);
}

void tree(int argc, char** argv) {
    DBG("CMD", "tree: Directory tree");
    
    char path[128];
    if (argc > 1) {
        Shell::resolve_path(argv[1], path);
    } else {
        Shell::resolve_path(".", path);
    }
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    Console::set_color(Color::LightBlue);
    // Print root as "/" instead of potentially garbage
    if (str::cmp(path, "/") == 0 || path[0] == 0) {
        Console::println("/");
    } else {
        Console::println(path);
    }
    
    int file_count = 0, dir_count = 0;
    bool last_at_depth[16] = {false};  // Track if we're at last item per depth
    tree_recurse(path, 0, &file_count, &dir_count, last_at_depth);
    
    Console::set_color(Color::LightGray);
    Console::print("\n");
    Console::print_dec(dir_count);
    Console::print(" directories, ");
    Console::print_dec(file_count);
    Console::println(" files");
}

void df() {
    DBG("CMD", "df: Disk free space");
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    Console::set_color(Color::LightCyan);
    Console::println("Filesystem      Type       Mounted on");
    Console::set_color(Color::White);
    
    // Get mount info from VFS
    Console::print("/dev/hda1       FAT32      /\n");
    Console::println("(Detailed disk stats not yet implemented)");
    
    Console::set_color(Color::LightGray);
}

void head(int argc, char** argv) {
    DBG("CMD", "head: First lines of file");
    
    if (argc < 2) {
        Console::println("Usage: head [-n lines] <filename>");
        return;
    }
    
    int lines = 10;  // Default
    const char* filename = nullptr;
    
    for (int i = 1; i < argc; i++) {
        if (str::cmp(argv[i], "-n") == 0 && i + 1 < argc) {
            lines = 0;
            for (const char* p = argv[++i]; *p >= '0' && *p <= '9'; p++) {
                lines = lines * 10 + (*p - '0');
            }
        } else {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        Console::println("Usage: head [-n lines] <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(filename, path);
    
    if (!VFS::is_ready() || !VFS::exists(path)) {
        Console::set_color(Color::LightRed);
        Console::println("File not found");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u32 fd = 0;
    if (VFS::open(path, FileMode::Read, fd) != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot open file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    char buffer[512];
    u64 bytes_read = 0;
    int line_count = 0;
    
    while (line_count < lines && VFS::read(fd, buffer, sizeof(buffer) - 1, bytes_read) == VFSResult::Success && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        
        for (u64 i = 0; i < bytes_read && line_count < lines; i++) {
            Console::putchar(buffer[i]);
            if (buffer[i] == '\n') line_count++;
        }
    }
    
    if (line_count > 0 && buffer[0] != '\n') Console::println("");
    VFS::close(fd);
}

void tail(int argc, char** argv) {
    DBG("CMD", "tail: Last lines of file");
    
    if (argc < 2) {
        Console::println("Usage: tail [-n lines] <filename>");
        return;
    }
    
    int lines = 10;
    const char* filename = nullptr;
    
    for (int i = 1; i < argc; i++) {
        if (str::cmp(argv[i], "-n") == 0 && i + 1 < argc) {
            lines = 0;
            for (const char* p = argv[++i]; *p >= '0' && *p <= '9'; p++) {
                lines = lines * 10 + (*p - '0');
            }
        } else {
            filename = argv[i];
        }
    }
    
    if (!filename) {
        Console::println("Usage: tail [-n lines] <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(filename, path);
    
    if (!VFS::is_ready() || !VFS::exists(path)) {
        Console::set_color(Color::LightRed);
        Console::println("File not found");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Read entire file to find last N lines (simple approach)
    u32 fd = 0;
    if (VFS::open(path, FileMode::Read, fd) != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot open file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Buffer the whole file (limited size)
    static char file_buffer[4096];
    u64 total_read = 0;
    u64 bytes_read = 0;
    
    while (VFS::read(fd, file_buffer + total_read, sizeof(file_buffer) - total_read - 1, bytes_read) == VFSResult::Success && bytes_read > 0) {
        total_read += bytes_read;
        if (total_read >= sizeof(file_buffer) - 1) break;
    }
    file_buffer[total_read] = '\0';
    VFS::close(fd);
    
    // Count newlines from end
    int newline_count = 0;
    i64 start_pos = static_cast<i64>(total_read) - 1;
    
    while (start_pos >= 0 && newline_count < lines) {
        if (file_buffer[start_pos] == '\n') newline_count++;
        if (newline_count < lines) start_pos--;
    }
    
    if (start_pos < 0) start_pos = 0;
    else if (file_buffer[start_pos] == '\n') start_pos++;
    
    Console::print(file_buffer + start_pos);
    if (total_read > 0 && file_buffer[total_read - 1] != '\n') Console::println("");
}

void append(int argc, char** argv) {
    DBG("CMD", "append: Append to file");
    
    if (argc < 3) {
        Console::println("Usage: append <filename> <text>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u32 fd = 0;
    VFSResult result = VFS::open(path, FileMode::Write | FileMode::Create | FileMode::Append, fd);
    
    if (result != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot open file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u64 bytes_written = 0;
    for (int i = 2; i < argc; i++) {
        if (i > 2) VFS::write(fd, " ", 1, bytes_written);
        VFS::write(fd, argv[i], str::len(argv[i]), bytes_written);
    }
    VFS::write(fd, "\n", 1, bytes_written);
    
    VFS::close(fd);
    
    Console::set_color(Color::LightGreen);
    Console::println("Appended.");
    Console::set_color(Color::LightGray);
}

// Helper for find command
static void find_recurse(const char* path, const char* pattern, int* count) {
    u32 fd = 0;
    if (VFS::opendir(path, fd) != VFSResult::Success) return;
    
    FileInfo info;
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        if (str::cmp(info.name, ".") == 0 || str::cmp(info.name, "..") == 0) continue;
        
        // Build full path
        char fullpath[256];
        str::cpy(fullpath, path);
        if (fullpath[str::len(fullpath) - 1] != '/') str::cat(fullpath, "/");
        str::cat(fullpath, info.name);
        
        // Check if name matches pattern (simple substring match)
        bool match = false;
        if (pattern[0] == '*') {
            // Wildcard at start: match suffix
            const char* suffix = pattern + 1;
            usize name_len = str::len(info.name);
            usize suffix_len = str::len(suffix);
            if (name_len >= suffix_len) {
                match = str::cmp(info.name + name_len - suffix_len, suffix) == 0;
            }
        } else if (pattern[str::len(pattern) - 1] == '*') {
            // Wildcard at end: match prefix
            usize pattern_len = str::len(pattern) - 1;
            match = str::ncmp(info.name, pattern, pattern_len) == 0;
        } else {
            // Exact match or substring
            match = str::cmp(info.name, pattern) == 0;
        }
        
        if (match) {
            Console::println(fullpath);
            (*count)++;
        }
        
        // Recurse into directories
        if (info.is_directory()) {
            find_recurse(fullpath, pattern, count);
        }
    }
    
    VFS::closedir(fd);
}

void find_cmd(int argc, char** argv) {
    DBG("CMD", "find: Search for files");
    
    if (argc < 2) {
        Console::println("Usage: find <pattern> [path]");
        Console::println("  Pattern: name, *.ext, prefix*");
        return;
    }
    
    const char* pattern = argv[1];
    char path[128];
    
    if (argc > 2) {
        Shell::resolve_path(argv[2], path);
    } else {
        Shell::resolve_path(".", path);
    }
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    int count = 0;
    find_recurse(path, pattern, &count);
    
    Console::set_color(Color::LightGray);
    Console::print("\nFound ");
    Console::print_dec(count);
    Console::println(" matches.");
}

void wc(int argc, char** argv) {
    DBG("CMD", "wc: Word count");
    
    if (argc < 2) {
        Console::println("Usage: wc <filename>");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    if (!VFS::is_ready() || !VFS::exists(path)) {
        Console::set_color(Color::LightRed);
        Console::println("File not found");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u32 fd = 0;
    if (VFS::open(path, FileMode::Read, fd) != VFSResult::Success) {
        Console::set_color(Color::LightRed);
        Console::println("Cannot open file");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u32 lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    char buffer[256];
    u64 bytes_read = 0;
    
    while (VFS::read(fd, buffer, sizeof(buffer), bytes_read) == VFSResult::Success && bytes_read > 0) {
        for (u64 i = 0; i < bytes_read; i++) {
            bytes++;
            char c = buffer[i];
            
            if (c == '\n') lines++;
            
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }
    
    VFS::close(fd);
    
    Console::print("  ");
    Console::print_dec(static_cast<i32>(lines));
    Console::print("  ");
    Console::print_dec(static_cast<i32>(words));
    Console::print("  ");
    Console::print_dec(static_cast<i32>(bytes));
    Console::print("  ");
    Console::println(argv[1]);
}

// Helper for du command
static u64 du_recurse(const char* path, bool show_all) {
    u64 total = 0;
    
    u32 fd = 0;
    if (VFS::opendir(path, fd) != VFSResult::Success) return 0;
    
    FileInfo info;
    while (VFS::readdir(fd, info) == VFSResult::Success) {
        if (str::cmp(info.name, ".") == 0 || str::cmp(info.name, "..") == 0) continue;
        
        char fullpath[256];
        str::cpy(fullpath, path);
        if (fullpath[str::len(fullpath) - 1] != '/') str::cat(fullpath, "/");
        str::cat(fullpath, info.name);
        
        if (info.is_directory()) {
            u64 dir_size = du_recurse(fullpath, show_all);
            total += dir_size;
            
            if (show_all) {
                Console::print_dec(static_cast<i32>(dir_size / 1024));
                Console::print("K\t");
                Console::println(fullpath);
            }
        } else {
            total += info.size;
            
            if (show_all) {
                Console::print_dec(static_cast<i32>(info.size / 1024));
                Console::print("K\t");
                Console::println(fullpath);
            }
        }
    }
    
    VFS::closedir(fd);
    return total;
}

void du(int argc, char** argv) {
    DBG("CMD", "du: Disk usage");
    
    char path[128];
    bool show_all = false;
    
    for (int i = 1; i < argc; i++) {
        if (str::cmp(argv[i], "-a") == 0) {
            show_all = true;
        } else {
            Shell::resolve_path(argv[i], path);
        }
    }
    
    if (argc < 2 || (argc == 2 && show_all)) {
        Shell::resolve_path(".", path);
    }
    
    if (!VFS::is_ready()) {
        Console::set_color(Color::LightRed);
        Console::println("Filesystem not ready");
        Console::set_color(Color::LightGray);
        return;
    }
    
    u64 total = du_recurse(path, show_all);
    
    Console::print_dec(static_cast<i32>(total / 1024));
    Console::print("K\t");
    Console::println(path);
}

// ===========================================================================
// Simple Text Editor (Framebuffer-based)
// ===========================================================================

static constexpr int EDITOR_MAX_LINES = 100;
static constexpr int EDITOR_MAX_LINE_LEN = 120;
static char editor_lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LEN];
static int editor_line_count = 0;
static int editor_cursor_line = 0;
static int editor_cursor_col = 0;
static int editor_scroll_offset = 0;
static bool editor_modified = false;
static char editor_filename[128];

// Screen layout
static constexpr int EDITOR_COLS = 80;
static constexpr int EDITOR_ROWS = 45;  // Visible text rows (adjust for your resolution)
static constexpr int EDITOR_STATUS_ROW = EDITOR_ROWS;
static constexpr int EDITOR_HELP_ROW = EDITOR_ROWS + 1;

static void editor_draw_line(int screen_row, int file_line) {
    Framebuffer::set_cursor(0, screen_row);
    
    if (file_line < editor_line_count) {
        // Line number (4 chars)
        char numstr[8];
        int linenum = file_line + 1;
        int pos = 0;
        if (linenum < 10) { numstr[pos++] = ' '; numstr[pos++] = ' '; numstr[pos++] = ' '; }
        else if (linenum < 100) { numstr[pos++] = ' '; numstr[pos++] = ' '; }
        else if (linenum < 1000) { numstr[pos++] = ' '; }
        
        // Convert number
        char tmp[8];
        int tpos = 0;
        do { tmp[tpos++] = '0' + (linenum % 10); linenum /= 10; } while (linenum > 0);
        while (tpos > 0) numstr[pos++] = tmp[--tpos];
        numstr[pos++] = ' ';
        numstr[pos] = '\0';
        
        Framebuffer::draw_string(0, screen_row * 16, numstr, Color32::DarkGray());
        
        // Line content
        const char* line = editor_lines[file_line];
        int max_chars = EDITOR_COLS - 5;
        char display[128];
        int i;
        for (i = 0; i < max_chars && line[i]; i++) {
            display[i] = line[i];
        }
        for (; i < max_chars; i++) display[i] = ' ';  // Clear rest of line
        display[max_chars] = '\0';
        
        Framebuffer::draw_string(5 * 8, screen_row * 16, display, Color32::Text());
    } else {
        // Empty line - show tilde
        char empty[EDITOR_COLS + 1];
        empty[0] = '~';
        for (int i = 1; i < EDITOR_COLS; i++) empty[i] = ' ';
        empty[EDITOR_COLS] = '\0';
        Framebuffer::draw_string(0, screen_row * 16, empty, Color32::DarkGray());
    }
}

static void editor_draw_status() {
    // Status bar
    char status[128];
    str::cpy(status, " EDIT: ");
    str::cat(status, editor_filename);
    str::cat(status, " | L");
    
    // Convert line number
    char numstr[16];
    int n = editor_cursor_line + 1;
    int pos = 0;
    char tmp[16];
    do { tmp[pos++] = '0' + (n % 10); n /= 10; } while (n > 0);
    int npos = 0;
    while (pos > 0) numstr[npos++] = tmp[--pos];
    numstr[npos] = '\0';
    str::cat(status, numstr);
    
    str::cat(status, "/");
    n = editor_line_count;
    pos = 0;
    do { tmp[pos++] = '0' + (n % 10); n /= 10; } while (n > 0);
    npos = 0;
    while (pos > 0) numstr[npos++] = tmp[--pos];
    numstr[npos] = '\0';
    str::cat(status, numstr);
    
    str::cat(status, " C");
    n = editor_cursor_col + 1;
    pos = 0;
    do { tmp[pos++] = '0' + (n % 10); n /= 10; } while (n > 0);
    npos = 0;
    while (pos > 0) numstr[npos++] = tmp[--pos];
    numstr[npos] = '\0';
    str::cat(status, numstr);
    
    if (editor_modified) str::cat(status, " [+]");
    
    // Pad to full width
    int len = str::len(status);
    for (int i = len; i < EDITOR_COLS; i++) status[i] = ' ';
    status[EDITOR_COLS] = '\0';
    
    Framebuffer::fill_rect(0, EDITOR_STATUS_ROW * 16, EDITOR_COLS * 8, 16, Color32::White());
    Framebuffer::draw_string(0, EDITOR_STATUS_ROW * 16, status, Color32::Black(), Color32::White());
    
    // Help bar
    const char* help = " Ctrl+S:Save  Ctrl+Q:Quit  Ctrl+K:DelLine  Enter:NewLine  Arrows:Move";
    char helpstr[128];
    str::cpy(helpstr, help);
    len = str::len(helpstr);
    for (int i = len; i < EDITOR_COLS; i++) helpstr[i] = ' ';
    helpstr[EDITOR_COLS] = '\0';
    
    Framebuffer::fill_rect(0, EDITOR_HELP_ROW * 16, EDITOR_COLS * 8, 16, Color32::Secondary());
    Framebuffer::draw_string(0, EDITOR_HELP_ROW * 16, helpstr, Color32::White(), Color32::Secondary());
}

static void editor_draw() {
    // Clear screen
    Framebuffer::clear(Color32::Background());
    
    // Adjust scroll if needed
    if (editor_cursor_line < editor_scroll_offset) {
        editor_scroll_offset = editor_cursor_line;
    } else if (editor_cursor_line >= editor_scroll_offset + EDITOR_ROWS) {
        editor_scroll_offset = editor_cursor_line - EDITOR_ROWS + 1;
    }
    
    // Draw all visible lines
    for (int i = 0; i < EDITOR_ROWS; i++) {
        editor_draw_line(i, editor_scroll_offset + i);
    }
    
    // Draw status bars
    editor_draw_status();
    
    // Draw cursor (inverted character)
    int cursor_screen_row = editor_cursor_line - editor_scroll_offset;
    int cursor_x = (5 + editor_cursor_col) * 8;
    int cursor_y = cursor_screen_row * 16;
    
    char c = ' ';
    if (editor_cursor_col < static_cast<int>(str::len(editor_lines[editor_cursor_line]))) {
        c = editor_lines[editor_cursor_line][editor_cursor_col];
    }
    Framebuffer::fill_rect(cursor_x, cursor_y, 8, 16, Color32::Primary());
    Framebuffer::draw_char(cursor_x, cursor_y, c, Color32::Black(), Color32::Primary());
}

static void editor_insert_char(char c) {
    if (editor_cursor_line >= EDITOR_MAX_LINES) return;
    
    char* line = editor_lines[editor_cursor_line];
    int len = str::len(line);
    
    if (len >= EDITOR_MAX_LINE_LEN - 1) return;
    
    // Shift characters right
    for (int i = len; i >= editor_cursor_col; i--) {
        line[i + 1] = line[i];
    }
    line[editor_cursor_col] = c;
    editor_cursor_col++;
    editor_modified = true;
}

static void editor_backspace() {
    if (editor_cursor_col > 0) {
        char* line = editor_lines[editor_cursor_line];
        int len = str::len(line);
        
        // Shift characters left
        for (int i = editor_cursor_col - 1; i < len; i++) {
            line[i] = line[i + 1];
        }
        editor_cursor_col--;
        editor_modified = true;
    } else if (editor_cursor_line > 0) {
        // Join with previous line
        char* prev = editor_lines[editor_cursor_line - 1];
        char* curr = editor_lines[editor_cursor_line];
        int prev_len = str::len(prev);
        
        if (prev_len + str::len(curr) < EDITOR_MAX_LINE_LEN - 1) {
            str::cat(prev, curr);
            
            // Remove current line
            for (int i = editor_cursor_line; i < editor_line_count - 1; i++) {
                str::cpy(editor_lines[i], editor_lines[i + 1]);
            }
            editor_line_count--;
            editor_cursor_line--;
            editor_cursor_col = prev_len;
            editor_modified = true;
        }
    }
}

static void editor_delete_line() {
    if (editor_line_count <= 1) {
        editor_lines[0][0] = '\0';
        editor_cursor_col = 0;
    } else {
        for (int i = editor_cursor_line; i < editor_line_count - 1; i++) {
            str::cpy(editor_lines[i], editor_lines[i + 1]);
        }
        editor_line_count--;
        if (editor_cursor_line >= editor_line_count) {
            editor_cursor_line = editor_line_count - 1;
        }
        int len = str::len(editor_lines[editor_cursor_line]);
        if (editor_cursor_col > len) editor_cursor_col = len;
    }
    editor_modified = true;
}

static void editor_new_line() {
    if (editor_line_count >= EDITOR_MAX_LINES) return;
    
    char* line = editor_lines[editor_cursor_line];
    
    // Shift lines down
    for (int i = editor_line_count; i > editor_cursor_line + 1; i--) {
        str::cpy(editor_lines[i], editor_lines[i - 1]);
    }
    editor_line_count++;
    
    // Split current line
    str::cpy(editor_lines[editor_cursor_line + 1], line + editor_cursor_col);
    line[editor_cursor_col] = '\0';
    
    editor_cursor_line++;
    editor_cursor_col = 0;
    editor_modified = true;
}

static bool editor_save() {
    u32 fd = 0;
    VFSResult result = VFS::open(editor_filename, 
                                  FileMode::Write | FileMode::Create | FileMode::Truncate, fd);
    if (result != VFSResult::Success) return false;
    
    u64 bytes_written;
    for (int i = 0; i < editor_line_count; i++) {
        VFS::write(fd, editor_lines[i], str::len(editor_lines[i]), bytes_written);
        VFS::write(fd, "\n", 1, bytes_written);
    }
    
    VFS::close(fd);
    editor_modified = false;
    return true;
}

static bool editor_load(const char* path) {
    // Clear editor
    editor_line_count = 1;
    for (int i = 0; i < EDITOR_MAX_LINES; i++) {
        editor_lines[i][0] = '\0';
    }
    editor_cursor_line = 0;
    editor_cursor_col = 0;
    editor_scroll_offset = 0;
    editor_modified = false;
    str::cpy(editor_filename, path);
    
    if (!VFS::exists(path)) return true;  // New file
    
    u32 fd = 0;
    VFSResult result = VFS::open(path, FileMode::Read, fd);
    if (result != VFSResult::Success) return false;
    
    char buffer[512];
    u64 bytes_read;
    editor_line_count = 0;
    int col = 0;
    
    while (VFS::read(fd, buffer, sizeof(buffer) - 1, bytes_read) == VFSResult::Success && bytes_read > 0) {
        for (u64 i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n' || buffer[i] == '\r') {
                editor_lines[editor_line_count][col] = '\0';
                editor_line_count++;
                col = 0;
                if (editor_line_count >= EDITOR_MAX_LINES) break;
                if (buffer[i] == '\r' && i + 1 < bytes_read && buffer[i + 1] == '\n') i++;
            } else if (col < EDITOR_MAX_LINE_LEN - 1) {
                editor_lines[editor_line_count][col++] = buffer[i];
            }
        }
        if (editor_line_count >= EDITOR_MAX_LINES) break;
    }
    
    if (col > 0 || editor_line_count == 0) {
        editor_lines[editor_line_count][col] = '\0';
        editor_line_count++;
    }
    
    VFS::close(fd);
    return true;
}

void edit(int argc, char** argv) {
    if (argc < 2) {
        Console::println("Usage: edit <filename>");
        return;
    }
    
    if (!VFS::is_ready()) {
        Console::println("Filesystem not ready");
        return;
    }
    
    char path[128];
    Shell::resolve_path(argv[1], path);
    
    if (!editor_load(path)) {
        Console::println("Cannot open file");
        return;
    }
    
    editor_draw();
    
    bool running = true;
    while (running) {
        KeyEvent ev = Keyboard::get_event();
        
        // Debug: log key events
        if (ev.ctrl) {
            Serial::log("EDIT", LogType::Debug, "Ctrl pressed, ascii=");
            char dbg[4] = {0};
            dbg[0] = '0' + ((ev.ascii / 100) % 10);
            dbg[1] = '0' + ((ev.ascii / 10) % 10);
            dbg[2] = '0' + (ev.ascii % 10);
            Serial::log("EDIT", LogType::Debug, dbg);
        }
        
        // Check for Ctrl combinations first
        if (ev.ctrl && ev.ascii != 0) {
            char key = ev.ascii;
            // Convert to lowercase for comparison
            if (key >= 'A' && key <= 'Z') key += 32;
            
            // Ctrl+S = save (ascii 's' or control code 19)
            if (key == 's' || ev.ascii == 19) {
                Serial::log("EDIT", LogType::Debug, "Saving file...");
                editor_save();
                editor_draw();
                continue;
            }
            // Ctrl+Q = quit (ascii 'q' or control code 17)
            if (key == 'q' || ev.ascii == 17) {
                Serial::log("EDIT", LogType::Debug, "Quitting editor");
                running = false;
                continue;
            }
            // Ctrl+K = delete line (ascii 'k' or control code 11)
            if (key == 'k' || ev.ascii == 11) {
                editor_delete_line();
                editor_draw();
                continue;
            }
        }
        
        // Handle special keys
        if (ev.special != SpecialKey::None) {
            switch (ev.special) {
                case SpecialKey::Up:
                    if (editor_cursor_line > 0) {
                        editor_cursor_line--;
                        int len = str::len(editor_lines[editor_cursor_line]);
                        if (editor_cursor_col > len) editor_cursor_col = len;
                    }
                    break;
                case SpecialKey::Down:
                    if (editor_cursor_line < editor_line_count - 1) {
                        editor_cursor_line++;
                        int len = str::len(editor_lines[editor_cursor_line]);
                        if (editor_cursor_col > len) editor_cursor_col = len;
                    }
                    break;
                case SpecialKey::Left:
                    if (editor_cursor_col > 0) editor_cursor_col--;
                    else if (editor_cursor_line > 0) {
                        editor_cursor_line--;
                        editor_cursor_col = str::len(editor_lines[editor_cursor_line]);
                    }
                    break;
                case SpecialKey::Right: {
                    int len = str::len(editor_lines[editor_cursor_line]);
                    if (editor_cursor_col < len) editor_cursor_col++;
                    else if (editor_cursor_line < editor_line_count - 1) {
                        editor_cursor_line++;
                        editor_cursor_col = 0;
                    }
                    break;
                }
                case SpecialKey::Home:
                    editor_cursor_col = 0;
                    break;
                case SpecialKey::End:
                    editor_cursor_col = str::len(editor_lines[editor_cursor_line]);
                    break;
                case SpecialKey::PageUp:
                    editor_cursor_line -= EDITOR_ROWS;
                    if (editor_cursor_line < 0) editor_cursor_line = 0;
                    break;
                case SpecialKey::PageDown:
                    editor_cursor_line += EDITOR_ROWS;
                    if (editor_cursor_line >= editor_line_count) 
                        editor_cursor_line = editor_line_count - 1;
                    break;
                case SpecialKey::Delete: {
                    char* line = editor_lines[editor_cursor_line];
                    int len = str::len(line);
                    if (editor_cursor_col < len) {
                        for (int i = editor_cursor_col; i < len; i++)
                            line[i] = line[i + 1];
                        editor_modified = true;
                    }
                    break;
                }
                case SpecialKey::Escape:
                    running = false;
                    continue;
                default:
                    break;
            }
            editor_draw();
            continue;
        }
        
        // Regular characters
        if (ev.ascii) {
            if (ev.ascii == '\b' || ev.ascii == 127) {
                editor_backspace();
            } else if (ev.ascii == '\n' || ev.ascii == '\r') {
                editor_new_line();
            } else if (ev.ascii == '\t') {
                for (int i = 0; i < 4; i++) editor_insert_char(' ');
            } else if (ev.ascii >= 32 && ev.ascii < 127) {
                editor_insert_char(ev.ascii);
            }
            editor_draw();
        }
    }
    
    // Restore console
    Framebuffer::clear();
    Console::clear();
    Console::println("Editor closed.");
}

} // namespace bolt::shell::cmd
