/* ===========================================================================
 * BOLT OS - ATA/IDE Driver Implementation
 * =========================================================================== */

#include "ata.hpp"
#include "../video/console.hpp"
#include "../serial/serial.hpp"

namespace bolt::drivers {

// Static storage
ATADrive ATA::drives[MAX_DRIVES];
u8 ATA::drive_count = 0;

// Port I/O implementations
void ATA::outb(u16 port, u8 val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

u8 ATA::inb(u16 port) {
    u8 val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void ATA::outw(u16 port, u16 val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

u16 ATA::inw(u16 port) {
    u16 val;
    asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void ATA::insw(u16 port, void* addr, u32 count) {
    asm volatile("rep insw" : "+D"(addr), "+c"(count) : "d"(port) : "memory");
}

void ATA::outsw(u16 port, const void* addr, u32 count) {
    asm volatile("rep outsw" : "+S"(addr), "+c"(count) : "d"(port));
}

u16 ATA::get_io_base(u8 channel) {
    return channel == 0 ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
}

u16 ATA::get_ctrl_base(u8 channel) {
    return channel == 0 ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
}

void ATA::delay_400ns(u8 channel) {
    // Read alternate status 4 times (each ~100ns)
    u16 ctrl = get_ctrl_base(channel);
    for (int i = 0; i < 4; i++) {
        inb(ctrl + ATA_REG_ALTSTATUS);
    }
}

void ATA::soft_reset(u8 channel) {
    u16 ctrl = get_ctrl_base(channel);
    outb(ctrl + ATA_REG_DEVCTRL, 0x04);  // Set SRST
    delay_400ns(channel);
    outb(ctrl + ATA_REG_DEVCTRL, 0x00);  // Clear SRST
    delay_400ns(channel);
    
    // Wait for BSY to clear
    if (!wait_ready(channel, 2000)) {
        DBG_WARN("ATA", channel == 0 ? "Primary channel reset timeout" : "Secondary channel reset timeout");
    }
}

void ATA::select_drive(u8 channel, u8 drive, u32 lba, bool lba_mode) {
    u16 io = get_io_base(channel);
    
    // Drive select with LBA mode
    u8 head = lba_mode ? ((lba >> 24) & 0x0F) : 0;
    u8 select = 0xA0 | (drive << 4) | (lba_mode ? 0x40 : 0) | head;
    
    outb(io + ATA_REG_HDDEVSEL, select);
    delay_400ns(channel);  // Wait 400ns for drive select
}

bool ATA::wait_ready(u8 channel, u32 timeout_ms) {
    u16 io = get_io_base(channel);
    
    // Simple timeout loop (approximate)
    for (u32 i = 0; i < timeout_ms * 1000; i++) {
        u8 status = inb(io + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        // Small delay
        for (volatile int j = 0; j < 100; j++);
    }
    return false;
}

bool ATA::wait_drq(u8 channel, u32 timeout_ms) {
    u16 io = get_io_base(channel);
    
    for (u32 i = 0; i < timeout_ms * 1000; i++) {
        u8 status = inb(io + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return false;
        if (status & ATA_SR_DF) return false;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return true;
        }
        for (volatile int j = 0; j < 100; j++);
    }
    return false;
}

void ATA::init() {
    DBG_LOADING("ATA", "Initializing ATA/IDE driver...");
    drive_count = 0;
    
    // Check all 4 possible drives
    for (u8 channel = 0; channel < 2; channel++) {
        DBG_DEBUG("ATA", channel == 0 ? "Probing primary channel..." : "Probing secondary channel...");
        
        soft_reset(channel);
        
        for (u8 drive = 0; drive < 2; drive++) {
            ATADrive drv;
            if (identify(channel, drive, drv)) {
                if (drive_count < MAX_DRIVES) {
                    drives[drive_count++] = drv;
                    
                    // Build info string
                    char info[80];
                    char* dst = info;
                    const char* src = "Found: ";
                    while (*src) *dst++ = *src++;
                    src = drv.model;
                    while (*src) *dst++ = *src++;
                    src = " (";
                    while (*src) *dst++ = *src++;
                    
                    // Add size
                    u32 mb = drv.size_mb;
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
                    
                    DBG_SUCCESS("ATA", info);
                }
            }
        }
    }
    
    // Summary
    DBG_SUCCESS("ATA", "Initialization complete");
    
    Console::print("[ATA] Found ");
    Console::print_dec(drive_count);
    Console::print(" drive(s)\n");
}

bool ATA::identify(u8 channel, u8 drive_num, ATADrive& out) {
    u16 io = get_io_base(channel);
    
    // Initialize output
    out.present = false;
    out.is_atapi = false;
    out.channel = channel;
    out.drive = drive_num;
    
    // Select drive
    select_drive(channel, drive_num, 0, false);
    
    // Clear sector count and LBA registers
    outb(io + ATA_REG_SECCOUNT, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    
    // Send IDENTIFY command
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    delay_400ns(channel);
    
    // Check if drive exists
    u8 status = inb(io + ATA_REG_STATUS);
    if (status == 0) {
        return false;  // No drive
    }
    
    // Wait for BSY to clear
    if (!wait_ready(channel, 1000)) {
        return false;
    }
    
    // Check for ATAPI
    u8 lba1 = inb(io + ATA_REG_LBA1);
    u8 lba2 = inb(io + ATA_REG_LBA2);
    
    if (lba1 == 0x14 && lba2 == 0xEB) {
        // ATAPI device (CD-ROM)
        out.is_atapi = true;
        outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        delay_400ns(channel);
    } else if (lba1 == 0x69 && lba2 == 0x96) {
        // ATAPI device (alternate signature)
        out.is_atapi = true;
        outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        delay_400ns(channel);
    } else if (lba1 != 0 || lba2 != 0) {
        return false;  // Not ATA
    }
    
    // Wait for DRQ
    if (!wait_drq(channel, 1000)) {
        return false;
    }
    
    // Read identification data
    u16 identify_data[256];
    insw(io + ATA_REG_DATA, identify_data, 256);
    
    // Parse identification data
    out.present = true;
    out.capabilities = identify_data[49];
    out.supports_lba = (identify_data[49] & (1 << 9)) != 0;
    out.supports_lba48 = (identify_data[83] & (1 << 10)) != 0;
    
    // Get size
    if (out.supports_lba48) {
        out.size_sectors = identify_data[100] | 
                          ((u32)identify_data[101] << 16);
    } else if (out.supports_lba) {
        out.size_sectors = identify_data[60] | 
                          ((u32)identify_data[61] << 16);
    } else {
        // CHS addressing - calculate from geometry
        u16 cylinders = identify_data[1];
        u16 heads = identify_data[3];
        u16 sectors = identify_data[6];
        out.size_sectors = cylinders * heads * sectors;
    }
    
    out.size_mb = out.size_sectors / 2048;  // sectors to MB
    
    // Extract model string (words 27-46, byte-swapped)
    for (int i = 0; i < 20; i++) {
        out.model[i * 2] = (identify_data[27 + i] >> 8) & 0xFF;
        out.model[i * 2 + 1] = identify_data[27 + i] & 0xFF;
    }
    out.model[40] = '\0';
    
    // Trim trailing spaces
    for (int i = 39; i >= 0 && out.model[i] == ' '; i--) {
        out.model[i] = '\0';
    }
    
    // Extract serial (words 10-19)
    for (int i = 0; i < 10; i++) {
        out.serial[i * 2] = (identify_data[10 + i] >> 8) & 0xFF;
        out.serial[i * 2 + 1] = identify_data[10 + i] & 0xFF;
    }
    out.serial[20] = '\0';
    
    // Print drive info
    Console::print("  Drive ");
    Console::print_dec(channel * 2 + drive_num);
    Console::print(": ");
    Console::print(out.is_atapi ? "ATAPI " : "ATA ");
    Console::print(out.model);
    Console::print(" (");
    Console::print_dec(out.size_mb);
    Console::print(" MB)\n");
    
    return true;
}

const ATADrive* ATA::get_drive(u8 index) {
    if (index >= drive_count) return nullptr;
    return &drives[index];
}

u8 ATA::get_drive_count() {
    return drive_count;
}

bool ATA::read_sectors(u8 drive_idx, u32 lba, u8 count, void* buffer) {
    if (drive_idx >= drive_count) return false;
    if (count == 0) return false;
    
    const ATADrive& drv = drives[drive_idx];
    u16 io = get_io_base(drv.channel);
    
    if (!drv.supports_lba) {
        Console::print("[ATA] Error: Drive doesn't support LBA\n");
        return false;
    }
    
    // Select drive with LBA
    select_drive(drv.channel, drv.drive, lba, true);
    
    // Wait for drive ready
    if (!wait_ready(drv.channel, 1000)) {
        Console::print("[ATA] Error: Drive not ready\n");
        return false;
    }
    
    // Send read command
    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA0, lba & 0xFF);
    outb(io + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    
    // LBA mode + drive + upper 4 bits of LBA
    outb(io + ATA_REG_HDDEVSEL, 0xE0 | (drv.drive << 4) | ((lba >> 24) & 0x0F));
    
    outb(io + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    // Read sectors
    u16* buf = static_cast<u16*>(buffer);
    for (u8 i = 0; i < count; i++) {
        // Wait for data
        if (!wait_drq(drv.channel, 1000)) {
            Console::print("[ATA] Error: Read timeout\n");
            return false;
        }
        
        // Read 256 words (512 bytes)
        insw(io + ATA_REG_DATA, buf, 256);
        buf += 256;
        
        delay_400ns(drv.channel);
    }
    
    return true;
}

bool ATA::write_sectors(u8 drive_idx, u32 lba, u8 count, const void* buffer) {
    if (drive_idx >= drive_count) return false;
    if (count == 0) return false;
    
    const ATADrive& drv = drives[drive_idx];
    u16 io = get_io_base(drv.channel);
    
    if (!drv.supports_lba) {
        Console::print("[ATA] Error: Drive doesn't support LBA\n");
        return false;
    }
    
    // Select drive with LBA
    select_drive(drv.channel, drv.drive, lba, true);
    
    // Wait for drive ready
    if (!wait_ready(drv.channel, 1000)) {
        Console::print("[ATA] Error: Drive not ready\n");
        return false;
    }
    
    // Send write command
    outb(io + ATA_REG_SECCOUNT, count);
    outb(io + ATA_REG_LBA0, lba & 0xFF);
    outb(io + ATA_REG_LBA1, (lba >> 8) & 0xFF);
    outb(io + ATA_REG_LBA2, (lba >> 16) & 0xFF);
    
    outb(io + ATA_REG_HDDEVSEL, 0xE0 | (drv.drive << 4) | ((lba >> 24) & 0x0F));
    
    outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    // Write sectors
    const u16* buf = static_cast<const u16*>(buffer);
    for (u8 i = 0; i < count; i++) {
        // Wait for DRQ
        if (!wait_drq(drv.channel, 1000)) {
            Console::print("[ATA] Error: Write timeout\n");
            return false;
        }
        
        // Write 256 words (512 bytes)
        outsw(io + ATA_REG_DATA, buf, 256);
        buf += 256;
        
        delay_400ns(drv.channel);
    }
    
    // Flush cache
    outb(io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    wait_ready(drv.channel, 5000);
    
    return true;
}

} // namespace bolt::drivers
