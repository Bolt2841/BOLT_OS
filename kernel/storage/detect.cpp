/* ===========================================================================
 * BOLT OS - Filesystem Detection Implementation
 * =========================================================================== */

#include "detect.hpp"
#include "ramfs.hpp"
#include "fat32fs.hpp"
#include "../drivers/serial/serial.hpp"
#include "../drivers/video/console.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
bool FilesystemDetector::initialized = false;
FilesystemDriver FilesystemRegistry::drivers[FilesystemRegistry::MAX_DRIVERS];
u32 FilesystemRegistry::driver_count = 0;
bool FilesystemRegistry::initialized = false;

// Sector buffers
static u8 detect_buffer[512] __attribute__((aligned(4)));
static u8 detect_buffer2[512] __attribute__((aligned(4)));

// ===========================================================================
// Filesystem Detector Implementation
// ===========================================================================

void FilesystemDetector::init() {
    DBG_LOADING("FSDET", "Initializing filesystem detector...");
    initialized = true;
    DBG_OK("FSDET", "Filesystem detector ready");
}

const char* FilesystemDetector::type_name(FilesystemType type) {
    switch (type) {
        case FilesystemType::Unknown: return "Unknown";
        case FilesystemType::FAT12:   return "FAT12";
        case FilesystemType::FAT16:   return "FAT16";
        case FilesystemType::FAT32:   return "FAT32";
        case FilesystemType::exFAT:   return "exFAT";
        case FilesystemType::NTFS:    return "NTFS";
        case FilesystemType::ext2:    return "ext2";
        case FilesystemType::ext3:    return "ext3";
        case FilesystemType::ext4:    return "ext4";
        case FilesystemType::ISO9660: return "ISO9660";
        case FilesystemType::UDF:     return "UDF";
        case FilesystemType::RAMFS:   return "RAMFS";
        case FilesystemType::DevFS:   return "DevFS";
        case FilesystemType::ProcFS:  return "ProcFS";
        case FilesystemType::TmpFS:   return "TmpFS";
        default:                      return "Unknown";
    }
}

const char* FilesystemDetector::type_features(FilesystemType type) {
    switch (type) {
        case FilesystemType::FAT12:   return "Legacy, 16MB max";
        case FilesystemType::FAT16:   return "Legacy, 2GB max";
        case FilesystemType::FAT32:   return "Universal, 2TB max, R/W";
        case FilesystemType::exFAT:   return "Flash storage, 128PB max";
        case FilesystemType::NTFS:    return "Windows, journaling, ACLs";
        case FilesystemType::ext2:    return "Linux legacy";
        case FilesystemType::ext3:    return "Linux, journaling";
        case FilesystemType::ext4:    return "Linux modern, extents";
        case FilesystemType::ISO9660: return "CD-ROM, read-only";
        case FilesystemType::UDF:     return "DVD/Blu-ray";
        case FilesystemType::RAMFS:   return "In-memory, volatile";
        default:                      return "";
    }
}

bool FilesystemDetector::is_supported(FilesystemType type) {
    switch (type) {
        case FilesystemType::FAT32:
        case FilesystemType::RAMFS:
        case FilesystemType::TmpFS:
            return true;
        default:
            return false;
    }
}

bool FilesystemDetector::is_read_only_supported(FilesystemType type) {
    switch (type) {
        case FilesystemType::FAT12:
        case FilesystemType::FAT16:
        case FilesystemType::ISO9660:
            return true;
        default:
            return is_supported(type);
    }
}

FilesystemType FilesystemDetector::detect(BlockDevice* device) {
    if (!device) {
        DBG_WARN("FSDET", "Null device");
        return FilesystemType::Unknown;
    }
    
    const DeviceInfo& info = device->get_info();
    DBG_DEBUG("FSDET", info.name);
    
    // Try detection at multiple offsets:
    // - Sector 0: Standard location (superfloppy or MBR)
    // - Sector 257: Boot + kernel layout (256 reserved sectors + boot)
    u32 offsets[] = { 0, 257 };
    
    for (int oi = 0; oi < 2; oi++) {
        u32 offset = offsets[oi];
        
        // Read boot sector at this offset
        IOResult result = device->read_sectors(offset, 1, detect_buffer);
        if (result != IOResult::Success) {
            continue;
        }
        
        // Check boot signature
        u16 boot_sig = detect_buffer[510] | (detect_buffer[511] << 8);
        if (boot_sig != 0xAA55) {
            continue;  // Try next offset
        }
        
        FilesystemType detected = FilesystemType::Unknown;
        
        // Try each detection method
        if (detect_exfat(detect_buffer, detected)) {
            if (offset > 0) {
                DBG("FSDET", "Found filesystem at offset");
            }
            DBG_LOADING("FSDET", type_name(detected));
            return detected;
        }
        
        if (detect_ntfs(detect_buffer, detected)) {
            if (offset > 0) {
                DBG("FSDET", "Found filesystem at offset");
            }
            DBG_LOADING("FSDET", type_name(detected));
            return detected;
        }
        
        if (detect_fat(detect_buffer, detected)) {
            if (offset > 0) {
                DBG("FSDET", "Found filesystem at offset");
            }
            DBG_LOADING("FSDET", type_name(detected));
            return detected;
        }
        
        // ext2/3/4 superblock is at byte 1024 (sector 2 for 512-byte sectors)
        result = device->read_sectors(offset + 2, 1, detect_buffer2);
        if (result == IOResult::Success) {
            if (detect_ext(detect_buffer, detect_buffer2, detected)) {
                if (offset > 0) {
                    DBG("FSDET", "Found filesystem at offset");
                }
                DBG_LOADING("FSDET", type_name(detected));
                return detected;
            }
        }
    }
    
    // ISO9660 check (for CD-ROMs) - no offset needed
    if (info.type == DeviceType::ATAPI_CDROM) {
        FilesystemType detected = FilesystemType::Unknown;
        if (detect_iso9660(device, detected)) {
            DBG_LOADING("FSDET", type_name(detected));
            return detected;
        }
    }
    
    DBG_WARN("FSDET", "No filesystem detected");
    return FilesystemType::Unknown;
}

FilesystemType FilesystemDetector::detect_from_sector(const u8* sector, usize size) {
    if (!sector || size < 512) return FilesystemType::Unknown;
    
    FilesystemType detected = FilesystemType::Unknown;
    
    if (detect_exfat(sector, detected)) return detected;
    if (detect_ntfs(sector, detected)) return detected;
    if (detect_fat(sector, detected)) return detected;
    
    return FilesystemType::Unknown;
}

bool FilesystemDetector::validate_fat_bpb(const FATBootSector* bpb) {
    // Check bytes per sector (must be power of 2, 512-4096)
    u16 bps = bpb->bytes_per_sector;
    if (bps < 512 || bps > 4096) return false;
    if (bps & (bps - 1)) return false;  // Not power of 2
    
    // Check sectors per cluster (1, 2, 4, 8, 16, 32, 64, 128)
    u8 spc = bpb->sectors_per_cluster;
    if (spc == 0 || spc > 128) return false;
    if (spc & (spc - 1)) return false;  // Not power of 2
    
    // Reserved sectors must be non-zero
    if (bpb->reserved_sectors == 0) return false;
    
    // FAT count usually 1 or 2
    if (bpb->fat_count == 0 || bpb->fat_count > 2) return false;
    
    // Media type must be valid
    if (bpb->media_type != 0xF0 && bpb->media_type < 0xF8) return false;
    
    return true;
}

bool FilesystemDetector::detect_fat(const u8* sector, FilesystemType& type) {
    const FATBootSector* bpb = reinterpret_cast<const FATBootSector*>(sector);
    
    // Validate BPB structure
    if (!validate_fat_bpb(bpb)) return false;
    
    // Determine FAT type based on cluster count
    u32 root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1)) 
                           / bpb->bytes_per_sector;
    
    u32 fat_size = bpb->sectors_per_fat_16 ? bpb->sectors_per_fat_16 
                                           : bpb->ext.fat32.sectors_per_fat_32;
    
    u32 total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 
                                               : bpb->total_sectors_32;
    
    u32 data_sectors = total_sectors - (bpb->reserved_sectors + 
                                        (bpb->fat_count * fat_size) + 
                                        root_dir_sectors);
    
    u32 cluster_count = data_sectors / bpb->sectors_per_cluster;
    
    // FAT type determination per Microsoft spec
    if (cluster_count < 4085) {
        type = FilesystemType::FAT12;
        DBG_LOADING("FSDET", "FAT12 detected (cluster count < 4085)");
    } else if (cluster_count < 65525) {
        type = FilesystemType::FAT16;
        DBG_LOADING("FSDET", "FAT16 detected (cluster count < 65525)");
    } else {
        type = FilesystemType::FAT32;
        DBG_LOADING("FSDET", "FAT32 detected");
    }
    
    // Additional validation using extended boot signature
    if (type == FilesystemType::FAT32) {
        if (bpb->ext.fat32.boot_sig == 0x29) {
            // Check fs_type string
            bool is_fat32 = true;
            const char* expected = "FAT32   ";
            for (int i = 0; i < 8; i++) {
                if (bpb->ext.fat32.fs_type[i] != expected[i]) {
                    is_fat32 = false;
                    break;
                }
            }
            if (!is_fat32) {
                DBG_WARN("FSDET", "FAT32 fs_type mismatch, but cluster count matches");
            }
        }
    }
    
    return true;
}

bool FilesystemDetector::detect_ntfs(const u8* sector, FilesystemType& type) {
    const NTFSBootSector* ntfs = reinterpret_cast<const NTFSBootSector*>(sector);
    
    // Check OEM ID "NTFS    "
    const char* ntfs_id = "NTFS    ";
    bool match = true;
    for (int i = 0; i < 8; i++) {
        if (ntfs->oem_id[i] != ntfs_id[i]) {
            match = false;
            break;
        }
    }
    
    if (!match) return false;
    
    // Validate NTFS-specific fields
    if (ntfs->reserved_sectors != 0) return false;
    if (ntfs->zeros1[0] != 0 || ntfs->zeros1[1] != 0 || ntfs->zeros1[2] != 0) return false;
    
    type = FilesystemType::NTFS;
    return true;
}

bool FilesystemDetector::detect_ext(const u8* /* boot_sector */, const u8* sb_sector, 
                                     FilesystemType& type) {
    const Ext2Superblock* sb = reinterpret_cast<const Ext2Superblock*>(sb_sector);
    
    // Check magic number
    if (sb->magic != FSMagic::EXT2_MAGIC) return false;
    
    // Determine ext2/3/4 based on features
    bool has_journal = (sb->feature_compat & 0x0004) != 0;      // has_journal
    bool has_extents = (sb->feature_incompat & 0x0040) != 0;    // extents
    bool has_64bit = (sb->feature_incompat & 0x0080) != 0;      // 64bit
    
    if (has_extents || has_64bit) {
        type = FilesystemType::ext4;
        DBG_DEBUG("FSDET", "ext4 detected (extents/64bit feature)");
    } else if (has_journal) {
        type = FilesystemType::ext3;
        DBG_DEBUG("FSDET", "ext3 detected (journal feature)");
    } else {
        type = FilesystemType::ext2;
        DBG_DEBUG("FSDET", "ext2 detected (no journal/extents)");
    }
    
    return true;
}

bool FilesystemDetector::detect_exfat(const u8* sector, FilesystemType& type) {
    const exFATBootSector* exfat = reinterpret_cast<const exFATBootSector*>(sector);
    
    // Check OEM ID "EXFAT   "
    const char* exfat_id = "EXFAT   ";
    bool match = true;
    for (int i = 0; i < 8; i++) {
        if (exfat->fs_name[i] != exfat_id[i]) {
            match = false;
            break;
        }
    }
    
    if (!match) return false;
    
    // Verify zeros field
    for (int i = 0; i < 53; i++) {
        if (exfat->zeros[i] != 0) return false;
    }
    
    type = FilesystemType::exFAT;
    return true;
}

bool FilesystemDetector::detect_iso9660(BlockDevice* device, FilesystemType& type) {
    // ISO9660 volume descriptor at sector 16 (2048-byte sectors)
    // For 512-byte sector devices, this is sector 64
    
    u32 sector = (device->get_info().sector_size == 2048) ? 16 : 64;
    
    IOResult result = device->read_sectors(sector, 1, detect_buffer);
    if (result != IOResult::Success) return false;
    
    const ISO9660VolumeDescriptor* vd = reinterpret_cast<const ISO9660VolumeDescriptor*>(detect_buffer);
    
    // Check "CD001" signature
    bool match = true;
    for (int i = 0; i < 5; i++) {
        if (vd->id[i] != FSMagic::ISO9660_ID[i]) {
            match = false;
            break;
        }
    }
    
    if (!match) return false;
    
    type = FilesystemType::ISO9660;
    return true;
}

Filesystem* FilesystemDetector::create_filesystem(FilesystemType type) {
    // First try the registry
    Filesystem* fs = FilesystemRegistry::create_for_type(type);
    if (fs) return fs;
    
    // Fallback built-in filesystems
    switch (type) {
        case FilesystemType::FAT32:
            return new FAT32Filesystem();
            
        case FilesystemType::RAMFS:
        case FilesystemType::TmpFS:
            return new RAMFilesystem();
            
        default:
            DBG_WARN("FSDET", "No driver for filesystem type");
            return nullptr;
    }
}

// ===========================================================================
// Filesystem Registry Implementation
// ===========================================================================

void FilesystemRegistry::init() {
    DBG_LOADING("FSREG", "Initializing filesystem registry...");
    
    for (u32 i = 0; i < MAX_DRIVERS; i++) {
        drivers[i].type = FilesystemType::Unknown;
        drivers[i].name = nullptr;
        drivers[i].create = nullptr;
        drivers[i].read_write = false;
    }
    driver_count = 0;
    initialized = true;
    
    DBG_OK("FSREG", "Filesystem registry ready");
}

bool FilesystemRegistry::register_driver(const FilesystemDriver& driver) {
    if (!initialized) return false;
    if (driver_count >= MAX_DRIVERS) return false;
    
    // Check for duplicate
    for (u32 i = 0; i < driver_count; i++) {
        if (drivers[i].type == driver.type) {
            DBG_WARN("FSREG", "Driver already registered");
            return false;
        }
    }
    
    drivers[driver_count++] = driver;
    
    DBG_SUCCESS("FSREG", driver.name);
    
    return true;
}

Filesystem* FilesystemRegistry::create_for_type(FilesystemType type) {
    for (u32 i = 0; i < driver_count; i++) {
        if (drivers[i].type == type && drivers[i].create) {
            return drivers[i].create();
        }
    }
    return nullptr;
}

const FilesystemDriver* FilesystemRegistry::get_driver(FilesystemType type) {
    for (u32 i = 0; i < driver_count; i++) {
        if (drivers[i].type == type) {
            return &drivers[i];
        }
    }
    return nullptr;
}

void FilesystemRegistry::print_drivers() {
    Console::set_color(Color::Yellow);
    Console::println("=== Filesystem Drivers ===");
    Console::set_color(Color::LightGray);
    
    if (driver_count == 0) {
        Console::println("  No drivers registered");
        return;
    }
    
    for (u32 i = 0; i < driver_count; i++) {
        Console::print("  ");
        Console::set_color(Color::LightCyan);
        Console::print(drivers[i].name);
        Console::set_color(Color::LightGray);
        
        // Pad
        usize len = str::len(drivers[i].name);
        for (usize j = len; j < 12; j++) Console::print(" ");
        
        Console::print(drivers[i].read_write ? " [R/W]" : " [RO] ");
        
        Console::set_color(Color::DarkGray);
        Console::print("  ");
        Console::print(FilesystemDetector::type_features(drivers[i].type));
        Console::set_color(Color::LightGray);
        
        Console::println("");
    }
}

} // namespace bolt::storage
