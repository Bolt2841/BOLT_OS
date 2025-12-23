#pragma once
/* ===========================================================================
 * BOLT OS - Block Device Abstraction Layer
 * ===========================================================================
 * Provides a unified interface for all block storage devices.
 * Supports hot-plug, multiple device types, and async I/O (future).
 * =========================================================================== */

#include "../lib/types.hpp"

namespace bolt::storage {

// Forward declarations
class BlockDevice;
class PartitionDevice;

// ===========================================================================
// Block Device Types
// ===========================================================================

enum class DeviceType : u8 {
    Unknown = 0,
    ATA_HDD,        // ATA/IDE Hard Drive
    ATA_SSD,        // ATA/IDE SSD
    ATAPI_CDROM,    // ATAPI CD/DVD-ROM
    AHCI_HDD,       // SATA Hard Drive (future)
    AHCI_SSD,       // SATA SSD (future)
    NVMe,           // NVMe SSD (future)
    USB_Mass,       // USB Mass Storage (future)
    RAMDisk,        // RAM Disk (fallback)
    Floppy,         // Floppy Disk
    Partition       // Partition on another device
};

enum class DeviceState : u8 {
    Uninitialized = 0,
    Initializing,
    Ready,
    Busy,
    Error,
    Removed
};

// I/O Operation Result
enum class IOResult : u8 {
    Success = 0,
    DeviceNotReady,
    DeviceNotFound,
    InvalidParameter,
    OutOfBounds,
    ReadError,
    WriteError,
    WriteProtected,
    Timeout,
    DeviceRemoved,
    NotSupported,
    NoMedia
};

// ===========================================================================
// Block Device Statistics
// ===========================================================================

struct DeviceStats {
    u64 sectors_read;
    u64 sectors_written;
    u64 read_errors;
    u64 write_errors;
    u64 total_io_time_ms;
    u32 io_operations;
};

// ===========================================================================
// Block Device Info
// ===========================================================================

struct DeviceInfo {
    char name[32];              // Device name (e.g., "hda", "sda")
    char model[48];             // Model string
    char serial[24];            // Serial number
    DeviceType type;
    DeviceState state;
    u64 total_sectors;          // Total sectors
    u32 sector_size;            // Bytes per sector (usually 512)
    u64 total_bytes;            // Total capacity in bytes
    bool removable;             // Is device removable?
    bool read_only;             // Is device read-only?
    bool supports_lba48;        // Supports 48-bit LBA?
    bool supports_dma;          // Supports DMA?
    u8 device_id;               // Internal device ID
};

// ===========================================================================
// Block Device Base Class
// ===========================================================================

class BlockDevice {
public:
    virtual ~BlockDevice() = default;
    
    // Core I/O operations
    virtual IOResult read_sectors(u64 lba, u32 count, void* buffer) = 0;
    virtual IOResult write_sectors(u64 lba, u32 count, const void* buffer) = 0;
    
    // Device info
    virtual const DeviceInfo& get_info() const = 0;
    virtual const DeviceStats& get_stats() const = 0;
    
    // Device control
    virtual IOResult flush() { return IOResult::Success; }
    virtual IOResult reset() { return IOResult::NotSupported; }
    virtual bool is_ready() const = 0;
    
    // Convenience methods
    u64 size_bytes() const { return get_info().total_sectors * get_info().sector_size; }
    u64 size_mb() const { return size_bytes() / (1024 * 1024); }
    u32 sector_size() const { return get_info().sector_size; }
    u64 sector_count() const { return get_info().total_sectors; }
    
protected:
    DeviceInfo info;
    DeviceStats stats;
    
    void init_stats() {
        stats.sectors_read = 0;
        stats.sectors_written = 0;
        stats.read_errors = 0;
        stats.write_errors = 0;
        stats.total_io_time_ms = 0;
        stats.io_operations = 0;
    }
};

// ===========================================================================
// Block Device Manager
// ===========================================================================

class BlockDeviceManager {
public:
    static constexpr u32 MAX_DEVICES = 16;
    
    static void init();
    
    // Device registration
    static bool register_device(BlockDevice* device);
    static bool unregister_device(BlockDevice* device);
    
    // Device enumeration
    static u32 get_device_count();
    static BlockDevice* get_device(u32 index);
    static BlockDevice* get_device_by_name(const char* name);
    
    // Find devices by type
    static BlockDevice* find_first_hdd();
    static BlockDevice* find_first_cdrom();
    
    // Debug
    static void print_devices();
    
private:
    static BlockDevice* devices[MAX_DEVICES];
    static u32 device_count;
    static bool initialized;
    
    static void generate_device_name(BlockDevice* dev, char* name);
};

// ===========================================================================
// Partition Device (wrapper around parent device + offset)
// ===========================================================================

class PartitionDevice : public BlockDevice {
public:
    PartitionDevice(BlockDevice* parent, u64 start_lba, u64 sector_count, u8 part_num);
    
    IOResult read_sectors(u64 lba, u32 count, void* buffer) override;
    IOResult write_sectors(u64 lba, u32 count, const void* buffer) override;
    
    const DeviceInfo& get_info() const override { return info; }
    const DeviceStats& get_stats() const override { return stats; }
    
    bool is_ready() const override;
    
    BlockDevice* get_parent() const { return parent_device; }
    u64 get_start_lba() const { return start_lba; }
    
private:
    BlockDevice* parent_device;
    u64 start_lba;
};

// ===========================================================================
// Result helper
// ===========================================================================

inline const char* io_result_string(IOResult result) {
    switch (result) {
        case IOResult::Success: return "Success";
        case IOResult::DeviceNotReady: return "Device not ready";
        case IOResult::InvalidParameter: return "Invalid parameter";
        case IOResult::OutOfBounds: return "Out of bounds";
        case IOResult::ReadError: return "Read error";
        case IOResult::WriteError: return "Write error";
        case IOResult::Timeout: return "Timeout";
        case IOResult::DeviceRemoved: return "Device removed";
        case IOResult::NotSupported: return "Not supported";
        case IOResult::NoMedia: return "No media";
        default: return "Unknown error";
    }
}

} // namespace bolt::storage
