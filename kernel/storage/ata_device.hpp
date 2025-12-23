/* ===========================================================================
 * BOLT OS - ATA Block Device Adapter
 * 
 * Wraps the existing ATA driver to implement the BlockDevice interface
 * for use with the VFS and storage subsystem.
 * =========================================================================== */

#ifndef BOLT_STORAGE_ATA_DEVICE_HPP
#define BOLT_STORAGE_ATA_DEVICE_HPP

#include "../core/types.hpp"
#include "block.hpp"
#include "../drivers/storage/ata.hpp"

namespace bolt::storage {

// ===========================================================================
// ATA Block Device
// ===========================================================================

class ATABlockDevice : public BlockDevice {
public:
    // Create from detected ATA drive
    explicit ATABlockDevice(u8 drive_index);
    virtual ~ATABlockDevice() = default;
    
    // BlockDevice interface
    IOResult read_sectors(u64 lba, u32 count, void* buffer) override;
    IOResult write_sectors(u64 lba, u32 count, const void* buffer) override;
    bool is_ready() const override;
    const DeviceInfo& get_info() const override { return info; }
    const DeviceStats& get_stats() const override { return stats; }
    
private:
    u8 ata_drive_index;        // Index in ATA driver's drive array
    const drivers::ATADrive* ata_drive;
};

// ===========================================================================
// ATA Device Manager
// ===========================================================================

class ATADeviceManager {
public:
    // Create BlockDevice wrappers for all detected ATA drives
    // and register them with BlockDeviceManager
    static u32 create_devices();
    
    // Get wrapper for specific ATA drive
    static ATABlockDevice* get_device(u8 index);
    
    // Maximum ATA devices we'll track
    static constexpr u32 MAX_ATA_DEVICES = 4;
    
private:
    static ATABlockDevice* devices[MAX_ATA_DEVICES];
    static u32 device_count;
    static bool initialized;
};

} // namespace bolt::storage

#endif // BOLT_STORAGE_ATA_DEVICE_HPP
