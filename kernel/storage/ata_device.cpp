/* ===========================================================================
 * BOLT OS - ATA Block Device Adapter Implementation
 * =========================================================================== */

#include "ata_device.hpp"
#include "../drivers/serial/serial.hpp"
#include "../lib/string.hpp"

namespace bolt::storage {

using namespace drivers;

// Static storage
ATABlockDevice* ATADeviceManager::devices[MAX_ATA_DEVICES];
u32 ATADeviceManager::device_count = 0;
bool ATADeviceManager::initialized = false;

// ===========================================================================
// ATA Block Device Implementation
// ===========================================================================

ATABlockDevice::ATABlockDevice(u8 drive_index)
    : ata_drive_index(drive_index), ata_drive(nullptr)
{
    init_stats();
    
    // Get the ATA drive info
    ata_drive = ATA::get_drive(drive_index);
    if (!ata_drive) {
        DBG_ERROR("ATA_BLK", "Invalid drive index");
        return;
    }
    
    // Fill in device info
    info.device_id = drive_index;
    info.sector_size = 512;  // ATA always uses 512-byte sectors
    info.total_sectors = ata_drive->size_sectors;
    info.total_bytes = (u64)ata_drive->size_sectors * 512;
    info.removable = ata_drive->is_atapi;
    info.read_only = false;
    info.supports_lba48 = ata_drive->supports_lba48;
    info.supports_dma = false;  // We use PIO mode
    
    // Set device type
    if (ata_drive->is_atapi) {
        info.type = DeviceType::ATAPI_CDROM;
    } else {
        info.type = DeviceType::ATA_HDD;
    }
    
    // Copy model string
    str::cpy(info.model, ata_drive->model);
    
    // Copy serial string
    str::cpy(info.serial, ata_drive->serial);
    
    // Device name will be set by BlockDeviceManager
    info.name[0] = '\0';
    
    // Device is ready if drive is present
    info.state = ata_drive->present ? DeviceState::Ready : DeviceState::Error;
    
    DBG_SUCCESS("ATA_BLK", info.model);
}

IOResult ATABlockDevice::read_sectors(u64 lba, u32 count, void* buffer) {
    if (!ata_drive || !ata_drive->present) {
        return IOResult::DeviceNotFound;
    }
    
    if (!buffer) {
        return IOResult::InvalidParameter;
    }
    
    // Check LBA bounds
    if (lba >= info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    if (lba + count > info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    // For now, we can only read up to 255 sectors at once (u8 count limit)
    // Split larger reads into chunks
    u8* buf = static_cast<u8*>(buffer);
    u64 current_lba = lba;
    u32 remaining = count;
    
    while (remaining > 0) {
        u8 chunk = remaining > 255 ? 255 : static_cast<u8>(remaining);
        
        bool success = ATA::read_sectors(
            ata_drive_index, 
            static_cast<u32>(current_lba),  // TODO: Support LBA48
            chunk, 
            buf
        );
        
        if (!success) {
            stats.read_errors++;
            stats.io_operations++;
            return IOResult::ReadError;
        }
        
        stats.sectors_read += chunk;
        buf += chunk * 512;
        current_lba += chunk;
        remaining -= chunk;
    }
    
    stats.io_operations++;
    return IOResult::Success;
}

IOResult ATABlockDevice::write_sectors(u64 lba, u32 count, const void* buffer) {
    if (!ata_drive || !ata_drive->present) {
        return IOResult::DeviceNotFound;
    }
    
    if (!buffer) {
        return IOResult::InvalidParameter;
    }
    
    // Check LBA bounds
    if (lba >= info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    if (lba + count > info.total_sectors) {
        return IOResult::OutOfBounds;
    }
    
    // ATAPI devices (CD-ROMs) are typically read-only
    if (ata_drive->is_atapi) {
        return IOResult::WriteProtected;
    }
    
    // Split into chunks
    const u8* buf = static_cast<const u8*>(buffer);
    u64 current_lba = lba;
    u32 remaining = count;
    
    while (remaining > 0) {
        u8 chunk = remaining > 255 ? 255 : static_cast<u8>(remaining);
        
        bool success = ATA::write_sectors(
            ata_drive_index,
            static_cast<u32>(current_lba),
            chunk,
            buf
        );
        
        if (!success) {
            stats.write_errors++;
            stats.io_operations++;
            return IOResult::WriteError;
        }
        
        stats.sectors_written += chunk;
        buf += chunk * 512;
        current_lba += chunk;
        remaining -= chunk;
    }
    
    stats.io_operations++;
    return IOResult::Success;
}

bool ATABlockDevice::is_ready() const {
    return ata_drive && ata_drive->present && 
           info.state == DeviceState::Ready;
}

// ===========================================================================
// ATA Device Manager Implementation
// ===========================================================================

u32 ATADeviceManager::create_devices() {
    DBG_LOADING("ATA_BLK", "Creating block devices for ATA drives...");
    
    device_count = 0;
    
    // Clear device array
    for (u32 i = 0; i < MAX_ATA_DEVICES; i++) {
        devices[i] = nullptr;
    }
    
    // Get number of ATA drives detected
    u8 ata_count = ATA::get_drive_count();
    
    DBG_DEBUG("ATA_BLK", ata_count > 0 ? "ATA drives found" : "No ATA drives");
    
    // Create block device for each ATA drive
    for (u8 i = 0; i < ata_count && device_count < MAX_ATA_DEVICES; i++) {
        const drivers::ATADrive* drv = ATA::get_drive(i);
        if (!drv || !drv->present) continue;
        
        // Skip ATAPI devices for now (CD-ROMs need special handling)
        if (drv->is_atapi) {
            DBG_DEBUG("ATA_BLK", "Skipping ATAPI device");
            continue;
        }
        
        // Create wrapper
        ATABlockDevice* dev = new ATABlockDevice(i);
        if (!dev) {
            DBG_WARN("ATA_BLK", "Failed to allocate device");
            continue;
        }
        
        devices[device_count++] = dev;
        
        // Register with block device manager
        if (!BlockDeviceManager::register_device(dev)) {
            DBG_WARN("ATA_BLK", "Failed to register device");
        }
    }
    
    initialized = true;
    
    DBG_SUCCESS("ATA_BLK", device_count > 0 ? "Block devices created" : "No block devices created");
    
    return device_count;
}

ATABlockDevice* ATADeviceManager::get_device(u8 index) {
    if (index >= device_count) return nullptr;
    return devices[index];
}

} // namespace bolt::storage
