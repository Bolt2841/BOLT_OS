/* ===========================================================================
 * BOLT OS - FAT32 VFS Filesystem Implementation
 * =========================================================================== */

#include "fat32fs.hpp"
#include "../drivers/serial/serial.hpp"
#include "../lib/string.hpp"
#include "../core/memory/heap.hpp"

namespace bolt::storage {

using namespace drivers;
using namespace mem;

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

FAT32Filesystem::FAT32Filesystem()
    : blk_device(nullptr),
      partition_offset(0),
      bytes_per_sector(512),
      sectors_per_cluster(0),
      cluster_size(0),
      reserved_sectors(0),
      fat_count(0),
      fat_size(0),
      fat_start_lba(0),
      data_start_lba(0),
      root_cluster(0),
      total_clusters(0),
      free_clusters(0),
      sector_buffer(nullptr),
      cluster_buffer(nullptr),
      cached_fat_sector(0xFFFFFFFF),
      fat_cache(nullptr)
{
    volume_label[0] = '\0';
}

FAT32Filesystem::~FAT32Filesystem() {
    if (mounted) {
        unmount();
    }
}

// ===========================================================================
// Mount / Unmount
// ===========================================================================

VFSResult FAT32Filesystem::mount(BlockDevice* dev, const char* mnt_point) {
    if (mounted) {
        return VFSResult::AlreadyMounted;
    }
    
    if (!dev) {
        DBG_WARN("FAT32", "No device provided");
        return VFSResult::InvalidArgument;
    }
    
    DBG_LOADING("FAT32", "Mounting FAT32 filesystem...");
    
    blk_device = dev;
    device = dev;
    partition_offset = 0;  // Start with no offset
    
    // Allocate buffers
    sector_buffer = static_cast<u8*>(Heap::alloc(SECTOR_SIZE));
    cluster_buffer = static_cast<u8*>(Heap::alloc(MAX_CLUSTER_SIZE));
    fat_cache = static_cast<u32*>(Heap::alloc(SECTOR_SIZE));
    
    if (!sector_buffer || !cluster_buffer || !fat_cache) {
        DBG_FAIL("FAT32", "Failed to allocate buffers");
        return VFSResult::NoSpace;
    }
    
    // Read sector 0 to check if it's a FAT32 boot sector or MBR/boot+kernel
    if (blk_device->read_sectors(0, 1, sector_buffer) != IOResult::Success) {
        DBG_FAIL("FAT32", "Failed to read sector 0");
        return VFSResult::IOError;
    }
    
    const FAT32BootSector* bpb = reinterpret_cast<const FAT32BootSector*>(sector_buffer);
    
    // Check if sector 0 is a valid FAT32 boot sector
    bool valid_at_zero = (bpb->bytes_per_sector == 512 || bpb->bytes_per_sector == 1024 ||
                          bpb->bytes_per_sector == 2048 || bpb->bytes_per_sector == 4096) &&
                         bpb->sectors_per_cluster > 0 && bpb->fat_count > 0 &&
                         bpb->fat_size_32 > 0;
    
    if (!valid_at_zero) {
        // Not at sector 0 - try to find FAT32 using hidden_sectors hint
        // Check if there's a partition at sector 257 (our boot + kernel layout)
        DBG("FAT32", "Sector 0 not FAT32, scanning for partition...");
        
        // Try known offset: 257 (256 reserved sectors + 1 boot)
        u32 try_offsets[] = { 257, 63, 2048, 0 };  // Common partition starts
        
        for (int i = 0; try_offsets[i] != 0 || i == 0; i++) {
            if (i > 0 && try_offsets[i] == 0) break;
            
            u32 offset = try_offsets[i];
            if (blk_device->read_sectors(offset, 1, sector_buffer) != IOResult::Success) {
                continue;
            }
            
            bpb = reinterpret_cast<const FAT32BootSector*>(sector_buffer);
            
            if ((bpb->bytes_per_sector == 512 || bpb->bytes_per_sector == 1024 ||
                 bpb->bytes_per_sector == 2048 || bpb->bytes_per_sector == 4096) &&
                bpb->sectors_per_cluster > 0 && bpb->fat_count > 0) {
                
                DBG("FAT32", "Found FAT32 partition at offset");
                partition_offset = offset;
                break;
            }
        }
        
        if (partition_offset == 0 && !valid_at_zero) {
            DBG_FAIL("FAT32", "No FAT32 filesystem found");
            return VFSResult::NoFilesystem;
        }
    }
    
    // Re-read the boot sector at the detected offset (if offset changed)
    if (partition_offset > 0) {
        if (!read_sector(0, sector_buffer)) {
            DBG_FAIL("FAT32", "Failed to read partition boot sector");
            return VFSResult::IOError;
        }
        bpb = reinterpret_cast<const FAT32BootSector*>(sector_buffer);
    }
    
    // Validate again
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 1024 &&
        bpb->bytes_per_sector != 2048 && bpb->bytes_per_sector != 4096) {
        DBG_FAIL("FAT32", "Invalid bytes per sector");
        return VFSResult::NoFilesystem;
    }
    
    if (bpb->sectors_per_cluster == 0 || bpb->fat_count == 0) {
        DBG_FAIL("FAT32", "Invalid BPB parameters");
        return VFSResult::NoFilesystem;
    }
    
    // Extract parameters
    bytes_per_sector = bpb->bytes_per_sector;
    sectors_per_cluster = bpb->sectors_per_cluster;
    cluster_size = bytes_per_sector * sectors_per_cluster;
    reserved_sectors = bpb->reserved_sectors;
    fat_count = bpb->fat_count;
    fat_size = bpb->fat_size_32 ? bpb->fat_size_32 : bpb->fat_size_16;
    root_cluster = bpb->root_cluster;
    
    fat_start_lba = reserved_sectors;
    data_start_lba = reserved_sectors + (fat_count * fat_size);
    
    // Calculate total clusters
    u32 total_sectors = bpb->total_sectors_32 ? bpb->total_sectors_32 : bpb->total_sectors_16;
    u32 data_sectors = total_sectors - data_start_lba;
    total_clusters = data_sectors / sectors_per_cluster;
    
    // Extract volume label
    for (int i = 0; i < 11; i++) {
        volume_label[i] = bpb->volume_label[i];
    }
    volume_label[11] = '\0';
    // Trim trailing spaces
    for (int i = 10; i >= 0 && volume_label[i] == ' '; i--) {
        volume_label[i] = '\0';
    }
    
    // Read FSInfo for free cluster count
    if (bpb->fs_info_sector > 0 && bpb->fs_info_sector < reserved_sectors) {
        if (read_sector(bpb->fs_info_sector, sector_buffer)) {
            const FAT32FSInfo* fsinfo = reinterpret_cast<const FAT32FSInfo*>(sector_buffer);
            if (fsinfo->is_valid() && fsinfo->free_clusters != 0xFFFFFFFF) {
                free_clusters = fsinfo->free_clusters;
            }
        }
    }
    
    mount_path = mnt_point;
    mounted = true;
    
    DBG_SUCCESS("FAT32", volume_label);
    
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::unmount() {
    if (!mounted) {
        return VFSResult::NotMounted;
    }
    
    // Sync pending writes
    sync();
    
    // Free buffers
    if (sector_buffer) Heap::free(sector_buffer);
    if (cluster_buffer) Heap::free(cluster_buffer);
    if (fat_cache) Heap::free(fat_cache);
    
    sector_buffer = nullptr;
    cluster_buffer = nullptr;
    fat_cache = nullptr;
    
    blk_device = nullptr;
    device = nullptr;
    mounted = false;
    mount_path = nullptr;
    
    DBG_OK("FAT32", "Unmounted");
    return VFSResult::Success;
}

// ===========================================================================
// Low-level I/O
// ===========================================================================

bool FAT32Filesystem::read_sector(u64 lba, void* buffer) {
    if (!blk_device) return false;
    IOResult result = blk_device->read_sectors(partition_offset + lba, 1, buffer);
    return result == IOResult::Success;
}

bool FAT32Filesystem::write_sector(u64 lba, const void* buffer) {
    if (!blk_device) return false;
    IOResult result = blk_device->write_sectors(partition_offset + lba, 1, buffer);
    return result == IOResult::Success;
}

u32 FAT32Filesystem::cluster_to_lba(u32 cluster) {
    return data_start_lba + ((cluster - 2) * sectors_per_cluster);
}

bool FAT32Filesystem::read_cluster(u32 cluster, void* buffer) {
    if (cluster < 2) return false;
    u64 lba = partition_offset + cluster_to_lba(cluster);
    
    // Read all sectors in cluster
    IOResult result = blk_device->read_sectors(lba, sectors_per_cluster, buffer);
    return result == IOResult::Success;
}

bool FAT32Filesystem::write_cluster(u32 cluster, const void* buffer) {
    if (cluster < 2) {
        DBG_FAIL("FAT32", "write_cluster: invalid cluster");
        return false;
    }
    u64 lba = partition_offset + cluster_to_lba(cluster);
    
    IOResult result = blk_device->write_sectors(lba, sectors_per_cluster, buffer);
    if (result != IOResult::Success) {
        DBG_FAIL("FAT32", "write_cluster: write_sectors failed");
        return false;
    }
    return true;
}

bool FAT32Filesystem::read_fat_entry(u32 cluster, u32& value) {
    u32 fat_sector = fat_start_lba + (cluster * 4) / bytes_per_sector;
    u32 offset = (cluster * 4) % bytes_per_sector;
    
    // Check cache
    if (fat_sector != cached_fat_sector) {
        if (!read_sector(fat_sector, fat_cache)) {
            return false;
        }
        cached_fat_sector = fat_sector;
    }
    
    value = fat_cache[offset / 4] & 0x0FFFFFFF;
    return true;
}

// ===========================================================================
// FAT Write Operations
// ===========================================================================

bool FAT32Filesystem::set_fat_entry(u32 cluster, u32 value) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector_offset = fat_offset / bytes_per_sector;
    u32 offset_in_sector = (fat_offset % bytes_per_sector) / 4;
    
    DBG_DEBUG("FAT32", "set_fat_entry");
    
    // Read FAT sector
    u32 fat_lba = fat_start_lba + fat_sector_offset;
    if (!read_sector(fat_lba, sector_buffer)) {
        DBG_FAIL("FAT32", "Failed to read FAT sector");
        return false;
    }
    
    // Modify entry
    u32* fat_entries = reinterpret_cast<u32*>(sector_buffer);
    fat_entries[offset_in_sector] = (fat_entries[offset_in_sector] & 0xF0000000) | (value & 0x0FFFFFFF);
    
    // Write to all FAT copies
    for (u32 i = 0; i < fat_count; i++) {
        u32 this_fat_lba = fat_start_lba + (i * fat_size) + fat_sector_offset;
        if (!write_sector(this_fat_lba, sector_buffer)) {
            DBG_FAIL("FAT32", "Failed to write FAT sector");
            return false;
        }
    }
    
    // Invalidate cache if this was the cached sector
    if (fat_lba == cached_fat_sector) {
        cached_fat_sector = 0xFFFFFFFF;
    }
    
    fat_dirty = true;
    return true;
}

u32 FAT32Filesystem::alloc_cluster() {
    DBG_DEBUG("FAT32", "alloc_cluster: Finding free cluster");
    
    // Start searching from next_free_cluster hint or cluster 2
    u32 search_start = (next_free_cluster >= 2 && next_free_cluster < total_clusters + 2) 
                       ? next_free_cluster : 2;
    
    // Search for a free cluster
    for (u32 offset = 0; offset < total_clusters; offset++) {
        u32 cluster = search_start + offset;
        if (cluster >= total_clusters + 2) {
            cluster -= total_clusters;  // Wrap around
        }
        
        u32 value;
        if (!read_fat_entry(cluster, value)) {
            continue;
        }
        
        if (value == 0) {  // Free cluster
            // Mark as end of chain
            if (!set_fat_entry(cluster, 0x0FFFFFFF)) {
                return 0;
            }
            
            // Update hint and free count
            next_free_cluster = cluster + 1;
            if (free_clusters > 0) free_clusters--;
            
            // Zero out the cluster
            mem::memset(cluster_buffer, 0, cluster_size);
            write_cluster(cluster, cluster_buffer);
            
            DBG_OK("FAT32", "Allocated cluster");
            return cluster;
        }
    }
    
    DBG_FAIL("FAT32", "No free clusters");
    return 0;  // No free cluster found
}

u32 FAT32Filesystem::extend_cluster_chain(u32 last_cluster) {
    // Allocate a new cluster
    u32 new_cluster = alloc_cluster();
    if (new_cluster == 0) {
        return 0;
    }
    
    // Link it to the chain
    if (!set_fat_entry(last_cluster, new_cluster)) {
        // Failed to link, free the new cluster
        set_fat_entry(new_cluster, 0);
        return 0;
    }
    
    return new_cluster;
}

bool FAT32Filesystem::free_cluster_chain(u32 start_cluster) {
    DBG_DEBUG("FAT32", "free_cluster_chain");
    
    u32 cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        u32 next;
        if (!read_fat_entry(cluster, next)) {
            return false;
        }
        
        // Mark cluster as free
        if (!set_fat_entry(cluster, 0)) {
            return false;
        }
        
        free_clusters++;
        cluster = next;
    }
    
    return true;
}

// ===========================================================================
// Directory Write Operations
// ===========================================================================

u32 FAT32Filesystem::find_free_dir_entry(u32 parent_cluster) {
    u32 cluster = parent_cluster;
    
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return 0xFFFFFFFF;
        }
        
        FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
        u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
        
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (entries[i].is_end() || entries[i].is_free()) {
                // Found a free slot
                // Return combined info: (cluster << 16) | index_in_cluster
                return (cluster << 16) | i;
            }
        }
        
        cluster = get_next_cluster(cluster);
    }
    
    // No free entry, need to extend directory
    u32 last_cluster = parent_cluster;
    while (true) {
        u32 next = get_next_cluster(last_cluster);
        if (next == 0 || next >= 0x0FFFFFF8) break;
        last_cluster = next;
    }
    
    // Allocate new cluster for directory
    u32 new_cluster = extend_cluster_chain(last_cluster);
    if (new_cluster == 0) {
        return 0xFFFFFFFF;
    }
    
    // Return first entry in new cluster
    return (new_cluster << 16) | 0;
}

bool FAT32Filesystem::create_dir_entry(u32 parent_cluster, const char* name, u8 attr, 
                                       u32 file_cluster, u32 file_size) {
    DBG_DEBUG("FAT32", "create_dir_entry");
    Serial::log("FAT32", LogType::Debug, "  Name: ", name);
    
    // Find a free slot
    u32 slot = find_free_dir_entry(parent_cluster);
    if (slot == 0xFFFFFFFF) {
        DBG_FAIL("FAT32", "No free directory entry");
        return false;
    }
    
    u32 cluster = slot >> 16;
    u32 entry_index = slot & 0xFFFF;
    
    // Debug: print slot info
    char slot_info[64];
    slot_info[0] = 'S'; slot_info[1] = 'l'; slot_info[2] = 'o'; slot_info[3] = 't';
    slot_info[4] = ' '; slot_info[5] = 'c'; slot_info[6] = 'l'; slot_info[7] = 'u';
    slot_info[8] = 's'; slot_info[9] = 't'; slot_info[10] = 'e'; slot_info[11] = 'r';
    slot_info[12] = '='; int pos = 13;
    if (cluster == 0) { slot_info[pos++] = '0'; }
    else { char digits[12]; int dpos = 0; u32 tmp = cluster;
           while (tmp > 0) { digits[dpos++] = '0' + (tmp % 10); tmp /= 10; }
           while (dpos > 0) slot_info[pos++] = digits[--dpos]; }
    slot_info[pos++] = ' '; slot_info[pos++] = 'i'; slot_info[pos++] = 'n';
    slot_info[pos++] = 'd'; slot_info[pos++] = 'e'; slot_info[pos++] = 'x'; slot_info[pos++] = '=';
    if (entry_index == 0) { slot_info[pos++] = '0'; }
    else { char digits[12]; int dpos = 0; u32 tmp = entry_index;
           while (tmp > 0) { digits[dpos++] = '0' + (tmp % 10); tmp /= 10; }
           while (dpos > 0) slot_info[pos++] = digits[--dpos]; }
    slot_info[pos] = '\0';
    Serial::log("FAT32", LogType::Debug, slot_info);
    
    // Read the cluster
    if (!read_cluster(cluster, cluster_buffer)) {
        DBG_FAIL("FAT32", "Cannot read cluster for entry");
        return false;
    }
    
    FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
    FAT32DirEntry& entry = entries[entry_index];
    
    // Check if current slot was an end marker - we'll need to set a new one
    bool was_end_marker = entry.is_end();
    Serial::log("FAT32", LogType::Debug, was_end_marker ? "  Was end marker" : "  Reusing free slot");
    
    // Convert name to 8.3 format - write to the full 11-byte name region (name[8]+ext[3])
    char* entry_name = reinterpret_cast<char*>(&entry.name[0]);
    to_8_3_name(name, entry_name);
    
    // Set attributes
    entry.attributes = attr;
    entry.reserved = 0;
    entry.create_time_tenths = 0;
    entry.create_time = 0;
    entry.create_date = 0;
    entry.access_date = 0;
    entry.modify_time = 0;
    entry.modify_date = 0;
    entry.set_cluster(file_cluster);
    entry.file_size = file_size;
    
    // If we overwrote an end marker, set the next entry as end marker
    u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
    if (was_end_marker && entry_index + 1 < entries_per_cluster) {
        // Set next entry as end marker (zero out the entry)
        FAT32DirEntry* next = &entries[entry_index + 1];
        for (u32 k = 0; k < sizeof(FAT32DirEntry); k++) {
            reinterpret_cast<u8*>(next)[k] = 0;
        }
        DBG_DEBUG("FAT32", "  Set new end marker");
    }
    
    // Write back
    DBG_DEBUG("FAT32", "  Writing cluster");
    if (!write_cluster(cluster, cluster_buffer)) {
        DBG_FAIL("FAT32", "Failed to write directory entry");
        return false;
    }
    
    DBG_OK("FAT32", "Directory entry created");
    return true;
}

bool FAT32Filesystem::update_dir_entry(u32 parent_cluster, const char* name83, 
                                       u32 new_size, u32 new_cluster) {
    // Note: name83 is already in 8.3 format (11 chars, space-padded)
    
    u32 cluster = parent_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return false;
        }
        
        FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
        u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
        
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (entries[i].is_end()) {
                return false;  // Not found
            }
            
            if (!entries[i].is_free() && !entries[i].is_lfn()) {
                // Compare name - access the full 11-byte name region
                const char* entry_name = reinterpret_cast<const char*>(&entries[i].name[0]);
                bool match = true;
                for (int j = 0; j < 11; j++) {
                    if (entry_name[j] != name83[j]) {
                        match = false;
                        break;
                    }
                }
                
                if (match) {
                    // Update entry
                    entries[i].file_size = new_size;
                    if (new_cluster != 0) {
                        entries[i].set_cluster(new_cluster);
                    }
                    
                    // Write back
                    return write_cluster(cluster, cluster_buffer);
                }
            }
        }
        
        cluster = get_next_cluster(cluster);
    }
    
    return false;
}

bool FAT32Filesystem::delete_dir_entry(u32 parent_cluster, const char* name, bool already_8_3) {
    // Handle root directory (parent_cluster = 0 means root)
    if (parent_cluster == 0) {
        parent_cluster = root_cluster;
    }
    
    // Convert name to 8.3 format if not already
    char name83[12];
    str::set(name83, ' ', 11);
    name83[11] = '\0';
    if (already_8_3) {
        for (int i = 0; i < 11; i++) name83[i] = name[i];
    } else {
        to_8_3_name(name, name83);
    }
    
    u32 cluster = parent_cluster;
    
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return false;
        }
        
        FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
        u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
        
        for (u32 i = 0; i < entries_per_cluster; i++) {
            // Check for end of directory
            if (entries[i].name[0] == 0x00) {
                return false;  // Not found - end of directory
            }
            
            // Skip deleted entries
            if (static_cast<u8>(entries[i].name[0]) == 0xE5) {
                continue;
            }
            
            // Skip LFN entries
            if (entries[i].is_lfn()) {
                continue;
            }
            
            // Skip volume labels
            if (entries[i].attributes == 0x08) {
                continue;
            }
            
            // Compare name (case-insensitive, 11 chars)
            // NOTE: FAT32DirEntry has name[8] and ext[3] as separate fields
            const char* entry_name = reinterpret_cast<const char*>(&entries[i].name[0]);
            
            bool match = true;
            for (int j = 0; j < 11; j++) {
                char c1 = entry_name[j];
                char c2 = name83[j];
                // Convert to uppercase for comparison
                if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
                if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
                if (c1 != c2) {
                    match = false;
                    break;
                }
            }
            
            if (match) {
                // Get cluster to free
                u32 file_cluster = entries[i].get_cluster();
                
                // Mark entry as deleted (0xE5)
                entries[i].name[0] = static_cast<char>(0xE5);
                
                // Write back
                if (!write_cluster(cluster, cluster_buffer)) {
                    return false;
                }
                
                // Free the cluster chain
                if (file_cluster >= 2) {
                    free_cluster_chain(file_cluster);
                }
                
                return true;
            }
        }
        
        cluster = get_next_cluster(cluster);
    }
    
    return false;
}

u32 FAT32Filesystem::get_next_cluster(u32 cluster) {
    u32 next;
    if (!read_fat_entry(cluster, next)) {
        return 0;
    }
    
    // Check for end of chain
    if (next >= 0x0FFFFFF8) {
        return 0;  // End of chain
    }
    
    return next;
}

// ===========================================================================
// Directory Operations
// ===========================================================================

bool FAT32Filesystem::find_entry(const char* path, FAT32DirEntry& entry, u32& parent_cluster) {
    if (!mounted) return false;
    
    // Handle root
    if (path[0] == '/' && path[1] == '\0') {
        // Fake root entry
        str::set((char*)entry.name, ' ', 11);
        entry.attributes = 0x10;  // Directory
        entry.set_cluster(root_cluster);
        entry.file_size = 0;
        parent_cluster = 0;
        return true;
    }
    
    // Normalize path
    char norm_path[256];
    bolt::storage::path::normalize(path, norm_path, sizeof(norm_path));
    
    // Start at root
    u32 current_cluster = root_cluster;
    const char* p = norm_path;
    if (*p == '/') p++;
    
    FAT32DirEntry current_entry;
    str::set((char*)current_entry.name, ' ', 11);
    current_entry.attributes = 0x10;
    current_entry.set_cluster(root_cluster);
    current_entry.file_size = 0;
    
    parent_cluster = 0;
    
    while (*p) {
        // Extract component
        char component[256];
        u32 len = 0;
        while (*p && *p != '/' && len < 255) {
            component[len++] = *p++;
        }
        component[len] = '\0';
        
        if (len == 0) {
            if (*p == '/') p++;
            continue;
        }
        
        // Search in current directory
        bool found = false;
        u32 cluster = current_cluster;
        FAT32DirEntry dir_entry;
        // Note: long_name support is planned but not yet implemented
        
        while (cluster != 0 && cluster < 0x0FFFFFF8) {
            // Read cluster
            if (!read_cluster(cluster, cluster_buffer)) {
                return false;
            }
            
            u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
            const FAT32DirEntry* entries = reinterpret_cast<const FAT32DirEntry*>(cluster_buffer);
            
            for (u32 i = 0; i < entries_per_cluster; i++) {
                if (entries[i].is_end()) {
                    cluster = 0;
                    break;
                }
                
                if (entries[i].is_free() || entries[i].is_lfn() || 
                    entries[i].is_volume_label()) {
                    continue;
                }
                
                // Extract name
                char short_name[13];
                int pos = 0;
                for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
                    short_name[pos++] = entries[i].name[j];
                }
                if (entries[i].ext[0] != ' ') {
                    short_name[pos++] = '.';
                    for (int j = 0; j < 3 && entries[i].ext[j] != ' '; j++) {
                        short_name[pos++] = entries[i].ext[j];
                    }
                }
                short_name[pos] = '\0';
                
                if (compare_name(component, short_name)) {
                    found = true;
                    dir_entry = entries[i];
                    break;
                }
            }
            
            if (found) break;
            cluster = get_next_cluster(cluster);
        }
        
        if (!found) {
            return false;
        }
        
        parent_cluster = current_cluster;
        current_entry = dir_entry;
        current_cluster = dir_entry.get_cluster();
        
        if (*p == '/') p++;
        
        // If more path remaining, current must be directory
        if (*p && !dir_entry.is_directory()) {
            return false;
        }
    }
    
    entry = current_entry;
    return true;
}

void FAT32Filesystem::fill_file_info(const FAT32DirEntry& entry, const char* name, FileInfo& info) {
    info.clear();
    str::cpy(info.name, name);
    info.type = entry.is_directory() ? FileType::Directory : FileType::Regular;
    info.size = entry.file_size;
    info.inode = entry.get_cluster();
    info.attributes = entry.attributes;
}

bool FAT32Filesystem::compare_name(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        // To uppercase
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return false;
    }
    return *a == *b;
}

bool FAT32Filesystem::split_path(const char* path, char* dir, char* name) {
    if (!path || !dir || !name) return false;
    
    // Find last slash
    const char* last_slash = nullptr;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    if (!last_slash) {
        // No directory, just filename
        dir[0] = '/';
        dir[1] = '\0';
        str::cpy(name, path);
    } else if (last_slash == path) {
        // Root directory
        dir[0] = '/';
        dir[1] = '\0';
        str::cpy(name, last_slash + 1);
    } else {
        // Copy directory part
        u32 dir_len = last_slash - path;
        str::ncpy(dir, path, dir_len);
        dir[dir_len] = '\0';
        str::cpy(name, last_slash + 1);
    }
    
    return name[0] != '\0';
}

void FAT32Filesystem::to_8_3_name(const char* name, char* out) {
    // Initialize with spaces
    str::set(out, ' ', 11);
    
    if (!name || !*name) return;
    
    // Find the dot
    const char* dot = nullptr;
    for (const char* p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    
    // Copy base name (up to 8 chars)
    u32 i = 0;
    const char* p = name;
    while (*p && i < 8 && p != dot) {
        char c = *p++;
        // Convert to uppercase
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i++] = c;
    }
    
    // Copy extension (up to 3 chars)
    if (dot && dot[1]) {
        i = 8;
        p = dot + 1;
        while (*p && i < 11) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c -= 32;
            out[i++] = c;
        }
    }
}

// ===========================================================================
// File Operations
// ===========================================================================

VFSResult FAT32Filesystem::open(const char* path, FileMode mode, FileDescriptor& fd) {
    if (!mounted) return VFSResult::NotMounted;
    
    DBG_DEBUG("FAT32", "open: Opening file");
    Serial::log("FAT32", LogType::Debug, "  Path: ", path);
    
    FAT32DirEntry entry;
    u32 parent_cluster;
    
    bool file_exists = find_entry(path, entry, parent_cluster);
    
    if (!file_exists) {
        // File not found
        if (has_flag(mode, FileMode::Create)) {
            DBG_DEBUG("FAT32", "Creating new file");
            
            // Split path to get parent and name
            char parent_path[256];
            char name[256];
            
            if (!split_path(path, parent_path, name)) {
                return VFSResult::InvalidPath;
            }
            
            // Find parent directory
            FAT32DirEntry parent_entry;
            u32 grandparent_cluster;
            
            if (!find_entry(parent_path, parent_entry, grandparent_cluster)) {
                DBG_FAIL("FAT32", "Parent directory not found");
                return VFSResult::NotFound;
            }
            
            if (!parent_entry.is_directory()) {
                return VFSResult::NotDirectory;
            }
            
            parent_cluster = parent_entry.get_cluster();
            
            // Create file entry (no cluster allocated yet - will be on first write)
            if (!create_dir_entry(parent_cluster, name, 0x20, 0, 0)) {  // 0x20 = Archive attribute
                DBG_FAIL("FAT32", "Failed to create directory entry");
                return VFSResult::IOError;
            }
            
            // Set up file descriptor
            FAT32FileState* state = static_cast<FAT32FileState*>(
                Heap::alloc(sizeof(FAT32FileState))
            );
            if (!state) return VFSResult::NoSpace;
            
            state->start_cluster = 0;  // No data yet
            state->parent_cluster = parent_cluster;
            to_8_3_name(name, state->name);
            state->needs_dir_update = false;
            
            fd.position = 0;
            fd.size = 0;
            fd.inode = 0;
            fd.mode = mode;
            fd.type = FileType::Regular;
            fd.fs_data = state;
            
            DBG_OK("FAT32", "File created");
            return VFSResult::Success;
        }
        return VFSResult::NotFound;
    }
    
    if (entry.is_directory()) {
        return VFSResult::IsDirectory;
    }
    
    // File exists - open it
    FAT32FileState* state = static_cast<FAT32FileState*>(
        Heap::alloc(sizeof(FAT32FileState))
    );
    if (!state) return VFSResult::NoSpace;
    
    state->start_cluster = entry.get_cluster();
    state->parent_cluster = parent_cluster;
    
    // Get name for potential updates
    char name[256];
    bolt::storage::path::basename(path, name, sizeof(name));
    to_8_3_name(name, state->name);
    state->needs_dir_update = false;
    
    fd.position = 0;
    fd.size = entry.file_size;
    fd.inode = entry.get_cluster();
    fd.mode = mode;
    fd.type = FileType::Regular;
    fd.fs_data = state;
    
    // If truncate mode, free existing clusters and reset size
    if (has_flag(mode, FileMode::Truncate) && entry.get_cluster() != 0) {
        free_cluster_chain(entry.get_cluster());
        state->start_cluster = 0;
        fd.size = 0;
        fd.inode = 0;
        state->needs_dir_update = true;
        update_dir_entry(parent_cluster, state->name, 0, 0);
    }
    
    // If append mode, seek to end
    if (has_flag(mode, FileMode::Append)) {
        fd.position = fd.size;
    }
    
    DBG_OK("FAT32", "File opened");
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::close(FileDescriptor& fd) {
    // Update directory entry if needed
    FAT32FileState* state = static_cast<FAT32FileState*>(fd.fs_data);
    if (state) {
        if (state->needs_dir_update && state->parent_cluster != 0) {
            update_dir_entry(state->parent_cluster, state->name,
                           static_cast<u32>(fd.size), state->start_cluster);
        }
        Heap::free(state);
    }
    fd.fs_data = nullptr;
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::read(FileDescriptor& fd, void* buffer, u64 size, u64& bytes_read) {
    if (!mounted) return VFSResult::NotMounted;
    
    bytes_read = 0;
    
    if (fd.position >= fd.size) {
        return VFSResult::Success;  // EOF
    }
    
    // Clamp read size
    u64 remaining = fd.size - fd.position;
    if (size > remaining) size = remaining;
    
    VFSResult result = read_file_data(fd.inode, fd.position, buffer, size, fd.size, bytes_read);
    
    // Update file position
    fd.position += bytes_read;
    
    return result;
}

VFSResult FAT32Filesystem::read_file_data(u32 start_cluster, u64 offset, void* buffer, 
                                           u64 size, u64 file_size, u64& bytes_read) {
    bytes_read = 0;
    u8* out = static_cast<u8*>(buffer);
    
    // Navigate to starting cluster
    u32 cluster = start_cluster;
    u64 cluster_offset = 0;
    
    while (cluster_offset + cluster_size <= offset) {
        cluster = get_next_cluster(cluster);
        if (cluster == 0) return VFSResult::IOError;
        cluster_offset += cluster_size;
    }
    
    // Read data
    while (size > 0 && cluster != 0) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return VFSResult::IOError;
        }
        
        u64 offset_in_cluster = (offset + bytes_read) - cluster_offset;
        u64 available = cluster_size - offset_in_cluster;
        u64 to_copy = size < available ? size : available;
        
        // Don't read past file end
        if (offset + bytes_read + to_copy > file_size) {
            to_copy = file_size - (offset + bytes_read);
        }
        
        for (u64 i = 0; i < to_copy; i++) {
            out[i] = cluster_buffer[offset_in_cluster + i];
        }
        
        out += to_copy;
        size -= to_copy;
        bytes_read += to_copy;
        
        cluster_offset += cluster_size;
        cluster = get_next_cluster(cluster);
    }
    
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::write(FileDescriptor& fd, const void* buffer, u64 size, u64& bytes_written) {
    if (!mounted) return VFSResult::NotMounted;
    
    DBG_DEBUG("FAT32", "write: Writing file data");
    
    bytes_written = 0;
    const u8* src = static_cast<const u8*>(buffer);
    
    // Get file state
    FAT32FileState* state = static_cast<FAT32FileState*>(fd.fs_data);
    if (!state) {
        DBG_FAIL("FAT32", "No file state");
        return VFSResult::BadDescriptor;
    }
    
    u32 current_cluster = state->start_cluster;
    u64 cluster_offset = 0;
    
    // If file has no clusters yet, allocate the first one
    if (current_cluster == 0 && size > 0) {
        current_cluster = alloc_cluster();
        if (current_cluster == 0) {
            return VFSResult::NoSpace;
        }
        state->start_cluster = current_cluster;
        state->needs_dir_update = true;
    }
    
    // Navigate to the cluster at current position
    u64 pos = fd.position;
    while (pos >= cluster_size && current_cluster != 0 && current_cluster < 0x0FFFFFF8) {
        pos -= cluster_size;
        cluster_offset += cluster_size;
        
        u32 next = get_next_cluster(current_cluster);
        if (next == 0 || next >= 0x0FFFFFF8) {
            // Need to extend
            next = extend_cluster_chain(current_cluster);
            if (next == 0) {
                return VFSResult::NoSpace;
            }
        }
        current_cluster = next;
    }
    
    // Write data
    while (size > 0 && current_cluster != 0 && current_cluster < 0x0FFFFFF8) {
        // Read current cluster
        if (!read_cluster(current_cluster, cluster_buffer)) {
            return VFSResult::IOError;
        }
        
        // Calculate position in cluster (use 32-bit math to avoid 64-bit division)
        u32 pos_low = static_cast<u32>(fd.position);
        u32 offset_in_cluster = pos_low % cluster_size;
        u32 space_in_cluster = cluster_size - offset_in_cluster;
        u32 to_write = (size < space_in_cluster) ? static_cast<u32>(size) : space_in_cluster;
        
        // Copy data
        mem::memcpy(cluster_buffer + offset_in_cluster, src, to_write);
        
        // Write cluster back
        if (!write_cluster(current_cluster, cluster_buffer)) {
            return VFSResult::IOError;
        }
        
        src += to_write;
        size -= to_write;
        bytes_written += to_write;
        fd.position += to_write;
        
        // Update file size if we extended
        if (fd.position > fd.size) {
            fd.size = fd.position;
            state->needs_dir_update = true;
        }
        
        // Move to next cluster if needed (use 32-bit math)
        u32 pos_low2 = static_cast<u32>(fd.position);
        if (pos_low2 % cluster_size == 0 && size > 0) {
            u32 next = get_next_cluster(current_cluster);
            if (next == 0 || next >= 0x0FFFFFF8) {
                // Extend chain
                next = extend_cluster_chain(current_cluster);
                if (next == 0) {
                    break;  // Out of space
                }
            }
            current_cluster = next;
        }
    }
    
    // Update directory entry if needed
    if (state->needs_dir_update && state->parent_cluster != 0) {
        update_dir_entry(state->parent_cluster, state->name, 
                        static_cast<u32>(fd.size), state->start_cluster);
        state->needs_dir_update = false;
    }
    
    DBG_OK("FAT32", "Write complete");
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::seek(FileDescriptor& fd, i64 offset, SeekMode mode) {
    i64 new_pos;
    
    switch (mode) {
        case SeekMode::Set:
            new_pos = offset;
            break;
        case SeekMode::Current:
            new_pos = static_cast<i64>(fd.position) + offset;
            break;
        case SeekMode::End:
            new_pos = static_cast<i64>(fd.size) + offset;
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

// ===========================================================================
// Directory Operations
// ===========================================================================

VFSResult FAT32Filesystem::opendir(const char* path, FileDescriptor& fd) {
    if (!mounted) return VFSResult::NotMounted;
    
    FAT32DirEntry entry;
    u32 parent_cluster;
    
    if (!find_entry(path, entry, parent_cluster)) {
        return VFSResult::NotFound;
    }
    
    if (!entry.is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    // Allocate state
    FAT32DirState* state = static_cast<FAT32DirState*>(
        Heap::alloc(sizeof(FAT32DirState))
    );
    if (!state) return VFSResult::NoSpace;
    
    state->cluster = entry.get_cluster();
    state->sector_in_cluster = 0;
    state->entry_in_sector = 0;
    state->entry_index = 0;
    state->at_end = false;
    
    fd.inode = entry.get_cluster();
    fd.type = FileType::Directory;
    fd.fs_data = state;
    
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::readdir(FileDescriptor& fd, FileInfo& info) {
    if (!mounted) return VFSResult::NotMounted;
    
    FAT32DirState* state = static_cast<FAT32DirState*>(fd.fs_data);
    if (!state) return VFSResult::BadDescriptor;
    
    if (state->at_end) {
        return VFSResult::NotFound;
    }
    
    u32 cluster = state->cluster;
    u32 entry_to_return = state->entry_index;  // Which valid entry to return (0 = first, 1 = second, etc.)
    u32 valid_count = 0;  // Count of valid (non-deleted, non-LFN, non-volume) entries seen
    
    while (cluster != 0 && cluster < 0x0FFFFFF8) {
        if (!read_cluster(cluster, cluster_buffer)) {
            return VFSResult::IOError;
        }
        
        u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
        const FAT32DirEntry* entries = reinterpret_cast<const FAT32DirEntry*>(cluster_buffer);
        
        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (entries[i].is_end()) {
                state->at_end = true;
                return VFSResult::NotFound;
            }
            
            // Skip free/deleted, LFN, and volume label entries
            if (entries[i].is_free() || entries[i].is_lfn() || 
                entries[i].is_volume_label()) {
                continue;
            }
            
            // This is a valid entry
            if (valid_count == entry_to_return) {
                // Found the entry we want to return
                char name[13];
                int pos = 0;
                for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
                    name[pos++] = entries[i].name[j];
                }
                if (entries[i].ext[0] != ' ') {
                    name[pos++] = '.';
                    for (int j = 0; j < 3 && entries[i].ext[j] != ' '; j++) {
                        name[pos++] = entries[i].ext[j];
                    }
                }
                name[pos] = '\0';
                
                fill_file_info(entries[i], name, info);
                
                state->entry_index++;  // Next call returns the next valid entry
                state->cluster = cluster;
                
                return VFSResult::Success;
            }
            
            valid_count++;
        }
        
        cluster = get_next_cluster(cluster);
    }
    
    state->at_end = true;
    return VFSResult::NotFound;
}

VFSResult FAT32Filesystem::closedir(FileDescriptor& fd) {
    if (fd.fs_data) {
        Heap::free(fd.fs_data);
        fd.fs_data = nullptr;
    }
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::mkdir(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    DBG_DEBUG("FAT32", "mkdir: Creating directory");
    Serial::log("FAT32", LogType::Debug, "  Path: ", path);
    
    // Split path into parent and name
    char parent_path[256];
    char name[256];
    
    if (!split_path(path, parent_path, name)) {
        return VFSResult::InvalidPath;
    }
    
    // Find parent directory
    FAT32DirEntry parent_entry;
    u32 grandparent_cluster;
    
    if (!find_entry(parent_path, parent_entry, grandparent_cluster)) {
        DBG_FAIL("FAT32", "Parent not found");
        return VFSResult::NotFound;
    }
    
    if (!parent_entry.is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    u32 parent_cluster = parent_entry.get_cluster();
    
    // Check if already exists
    FAT32DirEntry existing;
    u32 dummy;
    if (find_entry(path, existing, dummy)) {
        DBG_WARN("FAT32", "Already exists");
        return VFSResult::AlreadyExists;
    }
    
    // Allocate cluster for new directory
    u32 new_cluster = alloc_cluster();
    if (new_cluster == 0) {
        DBG_FAIL("FAT32", "No free clusters");
        return VFSResult::NoSpace;
    }
    
    // Initialize directory with . and .. entries
    mem::memset(cluster_buffer, 0, cluster_size);
    FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
    
    // . entry (self)
    str::set(entries[0].name, ' ', 8);
    str::set(entries[0].ext, ' ', 3);
    entries[0].name[0] = '.';
    entries[0].attributes = 0x10;  // Directory
    entries[0].set_cluster(new_cluster);
    entries[0].file_size = 0;
    
    // .. entry (parent)
    str::set(entries[1].name, ' ', 8);
    str::set(entries[1].ext, ' ', 3);
    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    entries[1].attributes = 0x10;  // Directory
    entries[1].set_cluster(parent_cluster == root_cluster ? 0 : parent_cluster);
    entries[1].file_size = 0;
    
    // Write new directory cluster
    if (!write_cluster(new_cluster, cluster_buffer)) {
        free_cluster_chain(new_cluster);
        return VFSResult::IOError;
    }
    
    // Create entry in parent directory
    if (!create_dir_entry(parent_cluster, name, 0x10, new_cluster, 0)) {
        free_cluster_chain(new_cluster);
        return VFSResult::IOError;
    }
    
    DBG_OK("FAT32", "Directory created");
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::rmdir(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    DBG_DEBUG("FAT32", "rmdir: Removing directory");
    Serial::log("FAT32", LogType::Debug, "  Path: ", path);
    
    // Can't remove root
    if (path[0] == '/' && path[1] == '\0') {
        return VFSResult::AccessDenied;
    }
    
    // Find the directory
    FAT32DirEntry entry;
    u32 parent_cluster;
    
    if (!find_entry(path, entry, parent_cluster)) {
        DBG_FAIL("FAT32", "Directory not found");
        return VFSResult::NotFound;
    }
    
    DBG_OK("FAT32", "Found directory entry");
    
    if (!entry.is_directory()) {
        return VFSResult::NotDirectory;
    }
    
    // Check if directory is empty (only . and ..)
    u32 dir_cluster = entry.get_cluster();
    if (!read_cluster(dir_cluster, cluster_buffer)) {
        DBG_FAIL("FAT32", "Cannot read directory cluster");
        return VFSResult::IOError;
    }
    
    FAT32DirEntry* entries = reinterpret_cast<FAT32DirEntry*>(cluster_buffer);
    u32 entries_per_cluster = cluster_size / sizeof(FAT32DirEntry);
    
    for (u32 i = 2; i < entries_per_cluster; i++) {  // Skip . and ..
        if (entries[i].is_end()) break;
        if (!entries[i].is_free()) {
            DBG_FAIL("FAT32", "Directory not empty");
            return VFSResult::NotEmpty;
        }
    }
    
    // Get name from path for deletion
    char name[256];
    bolt::storage::path::basename(path, name, sizeof(name));
    
    // Delete directory entry
    if (!delete_dir_entry(parent_cluster, name)) {
        return VFSResult::IOError;
    }
    
    DBG_OK("FAT32", "Directory removed");
    return VFSResult::Success;
}

// ===========================================================================
// File Management
// ===========================================================================

VFSResult FAT32Filesystem::stat(const char* path, FileInfo& info) {
    if (!mounted) return VFSResult::NotMounted;
    
    FAT32DirEntry entry;
    u32 parent_cluster;
    
    if (!find_entry(path, entry, parent_cluster)) {
        return VFSResult::NotFound;
    }
    
    // Extract name from path
    char name[256];
    bolt::storage::path::basename(path, name, sizeof(name));
    if (name[0] == '\0') {
        str::cpy(name, "/");
    }
    
    fill_file_info(entry, name, info);
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::unlink(const char* path) {
    if (!mounted) return VFSResult::NotMounted;
    
    DBG_DEBUG("FAT32", "unlink: Deleting file");
    Serial::log("FAT32", LogType::Debug, "  Path: ", path);
    
    // Find the file
    FAT32DirEntry entry;
    u32 parent_cluster;
    
    if (!find_entry(path, entry, parent_cluster)) {
        DBG_FAIL("FAT32", "File not found");
        return VFSResult::NotFound;
    }
    
    // Can't delete directories with unlink
    if (entry.is_directory()) {
        return VFSResult::IsDirectory;
    }
    
    // Get name from path
    char name[256];
    bolt::storage::path::basename(path, name, sizeof(name));
    
    // Delete the entry
    if (!delete_dir_entry(parent_cluster, name)) {
        return VFSResult::IOError;
    }
    
    DBG_OK("FAT32", "File deleted");
    return VFSResult::Success;
}

VFSResult FAT32Filesystem::rename(const char* /* old_path */, const char* /* new_path */) {
    // TODO: Implement
    return VFSResult::Unsupported;
}

// ===========================================================================
// Filesystem Info
// ===========================================================================

u64 FAT32Filesystem::total_space() const {
    return (u64)total_clusters * cluster_size;
}

u64 FAT32Filesystem::free_space() const {
    return (u64)free_clusters * cluster_size;
}

VFSResult FAT32Filesystem::sync() {
    // No write caching implemented yet
    return VFSResult::Success;
}

// ===========================================================================
// Factory Function
// ===========================================================================

Filesystem* create_fat32_filesystem() {
    return new FAT32Filesystem();
}

} // namespace bolt::storage
