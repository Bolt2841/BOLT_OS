/* ===========================================================================
 * BOLT OS - FAT32 Filesystem Implementation
 * =========================================================================== */

#include "fat32.hpp"
#include "../drivers/storage/ata.hpp"
#include "../drivers/video/console.hpp"
#include "../drivers/serial/serial.hpp"

using namespace bolt::drivers;

namespace bolt::fs {

// Static storage
bool FAT32::mounted = false;
u8 FAT32::drive_index = 0;
u32 FAT32::partition_lba = 0;
u16 FAT32::bytes_per_sector = 512;
u8 FAT32::sectors_per_cluster = 0;
u32 FAT32::fat_begin_lba = 0;
u32 FAT32::cluster_begin_lba = 0;
u32 FAT32::root_cluster = 0;
u32 FAT32::fat_size_sectors = 0;
u32 FAT32::total_clusters = 0;
char FAT32::volume_label[12] = {0};
u8 FAT32::sector_buffer[512];
u8 FAT32::cluster_buffer[4096];

bool FAT32::init(u8 drive, u8 partition) {
    Serial::write("[FAT32] Init: drive=");
    Serial::write_dec(drive);
    Serial::write(" partition=");
    Serial::write_dec(partition);
    Serial::writeln("");
    
    mounted = false;
    drive_index = drive;
    
    // Read MBR
    if (!ATA::read_sectors(drive, 0, 1, sector_buffer)) {
        DBG_FAIL("FAT32", "Failed to read MBR");
        return false;
    }
    
    // Check MBR signature
    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        DBG_FAIL("FAT32", "Invalid MBR signature");
        return false;
    }
    
    DBG("FAT32", "Valid MBR found");
    
    // Get partition table (starts at offset 446)
    PartitionEntry* partitions = reinterpret_cast<PartitionEntry*>(&sector_buffer[446]);
    
    // Find the requested partition
    if (partition >= 4) {
        DBG_FAIL("FAT32", "Invalid partition number (must be 0-3)");
        return false;
    }
    
    PartitionEntry& part = partitions[partition];
    
    // Log partition type
    Serial::write("[FAT32] Partition type: ");
    Serial::write_hex(part.type);
    Serial::writeln("");
    
    // Check partition type
    // 0x0B = FAT32 with CHS, 0x0C = FAT32 with LBA
    if (part.type != 0x0B && part.type != 0x0C && part.type != 0x07) {
        // Also check for no partition
        if (part.type == 0x00) {
            DBG_WARN("FAT32", "Partition empty, trying direct FAT32...");
            // Try reading sector 0 as FAT32 boot sector directly
            partition_lba = 0;
        } else {
            DBG_WARN("FAT32", "Unknown partition type, trying anyway...");
            partition_lba = part.lba_start;
        }
    } else {
        partition_lba = part.lba_start;
    }
    
    Serial::write("[FAT32] Partition LBA: ");
    Serial::write_dec(partition_lba);
    Serial::writeln("");
    
    // Read FAT32 boot sector
    if (!ATA::read_sectors(drive, partition_lba, 1, sector_buffer)) {
        DBG_FAIL("FAT32", "Failed to read boot sector");
        return false;
    }
    
    FAT32BootSector* bpb = reinterpret_cast<FAT32BootSector*>(sector_buffer);
    
    // Validate it's FAT32
    if (bpb->bytes_per_sector != 512) {
        DBG_FAIL("FAT32", "Unsupported sector size");
        return false;
    }
    
    if (bpb->fat_size_16 != 0 || bpb->fat_size_32 == 0) {
        DBG_FAIL("FAT32", "Not a FAT32 filesystem");
        return false;
    }
    
    // Store filesystem parameters
    bytes_per_sector = bpb->bytes_per_sector;
    sectors_per_cluster = bpb->sectors_per_cluster;
    fat_size_sectors = bpb->fat_size_32;
    root_cluster = bpb->root_cluster;
    
    // Calculate LBA addresses
    fat_begin_lba = partition_lba + bpb->reserved_sectors;
    cluster_begin_lba = fat_begin_lba + (bpb->fat_count * fat_size_sectors);
    
    // Calculate total clusters
    u32 data_sectors = bpb->total_sectors_32 - (bpb->reserved_sectors + bpb->fat_count * fat_size_sectors);
    total_clusters = data_sectors / sectors_per_cluster;
    
    // Copy volume label
    for (int i = 0; i < 11; i++) {
        volume_label[i] = bpb->volume_label[i];
    }
    volume_label[11] = '\0';
    
    // Trim trailing spaces
    for (int i = 10; i >= 0 && volume_label[i] == ' '; i--) {
        volume_label[i] = '\0';
    }
    
    mounted = true;
    
    Console::print("[FAT32] Mounted: ");
    Console::print(volume_label[0] ? volume_label : "NO NAME");
    Console::print("\n");
    Console::print("  Cluster size: ");
    Console::print_dec(sectors_per_cluster * 512);
    Console::print(" bytes\n");
    Console::print("  Total size: ");
    Console::print_dec(get_total_size_mb());
    Console::print(" MB\n");
    
    return true;
}

u32 FAT32::cluster_to_lba(u32 cluster) {
    return cluster_begin_lba + (cluster - 2) * sectors_per_cluster;
}

u32 FAT32::get_next_cluster(u32 cluster) {
    // Each FAT entry is 4 bytes
    u32 fat_offset = cluster * 4;
    u32 fat_sector = fat_begin_lba + (fat_offset / 512);
    u32 entry_offset = fat_offset % 512;
    
    // Read the FAT sector
    if (!ATA::read_sectors(drive_index, fat_sector, 1, sector_buffer)) {
        return FAT32_EOC;
    }
    
    u32 next = *reinterpret_cast<u32*>(&sector_buffer[entry_offset]);
    return next & 0x0FFFFFFF;  // FAT32 uses 28 bits
}

bool FAT32::read_cluster(u32 cluster, void* buffer) {
    u32 lba = cluster_to_lba(cluster);
    return ATA::read_sectors(drive_index, lba, sectors_per_cluster, buffer);
}

void FAT32::parse_short_name(const FAT32DirEntry* entry, char* out) {
    int i = 0;
    
    // Copy name (8 chars, space-padded)
    for (int j = 0; j < 8 && entry->name[j] != ' '; j++) {
        out[i++] = entry->name[j];
    }
    
    // Add extension if present
    if (entry->ext[0] != ' ') {
        out[i++] = '.';
        for (int j = 0; j < 3 && entry->ext[j] != ' '; j++) {
            out[i++] = entry->ext[j];
        }
    }
    
    out[i] = '\0';
}

u32 FAT32::get_cluster_from_entry(const FAT32DirEntry* entry) {
    return ((u32)entry->cluster_high << 16) | entry->cluster_low;
}

void FAT32::parse_dir_entry(const FAT32DirEntry* entry, FileInfo& out) {
    parse_short_name(entry, out.short_name);
    
    // For now, use short name as the full name
    // TODO: Handle long filenames
    for (int i = 0; i < 13; i++) {
        out.name[i] = out.short_name[i];
    }
    
    out.size = entry->file_size;
    out.cluster = get_cluster_from_entry(entry);
    out.attributes = entry->attributes;
    out.is_directory = (entry->attributes & FAT32Attr::Directory) != 0;
}

bool FAT32::list_directory(u32 cluster, void (*callback)(const FileInfo& info)) {
    if (!mounted) return false;
    
    u32 current_cluster = cluster;
    
    while (current_cluster < FAT32_EOC && current_cluster >= 2) {
        // Read cluster
        if (!read_cluster(current_cluster, cluster_buffer)) {
            return false;
        }
        
        // Process directory entries
        u32 entries_per_cluster = (sectors_per_cluster * 512) / 32;
        FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
        
        for (u32 i = 0; i < entries_per_cluster; i++) {
            FAT32DirEntry& entry = entries[i];
            
            // End of directory?
            if (entry.name[0] == 0x00) {
                return true;
            }
            
            // Deleted entry?
            if (entry.name[0] == 0xE5) {
                continue;
            }
            
            // Long filename entry?
            if (entry.attributes == FAT32Attr::LongName) {
                // TODO: Handle LFN
                continue;
            }
            
            // Skip volume label and hidden entries
            if (entry.attributes & FAT32Attr::VolumeID) {
                continue;
            }
            
            // Parse and report entry
            FileInfo info;
            parse_dir_entry(&entry, info);
            callback(info);
        }
        
        // Get next cluster
        current_cluster = get_next_cluster(current_cluster);
    }
    
    return true;
}

bool FAT32::list_root(void (*callback)(const FileInfo& info)) {
    return list_directory(root_cluster, callback);
}

bool FAT32::str_equals_nocase(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        
        // Convert to uppercase
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        
        if (ca != cb) return false;
    }
    return *a == *b;
}

bool FAT32::find_file(const char* path, FileInfo& out) {
    if (!mounted) return false;
    
    // Skip leading slash
    if (*path == '/' || *path == '\\') path++;
    
    // Start at root
    u32 current_cluster = root_cluster;
    bool is_directory = true;
    
    while (*path) {
        // Extract next path component
        char component[256];
        int i = 0;
        while (*path && *path != '/' && *path != '\\' && i < 255) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        
        // Skip trailing slash
        if (*path == '/' || *path == '\\') path++;
        
        if (i == 0) continue;  // Empty component
        
        // Search for component in current directory
        bool found = false;
        u32 search_cluster = current_cluster;
        
        while (search_cluster < FAT32_EOC && search_cluster >= 2) {
            if (!read_cluster(search_cluster, cluster_buffer)) {
                return false;
            }
            
            u32 entries_per_cluster = (sectors_per_cluster * 512) / 32;
            FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
            
            for (u32 j = 0; j < entries_per_cluster; j++) {
                FAT32DirEntry& entry = entries[j];
                
                if (entry.name[0] == 0x00) break;  // End
                if (entry.name[0] == 0xE5) continue;  // Deleted
                if (entry.attributes == FAT32Attr::LongName) continue;
                if (entry.attributes & FAT32Attr::VolumeID) continue;
                
                // Compare name
                char entry_name[13];
                parse_short_name(&entry, entry_name);
                
                if (str_equals_nocase(entry_name, component)) {
                    // Found it!
                    parse_dir_entry(&entry, out);
                    current_cluster = out.cluster;
                    is_directory = out.is_directory;
                    found = true;
                    break;
                }
            }
            
            if (found) break;
            search_cluster = get_next_cluster(search_cluster);
        }
        
        if (!found) {
            return false;  // Path component not found
        }
        
        // If more path remains, this must be a directory
        if (*path && !is_directory) {
            return false;
        }
    }
    
    return true;
}

bool FAT32::read_file(const FileInfo& file, void* buffer, u32 max_size, u32& bytes_read) {
    if (!mounted) return false;
    if (file.is_directory) return false;
    
    bytes_read = 0;
    u32 remaining = file.size < max_size ? file.size : max_size;
    u32 current_cluster = file.cluster;
    u8* buf = static_cast<u8*>(buffer);
    u32 cluster_size = sectors_per_cluster * 512;
    
    while (remaining > 0 && current_cluster < FAT32_EOC && current_cluster >= 2) {
        if (!read_cluster(current_cluster, cluster_buffer)) {
            return false;
        }
        
        u32 to_copy = remaining < cluster_size ? remaining : cluster_size;
        
        for (u32 i = 0; i < to_copy; i++) {
            buf[bytes_read + i] = cluster_buffer[i];
        }
        
        bytes_read += to_copy;
        remaining -= to_copy;
        
        current_cluster = get_next_cluster(current_cluster);
    }
    
    return true;
}

bool FAT32::read_file_partial(const FileInfo& file, void* buffer, u32 offset, u32 count, u32& bytes_read) {
    if (!mounted) return false;
    if (file.is_directory) return false;
    if (offset >= file.size) {
        bytes_read = 0;
        return true;
    }
    
    bytes_read = 0;
    u32 cluster_size = sectors_per_cluster * 512;
    u32 current_cluster = file.cluster;
    u8* buf = static_cast<u8*>(buffer);
    
    // Skip clusters before offset
    u32 clusters_to_skip = offset / cluster_size;
    u32 offset_in_cluster = offset % cluster_size;
    
    for (u32 i = 0; i < clusters_to_skip && current_cluster < FAT32_EOC; i++) {
        current_cluster = get_next_cluster(current_cluster);
    }
    
    // Calculate how much we can actually read
    u32 available = file.size - offset;
    u32 remaining = count < available ? count : available;
    
    // Read data
    while (remaining > 0 && current_cluster < FAT32_EOC && current_cluster >= 2) {
        if (!read_cluster(current_cluster, cluster_buffer)) {
            return false;
        }
        
        u32 start = offset_in_cluster;
        u32 to_copy = cluster_size - start;
        if (to_copy > remaining) to_copy = remaining;
        
        for (u32 i = 0; i < to_copy; i++) {
            buf[bytes_read + i] = cluster_buffer[start + i];
        }
        
        bytes_read += to_copy;
        remaining -= to_copy;
        offset_in_cluster = 0;  // Only first cluster has offset
        
        current_cluster = get_next_cluster(current_cluster);
    }
    
    return true;
}

const char* FAT32::get_volume_label() {
    return volume_label;
}

u32 FAT32::get_total_size_mb() {
    return (total_clusters * sectors_per_cluster * 512) / (1024 * 1024);
}

u32 FAT32::get_free_size_mb() {
    // This would require scanning the entire FAT
    // Return 0 for now (TODO: implement)
    return 0;
}

} // namespace bolt::fs
