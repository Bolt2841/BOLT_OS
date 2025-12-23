#pragma once
/* ===========================================================================
 * BOLT OS - FAT32 Filesystem Driver (LEGACY)
 * ===========================================================================
 * 
 * DEPRECATED: This is the old static FAT32 implementation.
 * Use storage/fat32fs.hpp for the new VFS-based implementation instead.
 * 
 * This file is kept for reference and potential direct-access use cases.
 * =========================================================================== */

#include "../lib/types.hpp"

namespace bolt::fs {

// MBR Partition Entry
struct __attribute__((packed)) PartitionEntry {
    u8 bootable;
    u8 start_head;
    u16 start_sector_cylinder;
    u8 type;
    u8 end_head;
    u16 end_sector_cylinder;
    u32 lba_start;
    u32 sector_count;
};

// FAT32 Boot Sector (BPB)
struct __attribute__((packed)) FAT32BootSector {
    u8 jump[3];
    char oem_name[8];
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sectors;
    u8 fat_count;
    u16 root_entry_count;      // 0 for FAT32
    u16 total_sectors_16;      // 0 for FAT32
    u8 media_type;
    u16 fat_size_16;           // 0 for FAT32
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    
    // FAT32 specific
    u32 fat_size_32;
    u16 ext_flags;
    u16 fs_version;
    u32 root_cluster;
    u16 fs_info_sector;
    u16 backup_boot_sector;
    u8 reserved[12];
    u8 drive_number;
    u8 reserved1;
    u8 boot_signature;
    u32 volume_id;
    char volume_label[11];
    char fs_type[8];
};

// FAT32 Directory Entry (32 bytes)
struct __attribute__((packed)) FAT32DirEntry {
    char name[8];
    char ext[3];
    u8 attributes;
    u8 reserved;
    u8 create_time_tenths;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 cluster_high;
    u16 modify_time;
    u16 modify_date;
    u16 cluster_low;
    u32 file_size;
};

// Long filename entry
struct __attribute__((packed)) LFNEntry {
    u8 order;
    u16 name1[5];
    u8 attributes;
    u8 type;
    u8 checksum;
    u16 name2[6];
    u16 cluster;   // Always 0
    u16 name3[2];
};

// File attributes
namespace FAT32Attr {
    constexpr u8 ReadOnly = 0x01;
    constexpr u8 Hidden = 0x02;
    constexpr u8 System = 0x04;
    constexpr u8 VolumeID = 0x08;
    constexpr u8 Directory = 0x10;
    constexpr u8 Archive = 0x20;
    constexpr u8 LongName = 0x0F;
}

// File/Directory info structure
struct FileInfo {
    char name[256];         // Long filename
    char short_name[13];    // 8.3 name
    u32 size;
    u32 cluster;
    u8 attributes;
    bool is_directory;
};

// FAT32 end of chain marker
constexpr u32 FAT32_EOC = 0x0FFFFFF8;
constexpr u32 FAT32_BAD = 0x0FFFFFF7;

class FAT32 {
public:
    // Initialize with a drive and partition
    static bool init(u8 drive, u8 partition = 0);
    
    // List files in a directory
    static bool list_directory(u32 cluster, void (*callback)(const FileInfo& info));
    
    // List root directory
    static bool list_root(void (*callback)(const FileInfo& info));
    
    // Find file/directory by path
    static bool find_file(const char* path, FileInfo& out);
    
    // Read file contents
    static bool read_file(const FileInfo& file, void* buffer, u32 max_size, u32& bytes_read);
    
    // Read first N bytes of a file
    static bool read_file_partial(const FileInfo& file, void* buffer, u32 offset, u32 count, u32& bytes_read);
    
    // Get filesystem info
    static const char* get_volume_label();
    static u32 get_total_size_mb();
    static u32 get_free_size_mb();
    static bool is_mounted() { return mounted; }
    
private:
    static bool mounted;
    static u8 drive_index;
    static u32 partition_lba;
    
    // FAT32 parameters (from BPB)
    static u16 bytes_per_sector;
    static u8 sectors_per_cluster;
    static u32 fat_begin_lba;
    static u32 cluster_begin_lba;
    static u32 root_cluster;
    static u32 fat_size_sectors;
    static u32 total_clusters;
    static char volume_label[12];
    
    // Buffer for sector reads
    static u8 sector_buffer[512];
    static u8 cluster_buffer[4096];  // Max 8 sectors per cluster
    
    // Helper functions
    static u32 cluster_to_lba(u32 cluster);
    static u32 get_next_cluster(u32 cluster);
    static bool read_cluster(u32 cluster, void* buffer);
    static void parse_dir_entry(const FAT32DirEntry* entry, FileInfo& out);
    static void parse_short_name(const FAT32DirEntry* entry, char* out);
    static bool str_equals_nocase(const char* a, const char* b);
    static u32 get_cluster_from_entry(const FAT32DirEntry* entry);
};

} // namespace bolt::fs
