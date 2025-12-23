/* ===========================================================================
 * BOLT OS - Partition Table Manager Implementation
 * =========================================================================== */

#include "partition.hpp"
#include "../drivers/serial/serial.hpp"
#include "../drivers/video/console.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
PartitionInfo PartitionManager::partitions[MAX_PARTITIONS];
u32 PartitionManager::partition_count = 0;
bool PartitionManager::initialized = false;

// Sector buffer (512 bytes)
static u8 sector_buffer[512] __attribute__((aligned(4)));

void PartitionManager::init() {
    DBG_LOADING("PART", "Initializing partition manager...");
    
    for (u32 i = 0; i < MAX_PARTITIONS; i++) {
        partitions[i].clear();
    }
    partition_count = 0;
    initialized = true;
    
    DBG_OK("PART", "Partition manager ready");
}

bool PartitionManager::read_sector(BlockDevice* device, u64 lba, void* buffer) {
    IOResult result = device->read_sectors(lba, 1, buffer);
    if (result != IOResult::Success) {
        DBG_ERROR("PART", "Read error");
        return false;
    }
    return true;
}

PartitionScheme PartitionManager::detect_scheme(BlockDevice* device) {
    if (!device) return PartitionScheme::Unknown;
    
    // Read MBR (sector 0)
    if (!read_sector(device, 0, sector_buffer)) {
        return PartitionScheme::Unknown;
    }
    
    const MBR* mbr = reinterpret_cast<const MBR*>(sector_buffer);
    
    // Check MBR signature
    if (!mbr->is_valid()) {
        DBG_WARN("PART", "No valid MBR signature");
        return PartitionScheme::Unknown;
    }
    
    // Check for protective MBR (indicates GPT)
    if (mbr->partitions[0].is_gpt_protective()) {
        // Verify GPT header
        if (!read_sector(device, 1, sector_buffer)) {
            return PartitionScheme::Unknown;
        }
        
        const GPTHeader* gpt = reinterpret_cast<const GPTHeader*>(sector_buffer);
        if (gpt->is_valid()) {
            DBG_LOADING("PART", "GPT partition scheme detected");
            return PartitionScheme::GPT;
        }
    }
    
    // Standard MBR
    DBG_LOADING("PART", "MBR partition scheme detected");
    return PartitionScheme::MBR;
}

u32 PartitionManager::scan_device(BlockDevice* device) {
    if (!initialized || !device) return 0;
    
    partition_count = 0;
    
    const DeviceInfo& info = device->get_info();
    DBG_DEBUG("PART", info.name);
    
    // Detect partition scheme
    PartitionScheme scheme = detect_scheme(device);
    
    u32 found = 0;
    
    switch (scheme) {
        case PartitionScheme::GPT:
            found = parse_gpt(device);
            break;
            
        case PartitionScheme::MBR:
            // Re-read MBR
            if (read_sector(device, 0, sector_buffer)) {
                const MBR* mbr = reinterpret_cast<const MBR*>(sector_buffer);
                found = parse_mbr(device, mbr);
            }
            break;
            
        default:
            DBG_WARN("PART", "No partition table found");
            break;
    }
    
    DBG_DEBUG("PART", found > 0 ? "Partitions found" : "No partitions");
    
    return found;
}

u32 PartitionManager::parse_mbr(BlockDevice* device, const MBR* mbr) {
    u32 found = 0;
    
    // Process 4 primary partitions
    for (int i = 0; i < 4; i++) {
        const MBRPartitionEntry& entry = mbr->partitions[i];
        
        if (entry.is_empty()) continue;
        
        // Log partition discovery
        char info[32];
        char* dst = info;
        *dst++ = 'P';
        *dst++ = '1' + i;
        *dst++ = ' ';
        *dst++ = 'T';
        *dst++ = 'y';
        *dst++ = 'p';
        *dst++ = 'e';
        *dst++ = '=';
        *dst++ = '0';
        *dst++ = 'x';
        *dst++ = "0123456789ABCDEF"[(entry.type >> 4) & 0xF];
        *dst++ = "0123456789ABCDEF"[entry.type & 0xF];
        *dst = '\0';
        DBG_DEBUG("PART", info);
        
        if (entry.is_extended()) {
            // Parse extended partition
            u8 part_idx = found;
            found += parse_extended(device, entry.start_lba, entry.sector_count, part_idx);
        } else {
            // Create primary partition info
            if (partition_count < MAX_PARTITIONS) {
                PartitionInfo& pi = partitions[partition_count];
                pi.clear();
                pi.start_lba = entry.start_lba;
                pi.sector_count = entry.sector_count;
                pi.size_bytes = (u64)entry.sector_count * 512;
                pi.type_mbr = entry.type;
                pi.index = i;
                pi.bootable = entry.is_bootable();
                pi.is_gpt = false;
                
                // Generate label
                const DeviceInfo& dev_info = device->get_info();
                str::cpy(pi.label, dev_info.name);
                usize len = str::len(pi.label);
                pi.label[len] = '1' + i;
                pi.label[len + 1] = '\0';
                
                // Create partition device
                create_partition(device, pi);
                partition_count++;
                found++;
            }
        }
    }
    
    return found;
}

u32 PartitionManager::parse_extended(BlockDevice* device, u32 ext_start, u32 /* ext_size */, u8& /* part_index */) {
    u32 found = 0;
    u32 ebr_lba = ext_start;
    u8 logical_num = 5;  // Logical drives start at 5
    
    while (ebr_lba != 0 && found < 32) {  // Safety limit
        // Read Extended Boot Record
        if (!read_sector(device, ebr_lba, sector_buffer)) {
            break;
        }
        
        const MBR* ebr = reinterpret_cast<const MBR*>(sector_buffer);
        
        if (!ebr->is_valid()) break;
        
        // First entry: logical partition
        const MBRPartitionEntry& logical = ebr->partitions[0];
        if (!logical.is_empty() && !logical.is_extended()) {
            Serial::write("[PART]   L");
            Serial::write_dec(logical_num);
            Serial::write(": Type=0x");
            Serial::write_hex(logical.type);
            Serial::write(" LBA=");
            Serial::write_dec(ebr_lba + logical.start_lba);
            Serial::write(" Sectors=");
            Serial::write_dec(logical.sector_count);
            Serial::writeln("");
            
            if (partition_count < MAX_PARTITIONS) {
                PartitionInfo& pi = partitions[partition_count];
                pi.clear();
                pi.start_lba = ebr_lba + logical.start_lba;
                pi.sector_count = logical.sector_count;
                pi.size_bytes = (u64)logical.sector_count * 512;
                pi.type_mbr = logical.type;
                pi.index = logical_num - 1;
                pi.bootable = logical.is_bootable();
                pi.is_gpt = false;
                
                // Generate label
                const DeviceInfo& dev_info = device->get_info();
                str::cpy(pi.label, dev_info.name);
                usize len = str::len(pi.label);
                if (logical_num >= 10) {
                    pi.label[len] = '0' + (logical_num / 10);
                    pi.label[len + 1] = '0' + (logical_num % 10);
                    pi.label[len + 2] = '\0';
                } else {
                    pi.label[len] = '0' + logical_num;
                    pi.label[len + 1] = '\0';
                }
                
                create_partition(device, pi);
                partition_count++;
                found++;
            }
            logical_num++;
        }
        
        // Second entry: link to next EBR
        const MBRPartitionEntry& next = ebr->partitions[1];
        if (next.is_empty() || !next.is_extended()) {
            break;
        }
        ebr_lba = ext_start + next.start_lba;
    }
    
    return found;
}

u32 PartitionManager::parse_gpt(BlockDevice* device) {
    // Read GPT header (LBA 1)
    if (!read_sector(device, 1, sector_buffer)) {
        return 0;
    }
    
    GPTHeader gpt;
    // Copy header to avoid alignment issues
    for (usize i = 0; i < sizeof(GPTHeader); i++) {
        ((u8*)&gpt)[i] = sector_buffer[i];
    }
    
    if (!gpt.is_valid()) {
        DBG_WARN("PART", "Invalid GPT header");
        return 0;
    }
    
    Serial::write("[PART] GPT: ");
    Serial::write_dec(gpt.partition_entry_count);
    Serial::write(" entries, ");
    Serial::write_dec(gpt.partition_entry_size);
    Serial::writeln(" bytes each");
    
    u32 found = 0;
    u64 entry_lba = gpt.partition_table_lba;
    u32 entries_per_sector = 512 / gpt.partition_entry_size;
    
    for (u32 i = 0; i < gpt.partition_entry_count && found < MAX_PARTITIONS; i++) {
        // Calculate which sector this entry is in
        u32 sector_offset = i / entries_per_sector;
        u32 entry_in_sector = i % entries_per_sector;
        
        // Read sector if needed
        if (entry_in_sector == 0) {
            if (!read_sector(device, entry_lba + sector_offset, sector_buffer)) {
                break;
            }
        }
        
        const GPTPartitionEntry* entry = reinterpret_cast<const GPTPartitionEntry*>(
            sector_buffer + (entry_in_sector * gpt.partition_entry_size)
        );
        
        if (entry->is_empty()) continue;
        
        Serial::write("[PART]   P");
        Serial::write_dec(i + 1);
        Serial::write(": LBA=");
        Serial::write_dec(entry->start_lba);
        Serial::write(" End=");
        Serial::write_dec(entry->end_lba);
        Serial::writeln("");
        
        PartitionInfo& pi = partitions[partition_count];
        pi.clear();
        pi.start_lba = entry->start_lba;
        pi.sector_count = entry->sector_count();
        pi.size_bytes = pi.sector_count * 512;
        pi.is_gpt = true;
        pi.index = i;
        
        // Copy GUIDs
        for (int j = 0; j < 16; j++) {
            pi.type_guid[j] = entry->type_guid[j];
            pi.partition_guid[j] = entry->unique_guid[j];
        }
        
        // Extract name (UTF-16 to ASCII)
        for (int j = 0; j < 36 && entry->name[j]; j++) {
            pi.name[j] = (char)(entry->name[j] & 0xFF);
        }
        
        // Generate label
        const DeviceInfo& dev_info = device->get_info();
        str::cpy(pi.label, dev_info.name);
        usize len = str::len(pi.label);
        pi.label[len] = '1' + found;
        pi.label[len + 1] = '\0';
        
        create_partition(device, pi);
        partition_count++;
        found++;
    }
    
    return found;
}

bool PartitionManager::create_partition(BlockDevice* parent, const PartitionInfo& info) {
    // Create partition device wrapper
    PartitionDevice* part = new PartitionDevice(
        parent, 
        info.start_lba, 
        info.sector_count,
        info.index
    );
    
    if (!part) {
        DBG_FAIL("PART", "Failed to create partition device");
        return false;
    }
    
    // Register with block device manager
    return BlockDeviceManager::register_device(part);
}

const char* PartitionManager::mbr_type_name(u8 type) {
    switch (type) {
        case MBRType::Empty:          return "Empty";
        case MBRType::FAT12:          return "FAT12";
        case MBRType::FAT16_Small:    return "FAT16 <32MB";
        case MBRType::Extended_CHS:   return "Extended (CHS)";
        case MBRType::FAT16:          return "FAT16";
        case MBRType::NTFS:           return "NTFS/exFAT";
        case MBRType::FAT32_CHS:      return "FAT32 (CHS)";
        case MBRType::FAT32_LBA:      return "FAT32 (LBA)";
        case MBRType::FAT16_LBA:      return "FAT16 (LBA)";
        case MBRType::Extended_LBA:   return "Extended (LBA)";
        case MBRType::Linux_Swap:     return "Linux Swap";
        case MBRType::Linux_Native:   return "Linux";
        case MBRType::Linux_LVM:      return "Linux LVM";
        case MBRType::GPT_Protective: return "GPT Protective";
        case MBRType::EFI_System:     return "EFI System";
        default:                      return "Unknown";
    }
}

void PartitionManager::print_partitions(BlockDevice* device) {
    if (!device) return;
    
    const DeviceInfo& dev_info = device->get_info();
    
    Console::set_color(Color::Yellow);
    Console::print("Partition table for ");
    Console::print(dev_info.name);
    Console::println(":");
    Console::set_color(Color::LightGray);
    
    // Scan device (this will print findings to serial)
    PartitionScheme scheme = detect_scheme(device);
    
    if (scheme == PartitionScheme::Unknown) {
        Console::println("  No partition table found");
        return;
    }
    
    // Print partitions already registered
    u32 count = BlockDeviceManager::get_device_count();
    bool found_any = false;
    
    for (u32 i = 0; i < count; i++) {
        BlockDevice* bd = BlockDeviceManager::get_device(i);
        if (bd->get_info().type == DeviceType::Partition) {
            // Check if this partition belongs to our device
            // (by checking name prefix)
            const char* part_name = bd->get_info().name;
            if (str::ncmp(part_name, dev_info.name, str::len(dev_info.name)) == 0) {
                Console::print("  ");
                Console::set_color(Color::LightCyan);
                Console::print(part_name);
                Console::set_color(Color::LightGray);
                Console::print("  ");
                Console::print_dec(bd->size_mb());
                Console::print(" MB");
                Console::println("");
                found_any = true;
            }
        }
    }
    
    if (!found_any) {
        Console::println("  No partitions");
    }
}

// ===========================================================================
// Utility Functions
// ===========================================================================

bool guid_equal(const u8* a, const u8* b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

void guid_to_string(const u8* guid, char* str) {
    static const char hex[] = "0123456789ABCDEF";
    
    // Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    // Bytes:  0-3      4-5  6-7  8-9  10-15
    int pos = 0;
    
    // First group (bytes 3-0, reversed)
    for (int i = 3; i >= 0; i--) {
        str[pos++] = hex[(guid[i] >> 4) & 0xF];
        str[pos++] = hex[guid[i] & 0xF];
    }
    str[pos++] = '-';
    
    // Second group (bytes 5-4)
    for (int i = 5; i >= 4; i--) {
        str[pos++] = hex[(guid[i] >> 4) & 0xF];
        str[pos++] = hex[guid[i] & 0xF];
    }
    str[pos++] = '-';
    
    // Third group (bytes 7-6)
    for (int i = 7; i >= 6; i--) {
        str[pos++] = hex[(guid[i] >> 4) & 0xF];
        str[pos++] = hex[guid[i] & 0xF];
    }
    str[pos++] = '-';
    
    // Fourth group (bytes 8-9)
    for (int i = 8; i <= 9; i++) {
        str[pos++] = hex[(guid[i] >> 4) & 0xF];
        str[pos++] = hex[guid[i] & 0xF];
    }
    str[pos++] = '-';
    
    // Fifth group (bytes 10-15)
    for (int i = 10; i <= 15; i++) {
        str[pos++] = hex[(guid[i] >> 4) & 0xF];
        str[pos++] = hex[guid[i] & 0xF];
    }
    
    str[pos] = '\0';
}

} // namespace bolt::storage
