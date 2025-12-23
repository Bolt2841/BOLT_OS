/* ===========================================================================
 * BOLT OS - FAT32 VFS Filesystem Driver
 * 
 * Implements the Filesystem interface for FAT32 volumes.
 * Uses the block device abstraction for storage access.
 * =========================================================================== */

#ifndef BOLT_STORAGE_FAT32FS_HPP
#define BOLT_STORAGE_FAT32FS_HPP

#include "../core/types.hpp"
#include "vfs.hpp"
#include "block.hpp"

namespace bolt::storage {

// ===========================================================================
// FAT32 Structures (already defined elsewhere, but needed here)
// ===========================================================================

struct __attribute__((packed)) FAT32BootSector {
    u8  jmp[3];
    u8  oem_name[8];
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fat_count;
    u16 root_entry_count;
    u16 total_sectors_16;
    u8  media_type;
    u16 fat_size_16;
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    
    // FAT32 extended
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info_sector;
    u16 backup_boot_sector;
    u8  reserved[12];
    u8  drive_number;
    u8  reserved1;
    u8  boot_signature;
    u32 volume_id;
    u8  volume_label[11];
    u8  fs_type[8];
};

struct __attribute__((packed)) FAT32DirEntry {
    char name[8];
    char ext[3];
    u8  attributes;
    u8  reserved;
    u8  create_time_tenths;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 file_size;
    
    bool is_free() const { return (u8)name[0] == 0xE5; }
    bool is_end() const { return name[0] == 0x00; }
    bool is_lfn() const { return (attributes & 0x3F) == 0x0F; }
    bool is_directory() const { return (attributes & 0x10) != 0; }
    bool is_volume_label() const { return (attributes & 0x08) != 0; }
    bool is_hidden() const { return (attributes & 0x02) != 0; }
    bool is_readonly() const { return (attributes & 0x01) != 0; }
    
    u32 get_cluster() const {
        return ((u32)cluster_high << 16) | cluster_low;
    }
    
    void set_cluster(u32 cluster) {
        cluster_high = (cluster >> 16) & 0xFFFF;
        cluster_low = cluster & 0xFFFF;
    }
};

struct __attribute__((packed)) FAT32LFNEntry {
    u8  order;
    u16 name1[5];
    u8  attributes;
    u8  type;
    u8  checksum;
    u16 name2[6];
    u16 first_cluster;
    u16 name3[2];
};

struct __attribute__((packed)) FAT32FSInfo {
    u32 signature1;         // 0x41615252
    u8  reserved1[480];
    u32 signature2;         // 0x61417272
    u32 free_clusters;
    u32 next_free_cluster;
    u8  reserved2[12];
    u32 signature3;         // 0xAA550000
    
    bool is_valid() const {
        return signature1 == 0x41615252 &&
               signature2 == 0x61417272;
    }
};

// ===========================================================================
// FAT32 Directory Iteration State
// ===========================================================================

struct FAT32DirState {
    u32 cluster;
    u32 sector_in_cluster;
    u32 entry_in_sector;
    u32 entry_index;
    bool at_end;
};

// ===========================================================================
// FAT32 File State (for write support)
// ===========================================================================

struct FAT32FileState {
    u32 start_cluster;      // First cluster of file
    u32 parent_cluster;     // Parent directory cluster
    char name[12];          // 8.3 name for directory update
    bool needs_dir_update;  // Directory entry needs updating
};

// ===========================================================================
// FAT32 Filesystem Class
// ===========================================================================

class FAT32Filesystem : public Filesystem {
public:
    FAT32Filesystem();
    virtual ~FAT32Filesystem();
    
    // Filesystem interface
    FilesystemType type() const override { return FilesystemType::FAT32; }
    const char* name() const override { return "fat32"; }
    
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
    
    u64 total_space() const override;
    u64 free_space() const override;
    
    VFSResult sync() override;
    
    // FAT32-specific info
    const char* get_volume_label() const { return volume_label; }
    u32 get_cluster_size() const { return cluster_size; }
    
private:
    // FAT operations
    u32 get_next_cluster(u32 cluster);
    bool read_cluster(u32 cluster, void* buffer);
    bool write_cluster(u32 cluster, const void* buffer);
    u32 cluster_to_lba(u32 cluster);
    
    // Write support - FAT operations
    u32 alloc_cluster();                          // Find and allocate a free cluster
    bool set_fat_entry(u32 cluster, u32 value);   // Write FAT entry
    bool free_cluster_chain(u32 start_cluster);   // Free a chain of clusters
    u32 extend_cluster_chain(u32 last_cluster);   // Add a cluster to an existing chain
    
    // Write support - Directory operations
    bool create_dir_entry(u32 parent_cluster, const char* name, u8 attr, u32 file_cluster, u32 file_size);
    bool update_dir_entry(u32 parent_cluster, const char* name83, u32 new_size, u32 new_cluster);
    bool delete_dir_entry(u32 parent_cluster, const char* name, bool already_8_3 = false);
    u32 find_free_dir_entry(u32 parent_cluster);  // Find slot for new entry
    
    // Directory operations
    bool find_entry(const char* path, FAT32DirEntry& entry, u32& parent_cluster);
    bool read_dir_entry(u32 cluster, u32 index, FAT32DirEntry& entry, char* long_name);
    void fill_file_info(const FAT32DirEntry& entry, const char* name, FileInfo& info);
    
    // File operations
    VFSResult read_file_data(u32 start_cluster, u64 offset, void* buffer, 
                             u64 size, u64 file_size, u64& bytes_read);
    
    // Path utilities
    bool split_path(const char* path, char* dir, char* name);
    bool compare_name(const char* a, const char* b);  // Case-insensitive
    void to_8_3_name(const char* name, char* out);
    
    // Block device
    BlockDevice* blk_device;
    
    // Partition offset (for non-zero starting partitions)
    u32 partition_offset;
    
    // FAT32 parameters
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u32 cluster_size;
    u32 reserved_sectors;
    u32 fat_count;
    u32 fat_size;
    u32 fat_start_lba;
    u32 data_start_lba;
    u32 root_cluster;
    u32 total_clusters;
    u32 free_clusters;
    u32 next_free_cluster;  // Hint for finding free clusters
    u32 fs_info_sector;     // Location of FSInfo
    char volume_label[12];
    bool fat_dirty;         // FAT needs to be written back
    
    // Sector buffer
    static constexpr u32 SECTOR_SIZE = 512;
    static constexpr u32 MAX_CLUSTER_SIZE = 32 * SECTOR_SIZE;  // 32 sectors max
    u8* sector_buffer;
    u8* cluster_buffer;
    u32 cached_fat_sector;
    u32* fat_cache;  // One sector of FAT
    
    // Read helpers
    bool read_sector(u64 lba, void* buffer);
    bool write_sector(u64 lba, const void* buffer);
    bool read_fat_entry(u32 cluster, u32& value);
};

// ===========================================================================
// FAT32 Filesystem Factory (for registration)
// ===========================================================================

Filesystem* create_fat32_filesystem();

} // namespace bolt::storage

#endif // BOLT_STORAGE_FAT32FS_HPP
