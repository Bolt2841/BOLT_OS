/* ===========================================================================
 * BOLT OS - Filesystem Detection
 * 
 * Detects filesystem types by reading signature bytes from block devices.
 * =========================================================================== */

#ifndef BOLT_STORAGE_DETECT_HPP
#define BOLT_STORAGE_DETECT_HPP

#include "../core/types.hpp"
#include "block.hpp"
#include "vfs.hpp"

namespace bolt::storage {

// ===========================================================================
// Filesystem Signature Structures
// ===========================================================================

// FAT Boot Sector (common for FAT12/16/32)
struct __attribute__((packed)) FATBootSector {
    u8  jmp[3];               // Jump instruction
    u8  oem_name[8];          // OEM identifier
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;
    u8  fat_count;
    u16 root_entry_count;     // 0 for FAT32
    u16 total_sectors_16;     // 0 for FAT32
    u8  media_type;
    u16 sectors_per_fat_16;   // 0 for FAT32
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 total_sectors_32;
    
    // Extended boot record (FAT32 specific at offset 36)
    union {
        struct {
            // FAT12/16 extended
            u8  drive_number;
            u8  reserved;
            u8  boot_sig;        // 0x29 for extended
            u32 volume_id;
            u8  volume_label[11];
            u8  fs_type[8];      // "FAT12   " or "FAT16   "
        } fat16;
        
        struct {
            // FAT32 extended
            u32 sectors_per_fat_32;
            u16 flags;
            u16 version;
            u32 root_cluster;
            u16 fsinfo_sector;
            u16 backup_boot_sector;
            u8  reserved[12];
            u8  drive_number;
            u8  reserved2;
            u8  boot_sig;        // 0x29 for extended
            u32 volume_id;
            u8  volume_label[11];
            u8  fs_type[8];      // "FAT32   "
        } fat32;
    } ext;
};

// NTFS Boot Sector
struct __attribute__((packed)) NTFSBootSector {
    u8  jmp[3];
    u8  oem_id[8];            // "NTFS    "
    u16 bytes_per_sector;
    u8  sectors_per_cluster;
    u16 reserved_sectors;     // Always 0
    u8  zeros1[3];            // Always 0
    u16 not_used1;            // Always 0
    u8  media_type;
    u16 zeros2;               // Always 0
    u16 sectors_per_track;
    u16 head_count;
    u32 hidden_sectors;
    u32 not_used2;            // Always 0
    u32 not_used3;            // Always 0x80008000
    u64 total_sectors;
    u64 mft_cluster;          // Cluster number of MFT
    u64 mft_mirror_cluster;   // Cluster number of MFT mirror
    i8  clusters_per_mft_record;
    u8  reserved1[3];
    i8  clusters_per_index_record;
    u8  reserved2[3];
    u64 volume_serial;
    u32 checksum;
};

// ext2/3/4 Superblock (at offset 1024 bytes)
struct __attribute__((packed)) Ext2Superblock {
    u32 inodes_count;
    u32 blocks_count;
    u32 reserved_blocks;
    u32 free_blocks;
    u32 free_inodes;
    u32 first_data_block;
    u32 log_block_size;       // Block size = 1024 << log_block_size
    u32 log_frag_size;
    u32 blocks_per_group;
    u32 frags_per_group;
    u32 inodes_per_group;
    u32 mount_time;
    u32 write_time;
    u16 mount_count;
    u16 max_mount_count;
    u16 magic;                // 0xEF53
    u16 state;
    u16 errors;
    u16 minor_rev;
    u32 last_check;
    u32 check_interval;
    u32 creator_os;
    u32 rev_level;
    u16 default_uid;
    u16 default_gid;
    
    // Extended superblock (rev >= 1)
    u32 first_inode;
    u16 inode_size;
    u16 block_group_nr;
    u32 feature_compat;
    u32 feature_incompat;
    u32 feature_ro_compat;
    u8  uuid[16];
    u8  volume_name[16];
};

// exFAT Boot Sector
struct __attribute__((packed)) exFATBootSector {
    u8  jmp[3];
    u8  fs_name[8];           // "EXFAT   "
    u8  zeros[53];            // Must be zero
    u64 partition_offset;
    u64 volume_length;
    u32 fat_offset;
    u32 fat_length;
    u32 cluster_heap_offset;
    u32 cluster_count;
    u32 root_directory_cluster;
    u32 volume_serial;
    u16 fs_revision;
    u16 volume_flags;
    u8  bytes_per_sector_shift;
    u8  sectors_per_cluster_shift;
    u8  number_of_fats;
    u8  drive_select;
    u8  percent_in_use;
    u8  reserved[7];
    u8  boot_code[390];
    u16 boot_signature;       // 0xAA55
};

// ISO 9660 Volume Descriptor (at sector 16)
struct __attribute__((packed)) ISO9660VolumeDescriptor {
    u8  type;                 // 1 = Primary, 255 = terminator
    u8  id[5];                // "CD001"
    u8  version;
    u8  unused1;
    u8  system_id[32];
    u8  volume_id[32];
    // ... more fields
};

// ===========================================================================
// Filesystem Detection Constants
// ===========================================================================

namespace FSMagic {
    // FAT signatures
    constexpr u8 FAT_BOOT_SIG = 0x29;
    constexpr u16 FAT_END_SIG = 0xAA55;
    
    // NTFS signature
    constexpr u64 NTFS_OEM = 0x202020205346544EULL;  // "NTFS    "
    
    // ext2/3/4 magic
    constexpr u16 EXT2_MAGIC = 0xEF53;
    
    // exFAT signature
    constexpr u64 EXFAT_OEM = 0x2020205441465845ULL; // "EXFAT   "
    
    // ISO 9660
    constexpr u8 ISO9660_ID[5] = {'C', 'D', '0', '0', '1'};
}

// ===========================================================================
// Filesystem Detector
// ===========================================================================

class FilesystemDetector {
public:
    // Initialize detector
    static void init();
    
    // Detect filesystem type on a block device
    static FilesystemType detect(BlockDevice* device);
    
    // Detect filesystem on raw boot sector data
    static FilesystemType detect_from_sector(const u8* sector, usize size);
    
    // Get human-readable name for filesystem type
    static const char* type_name(FilesystemType type);
    
    // Create filesystem instance for given type
    static Filesystem* create_filesystem(FilesystemType type);
    
    // Get filesystem features/capabilities string
    static const char* type_features(FilesystemType type);
    
    // Check if filesystem type is supported for mounting
    static bool is_supported(FilesystemType type);
    
    // Check if filesystem type is read-only (detection only)
    static bool is_read_only_supported(FilesystemType type);
    
private:
    // Individual detection functions
    static bool detect_fat(const u8* sector, FilesystemType& type);
    static bool detect_ntfs(const u8* sector, FilesystemType& type);
    static bool detect_ext(const u8* sector, const u8* sb_sector, FilesystemType& type);
    static bool detect_exfat(const u8* sector, FilesystemType& type);
    static bool detect_iso9660(BlockDevice* device, FilesystemType& type);
    
    // Validate FAT boot sector sanity
    static bool validate_fat_bpb(const FATBootSector* bpb);
    
    static bool initialized;
};

// ===========================================================================
// Filesystem Registration
// ===========================================================================

// Filesystem driver registration entry
struct FilesystemDriver {
    FilesystemType type;
    const char* name;
    Filesystem* (*create)();
    bool read_write;
};

class FilesystemRegistry {
public:
    static constexpr u32 MAX_DRIVERS = 16;
    
    static void init();
    static bool register_driver(const FilesystemDriver& driver);
    static Filesystem* create_for_type(FilesystemType type);
    static const FilesystemDriver* get_driver(FilesystemType type);
    static void print_drivers();
    
private:
    static FilesystemDriver drivers[MAX_DRIVERS];
    static u32 driver_count;
    static bool initialized;
};

} // namespace bolt::storage

#endif // BOLT_STORAGE_DETECT_HPP
