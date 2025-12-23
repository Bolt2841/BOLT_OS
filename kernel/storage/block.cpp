/* ===========================================================================
 * BOLT OS - Block Device Manager Implementation
 * =========================================================================== */

#include "block.hpp"
#include "../drivers/serial/serial.hpp"
#include "../drivers/video/console.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
BlockDevice* BlockDeviceManager::devices[MAX_DEVICES];
u32 BlockDeviceManager::device_count = 0;
bool BlockDeviceManager::initialized = false;

// Device naming counters
static u8 hd_count = 0;   // hda, hdb, hdc...
static u8 sd_count = 0;   // sda, sdb, sdc... (for SATA/SCSI)
static u8 cd_count = 0;   // cd0, cd1...
static u8 rd_count = 0;   // rd0, rd1... (RAM disks)
static u8 fd_count = 0;   // fd0, fd1... (floppies)

void BlockDeviceManager::init() {
    DBG_LOADING("BLK", "Initializing block device manager...");
    
    for (u32 i = 0; i < MAX_DEVICES; i++) {
        devices[i] = nullptr;
    }
    device_count = 0;
    
    // Reset naming counters
    hd_count = 0;
    sd_count = 0;
    cd_count = 0;
    rd_count = 0;
    fd_count = 0;
    
    initialized = true;
    DBG_OK("BLK", "Block device manager ready");
}

void BlockDeviceManager::generate_device_name(BlockDevice* dev, char* name) {
    const DeviceInfo& info = dev->get_info();
    
    switch (info.type) {
        case DeviceType::ATA_HDD:
        case DeviceType::ATA_SSD:
            name[0] = 'h';
            name[1] = 'd';
            name[2] = 'a' + hd_count++;
            name[3] = '\0';
            break;
            
        case DeviceType::AHCI_HDD:
        case DeviceType::AHCI_SSD:
        case DeviceType::NVMe:
        case DeviceType::USB_Mass:
            name[0] = 's';
            name[1] = 'd';
            name[2] = 'a' + sd_count++;
            name[3] = '\0';
            break;
            
        case DeviceType::ATAPI_CDROM:
            name[0] = 'c';
            name[1] = 'd';
            name[2] = '0' + cd_count++;
            name[3] = '\0';
            break;
            
        case DeviceType::RAMDisk:
            name[0] = 'r';
            name[1] = 'd';
            name[2] = '0' + rd_count++;
            name[3] = '\0';
            break;
            
        case DeviceType::Floppy:
            name[0] = 'f';
            name[1] = 'd';
            name[2] = '0' + fd_count++;
            name[3] = '\0';
            break;
            
        case DeviceType::Partition:
            // Partition names are generated separately
            str::cpy(name, "part");
            break;
            
        default:
            name[0] = 'd';
            name[1] = 'e';
            name[2] = 'v';
            name[3] = '0' + device_count;
            name[4] = '\0';
            break;
    }
}

bool BlockDeviceManager::register_device(BlockDevice* device) {
    if (!initialized) {
        DBG_FAIL("BLK", "Manager not initialized");
        return false;
    }
    
    if (device_count >= MAX_DEVICES) {
        DBG_WARN("BLK", "Device limit reached");
        return false;
    }
    
    // Generate name for device
    DeviceInfo& info = const_cast<DeviceInfo&>(device->get_info());
    if (info.name[0] == '\0') {
        generate_device_name(device, info.name);
    }
    
    // Add to array
    devices[device_count++] = device;
    
    // Log registration
    char msg[80];
    char* dst = msg;
    const char* src = "/dev/";
    while (*src) *dst++ = *src++;
    src = info.name;
    while (*src) *dst++ = *src++;
    src = " (";
    while (*src) *dst++ = *src++;
    
    // Add size
    u32 mb = device->size_mb();
    char num[12];
    int idx = 0;
    if (mb == 0) num[idx++] = '0';
    else {
        char tmp[12];
        int ti = 0;
        while (mb > 0) { tmp[ti++] = '0' + (mb % 10); mb /= 10; }
        while (ti > 0) num[idx++] = tmp[--ti];
    }
    num[idx] = '\0';
    src = num;
    while (*src) *dst++ = *src++;
    src = " MB)";
    while (*src) *dst++ = *src++;
    *dst = '\0';
    
    DBG_SUCCESS("BLK", msg);
    
    return true;
}

bool BlockDeviceManager::unregister_device(BlockDevice* device) {
    for (u32 i = 0; i < device_count; i++) {
        if (devices[i] == device) {
            // Shift remaining devices
            for (u32 j = i; j < device_count - 1; j++) {
                devices[j] = devices[j + 1];
            }
            devices[--device_count] = nullptr;
            
            DBG_DEBUG("BLK", "Device unregistered");
            return true;
        }
    }
    return false;
}

u32 BlockDeviceManager::get_device_count() {
    return device_count;
}

BlockDevice* BlockDeviceManager::get_device(u32 index) {
    if (index >= device_count) return nullptr;
    return devices[index];
}

BlockDevice* BlockDeviceManager::get_device_by_name(const char* name) {
    for (u32 i = 0; i < device_count; i++) {
        if (str::cmp(devices[i]->get_info().name, name) == 0) {
            return devices[i];
        }
    }
    return nullptr;
}

BlockDevice* BlockDeviceManager::find_first_hdd() {
    for (u32 i = 0; i < device_count; i++) {
        DeviceType type = devices[i]->get_info().type;
        if (type == DeviceType::ATA_HDD || 
            type == DeviceType::ATA_SSD ||
            type == DeviceType::AHCI_HDD ||
            type == DeviceType::AHCI_SSD) {
            return devices[i];
        }
    }
    return nullptr;
}

BlockDevice* BlockDeviceManager::find_first_cdrom() {
    for (u32 i = 0; i < device_count; i++) {
        if (devices[i]->get_info().type == DeviceType::ATAPI_CDROM) {
            return devices[i];
        }
    }
    return nullptr;
}

void BlockDeviceManager::print_devices() {
    Console::set_color(Color::Yellow);
    Console::println("=== Block Devices ===");
    Console::set_color(Color::LightGray);
    
    if (device_count == 0) {
        Console::println("  No devices registered");
        return;
    }
    
    for (u32 i = 0; i < device_count; i++) {
        const DeviceInfo& info = devices[i]->get_info();
        
        Console::print("  /dev/");
        Console::set_color(Color::LightCyan);
        Console::print(info.name);
        Console::set_color(Color::LightGray);
        Console::print("  ");
        
        // Type
        switch (info.type) {
            case DeviceType::ATA_HDD: Console::print("[ATA HDD]  "); break;
            case DeviceType::ATA_SSD: Console::print("[ATA SSD]  "); break;
            case DeviceType::ATAPI_CDROM: Console::print("[CD-ROM]   "); break;
            case DeviceType::RAMDisk: Console::print("[RAMDisk]  "); break;
            case DeviceType::Partition: Console::print("[Part]     "); break;
            default: Console::print("[Unknown]  "); break;
        }
        
        // Size
        Console::print_dec(devices[i]->size_mb());
        Console::print(" MB  ");
        
        // Model
        Console::set_color(Color::DarkGray);
        Console::print(info.model);
        Console::set_color(Color::LightGray);
        Console::println("");
    }
}

// ===========================================================================
// Partition Device Implementation
// ===========================================================================

PartitionDevice::PartitionDevice(BlockDevice* parent, u64 start, u64 sectors, u8 part_num)
    : parent_device(parent), start_lba(start)
{
    init_stats();
    
    // Copy parent info and modify
    const DeviceInfo& parent_info = parent->get_info();
    
    info.type = DeviceType::Partition;
    info.state = parent_info.state;
    info.total_sectors = sectors;
    info.sector_size = parent_info.sector_size;
    info.total_bytes = sectors * info.sector_size;
    info.removable = parent_info.removable;
    info.read_only = parent_info.read_only;
    info.supports_lba48 = parent_info.supports_lba48;
    info.supports_dma = parent_info.supports_dma;
    info.device_id = part_num;
    
    // Generate partition name (e.g., hda1, hda2)
    str::cpy(info.name, parent_info.name);
    usize len = str::len(info.name);
    info.name[len] = '1' + part_num;
    info.name[len + 1] = '\0';
    
    // Model string
    str::cpy(info.model, "Partition ");
    char num[4];
    num[0] = '1' + part_num;
    num[1] = '\0';
    str::cat(info.model, num);
    str::cat(info.model, " on ");
    str::cat(info.model, parent_info.name);
}

IOResult PartitionDevice::read_sectors(u64 lba, u32 count, void* buffer) {
    if (!parent_device) return IOResult::DeviceRemoved;
    
    // Bounds check
    if (lba + count > info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    // Translate to parent LBA
    IOResult result = parent_device->read_sectors(start_lba + lba, count, buffer);
    
    if (result == IOResult::Success) {
        stats.sectors_read += count;
    } else {
        stats.read_errors++;
    }
    stats.io_operations++;
    
    return result;
}

IOResult PartitionDevice::write_sectors(u64 lba, u32 count, const void* buffer) {
    if (!parent_device) return IOResult::DeviceRemoved;
    
    // Bounds check
    if (lba + count > info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    // Translate to parent LBA
    IOResult result = parent_device->write_sectors(start_lba + lba, count, buffer);
    
    if (result == IOResult::Success) {
        stats.sectors_written += count;
    } else {
        stats.write_errors++;
    }
    stats.io_operations++;
    
    return result;
}

bool PartitionDevice::is_ready() const {
    return parent_device && parent_device->is_ready();
}

} // namespace bolt::storage
