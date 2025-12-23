#pragma once
/* ===========================================================================
 * BOLT OS - ATA/IDE Disk Driver
 * Read/write sectors from hard drives
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

// ATA Drive Info
struct ATADrive {
    bool present;
    bool is_atapi;          // CD-ROM vs hard disk
    u8 channel;             // 0 = primary, 1 = secondary
    u8 drive;               // 0 = master, 1 = slave
    u32 size_sectors;       // Total sectors
    u32 size_mb;            // Size in MB
    char model[41];         // Model string
    char serial[21];        // Serial number
    u16 capabilities;
    bool supports_lba48;
    bool supports_lba;
};

class ATA {
public:
    static void init();
    
    // Read sectors using LBA addressing
    static bool read_sectors(u8 drive, u32 lba, u8 count, void* buffer);
    
    // Write sectors using LBA addressing  
    static bool write_sectors(u8 drive, u32 lba, u8 count, const void* buffer);
    
    // Get drive info
    static const ATADrive* get_drive(u8 index);
    static u8 get_drive_count();
    
    // Identify drive (get detailed info)
    static bool identify(u8 channel, u8 drive, ATADrive& out);
    
private:
    // I/O Ports for Primary/Secondary channels
    static constexpr u16 ATA_PRIMARY_IO = 0x1F0;
    static constexpr u16 ATA_PRIMARY_CTRL = 0x3F6;
    static constexpr u16 ATA_SECONDARY_IO = 0x170;
    static constexpr u16 ATA_SECONDARY_CTRL = 0x376;
    
    // Register offsets (from IO base)
    static constexpr u8 ATA_REG_DATA = 0;
    static constexpr u8 ATA_REG_ERROR = 1;
    static constexpr u8 ATA_REG_FEATURES = 1;
    static constexpr u8 ATA_REG_SECCOUNT = 2;
    static constexpr u8 ATA_REG_LBA0 = 3;   // LBA low
    static constexpr u8 ATA_REG_LBA1 = 4;   // LBA mid
    static constexpr u8 ATA_REG_LBA2 = 5;   // LBA high
    static constexpr u8 ATA_REG_HDDEVSEL = 6;
    static constexpr u8 ATA_REG_COMMAND = 7;
    static constexpr u8 ATA_REG_STATUS = 7;
    
    // Control register offsets
    static constexpr u8 ATA_REG_ALTSTATUS = 0;
    static constexpr u8 ATA_REG_DEVCTRL = 0;
    
    // ATA Commands
    static constexpr u8 ATA_CMD_READ_PIO = 0x20;
    static constexpr u8 ATA_CMD_READ_PIO_EXT = 0x24;
    static constexpr u8 ATA_CMD_WRITE_PIO = 0x30;
    static constexpr u8 ATA_CMD_WRITE_PIO_EXT = 0x34;
    static constexpr u8 ATA_CMD_IDENTIFY = 0xEC;
    static constexpr u8 ATA_CMD_IDENTIFY_PACKET = 0xA1;
    static constexpr u8 ATA_CMD_CACHE_FLUSH = 0xE7;
    
    // Status bits
    static constexpr u8 ATA_SR_BSY = 0x80;   // Busy
    static constexpr u8 ATA_SR_DRDY = 0x40;  // Drive ready
    static constexpr u8 ATA_SR_DF = 0x20;    // Drive fault
    static constexpr u8 ATA_SR_DSC = 0x10;   // Drive seek complete
    static constexpr u8 ATA_SR_DRQ = 0x08;   // Data request ready
    static constexpr u8 ATA_SR_CORR = 0x04;  // Corrected data
    static constexpr u8 ATA_SR_IDX = 0x02;   // Index
    static constexpr u8 ATA_SR_ERR = 0x01;   // Error
    
    // Storage for detected drives
    static constexpr u8 MAX_DRIVES = 4;
    static ATADrive drives[MAX_DRIVES];
    static u8 drive_count;
    
    // Helper functions
    static u16 get_io_base(u8 channel);
    static u16 get_ctrl_base(u8 channel);
    static void select_drive(u8 channel, u8 drive, u32 lba = 0, bool lba_mode = true);
    static bool wait_ready(u8 channel, u32 timeout_ms = 1000);
    static bool wait_drq(u8 channel, u32 timeout_ms = 1000);
    static void soft_reset(u8 channel);
    static void delay_400ns(u8 channel);
    
    // Port I/O helpers
    static void outb(u16 port, u8 val);
    static u8 inb(u16 port);
    static void outw(u16 port, u16 val);
    static u16 inw(u16 port);
    static void insw(u16 port, void* addr, u32 count);
    static void outsw(u16 port, const void* addr, u32 count);
};

} // namespace bolt::drivers
