/* ===========================================================================
 * BOLT OS - Partition Table Manager
 * 
 * Handles MBR and GPT partition table parsing and partition device creation.
 * =========================================================================== */

#ifndef BOLT_STORAGE_PARTITION_HPP
#define BOLT_STORAGE_PARTITION_HPP

#include "../core/types.hpp"
#include "block.hpp"

namespace bolt::storage {

// ===========================================================================
// Partition Types
// ===========================================================================

enum class PartitionScheme : u8 {
    Unknown = 0,
    MBR,      // Master Boot Record (legacy)
    GPT       // GUID Partition Table
};

// Common MBR partition type codes
namespace MBRType {
    constexpr u8 Empty          = 0x00;
    constexpr u8 FAT12          = 0x01;
    constexpr u8 FAT16_Small    = 0x04;
    constexpr u8 Extended_CHS   = 0x05;
    constexpr u8 FAT16          = 0x06;
    constexpr u8 NTFS           = 0x07;   // Also exFAT, HPFS
    constexpr u8 FAT32_CHS      = 0x0B;
    constexpr u8 FAT32_LBA      = 0x0C;
    constexpr u8 FAT16_LBA      = 0x0E;
    constexpr u8 Extended_LBA   = 0x0F;
    constexpr u8 Hidden_FAT32   = 0x1B;
    constexpr u8 Linux_Swap     = 0x82;
    constexpr u8 Linux_Native   = 0x83;
    constexpr u8 Linux_Extended = 0x85;
    constexpr u8 Linux_LVM      = 0x8E;
    constexpr u8 FreeBSD        = 0xA5;
    constexpr u8 OpenBSD        = 0xA6;
    constexpr u8 MacOS_HFS      = 0xAF;
    constexpr u8 GPT_Protective = 0xEE;
    constexpr u8 EFI_System     = 0xEF;
}

// Common GPT partition type GUIDs (16 bytes each)
namespace GPTType {
    // Microsoft
    constexpr u8 MSR[16]   = {0xE3,0xC9,0xE3,0x16,0xB5,0x3C,0x4E,0x4D,0xAA,0xBB,0x00,0x00,0x00,0x00,0x00,0x00};
    constexpr u8 NTFS[16]  = {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
    // Linux
    constexpr u8 Linux[16] = {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};
    // EFI System
    constexpr u8 EFI[16]   = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
}

// ===========================================================================
// MBR Structures (packed for disk layout)
// ===========================================================================

struct __attribute__((packed)) MBRPartitionEntry {
    u8  boot_flag;        // 0x80 = bootable, 0x00 = not bootable
    u8  start_chs[3];     // Starting CHS address
    u8  type;             // Partition type code
    u8  end_chs[3];       // Ending CHS address
    u32 start_lba;        // Starting LBA
    u32 sector_count;     // Number of sectors
    
    bool is_bootable() const { return boot_flag == 0x80; }
    bool is_empty() const { return type == 0x00; }
    bool is_extended() const { 
        return type == MBRType::Extended_CHS || type == MBRType::Extended_LBA; 
    }
    bool is_gpt_protective() const { return type == MBRType::GPT_Protective; }
};

struct __attribute__((packed)) MBR {
    u8  bootstrap[446];   // Boot code
    MBRPartitionEntry partitions[4];
    u16 signature;        // Should be 0xAA55
    
    bool is_valid() const { return signature == 0xAA55; }
};

static_assert(sizeof(MBR) == 512, "MBR must be 512 bytes");

// ===========================================================================
// GPT Structures (packed for disk layout)
// ===========================================================================

struct __attribute__((packed)) GPTHeader {
    u64 signature;        // "EFI PART" = 0x5452415020494645
    u32 revision;         // Usually 0x00010000
    u32 header_size;      // Usually 92 bytes
    u32 header_crc32;     // CRC32 of header
    u32 reserved;
    u64 current_lba;      // LBA of this header
    u64 backup_lba;       // LBA of backup header
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8  disk_guid[16];
    u64 partition_table_lba;
    u32 partition_entry_count;
    u32 partition_entry_size;  // Usually 128 bytes
    u32 partition_table_crc32;
    
    bool is_valid() const { return signature == 0x5452415020494645ULL; }
};

struct __attribute__((packed)) GPTPartitionEntry {
    u8  type_guid[16];
    u8  unique_guid[16];
    u64 start_lba;
    u64 end_lba;
    u64 attributes;
    u16 name[36];         // UTF-16LE partition name
    
    bool is_empty() const {
        for (int i = 0; i < 16; i++) {
            if (type_guid[i] != 0) return false;
        }
        return true;
    }
    
    u64 sector_count() const { return end_lba - start_lba + 1; }
};

static_assert(sizeof(GPTPartitionEntry) == 128, "GPT entry must be 128 bytes");

// ===========================================================================
// Partition Info Structure
// ===========================================================================

struct PartitionInfo {
    u64         start_lba;
    u64         sector_count;
    u64         size_bytes;
    u8          type_mbr;        // MBR type code (0 if GPT)
    u8          type_guid[16];   // GPT type GUID
    u8          partition_guid[16];
    char        name[72];        // Partition name (from GPT)
    char        label[16];       // Generated label (sda1, hda2, etc.)
    u8          index;           // Partition number (0-based)
    bool        bootable;
    bool        is_gpt;
    
    void clear() {
        start_lba = 0;
        sector_count = 0;
        size_bytes = 0;
        type_mbr = 0;
        for (int i = 0; i < 16; i++) {
            type_guid[i] = 0;
            partition_guid[i] = 0;
        }
        for (int i = 0; i < 72; i++) name[i] = 0;
        for (int i = 0; i < 16; i++) label[i] = 0;
        index = 0;
        bootable = false;
        is_gpt = false;
    }
};

// ===========================================================================
// Partition Manager
// ===========================================================================

class PartitionManager {
public:
    static constexpr u32 MAX_PARTITIONS = 32;  // Per disk
    
    // Initialize the partition manager
    static void init();
    
    // Scan a device for partitions
    // Returns number of partitions found, creates PartitionDevice objects
    static u32 scan_device(BlockDevice* device);
    
    // Get partition scheme of a device
    static PartitionScheme detect_scheme(BlockDevice* device);
    
    // Get type name from MBR type code
    static const char* mbr_type_name(u8 type);
    
    // Print partition table for a device
    static void print_partitions(BlockDevice* device);
    
private:
    // Parse MBR partition table
    static u32 parse_mbr(BlockDevice* device, const MBR* mbr);
    
    // Parse extended partition (logical drives)
    static u32 parse_extended(BlockDevice* device, u32 ext_start, u32 ext_size, u8& part_index);
    
    // Parse GPT partition table
    static u32 parse_gpt(BlockDevice* device);
    
    // Create partition device and register it
    static bool create_partition(BlockDevice* parent, const PartitionInfo& info);
    
    // Read sector helper
    static bool read_sector(BlockDevice* device, u64 lba, void* buffer);
    
    // Per-device partition storage
    static PartitionInfo partitions[MAX_PARTITIONS];
    static u32 partition_count;
    static bool initialized;
};

// ===========================================================================
// Utility Functions
// ===========================================================================

// Check if two GUIDs are equal
bool guid_equal(const u8* a, const u8* b);

// Format GUID as string
void guid_to_string(const u8* guid, char* str);

} // namespace bolt::storage

#endif // BOLT_STORAGE_PARTITION_HPP
