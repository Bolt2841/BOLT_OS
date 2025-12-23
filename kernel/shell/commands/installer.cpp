/* ===========================================================================
 * BOLT OS - Installer Implementation
 * =========================================================================== */

#include "installer.hpp"
#include "../../drivers/video/console.hpp"
#include "../../drivers/video/framebuffer.hpp"
#include "../../drivers/input/keyboard.hpp"
#include "../../drivers/storage/ata.hpp"
#include "../../drivers/serial/serial.hpp"
#include "../../storage/block.hpp"
#include "../../storage/ata_device.hpp"
#include "../../lib/string.hpp"
#include "../../core/memory/heap.hpp"
#include "../../core/sys/io.hpp"

// Bootloader data (outside namespace)
#include "bootloader_data.hpp"

namespace bolt::shell::cmd {

using namespace drivers;
using namespace storage;
using namespace mem;

// ===========================================================================
// Installer Data (embedded bootloader)
// ===========================================================================

// Use the embedded bootloader from the header
using bolt::shell::cmd::hdd_bootloader;

// Get kernel size from boot info
static u32 get_kernel_size() {
    // Check if the value was stored during boot
    // The CD bootloader stores kernel_sectors at a known location
    volatile u8* boot_info = (volatile u8*)0x7C00;
    u32 sectors = boot_info[3];
    if (sectors == 0 || sectors > 200) sectors = 178;  // Default
    return sectors;
}

// ===========================================================================
// Helper Functions
// ===========================================================================

static void print_progress(const char* msg, int percent) {
    Console::print("\r[");
    Console::set_color(Color::LightGreen);
    int bars = percent / 5;
    for (int i = 0; i < 20; i++) {
        if (i < bars) Console::print("=");
        else Console::print(" ");
    }
    Console::set_color(Color::LightGray);
    Console::print("] ");
    Console::print_dec(percent);
    Console::print("% ");
    Console::print(msg);
    Console::print("        ");  // Clear trailing chars
}

static bool confirm(const char* message) {
    Console::set_color(Color::Yellow);
    Console::print(message);
    Console::print(" (y/n): ");
    Console::set_color(Color::LightGray);
    
    while (true) {
        char c = Keyboard::getchar();  // Blocking read
        if (c == 'y' || c == 'Y') {
            Console::println("Yes");
            return true;
        }
        if (c == 'n' || c == 'N' || c == 27) {
            Console::println("No");
            return false;
        }
    }
}

// ===========================================================================
// Create FAT32 Filesystem
// ===========================================================================

static bool create_fat32(BlockDevice* device, u32 partition_start, u32 partition_sectors) {
    u8* buffer = static_cast<u8*>(Heap::alloc(512));
    if (!buffer) return false;
    
    // Clear buffer
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    
    // FAT32 parameters
    u32 bytes_per_sector = 512;
    u32 sectors_per_cluster = 1;
    u32 reserved_sectors = 32;
    u32 fat_count = 2;
    u8 media_type = 0xF8;
    
    // Calculate FAT size
    u32 data_sectors = partition_sectors - reserved_sectors;
    u32 fat_size = (data_sectors / sectors_per_cluster * 4 + bytes_per_sector - 1) / bytes_per_sector;
    data_sectors = partition_sectors - reserved_sectors - (fat_count * fat_size);
    (void)(data_sectors / sectors_per_cluster);  // Total clusters calculated but not needed here
    
    Serial::log("INSTALL", LogType::Debug, "Creating FAT32...");
    
    // === Boot Sector ===
    buffer[0] = 0xEB; buffer[1] = 0x58; buffer[2] = 0x90;  // Jump
    
    // OEM Name
    const char* oem = "BOLTOS  ";
    for (int i = 0; i < 8; i++) buffer[3 + i] = oem[i];
    
    // BPB
    buffer[11] = 0x00; buffer[12] = 0x02;  // Bytes per sector (512)
    buffer[13] = (u8)sectors_per_cluster;
    buffer[14] = reserved_sectors & 0xFF;
    buffer[15] = (reserved_sectors >> 8) & 0xFF;
    buffer[16] = (u8)fat_count;
    buffer[17] = 0; buffer[18] = 0;  // Root entries (0 for FAT32)
    buffer[19] = 0; buffer[20] = 0;  // Total sectors 16
    buffer[21] = media_type;
    buffer[22] = 0; buffer[23] = 0;  // FAT size 16
    buffer[24] = 63; buffer[25] = 0;  // Sectors per track
    buffer[26] = 16; buffer[27] = 0;  // Heads
    
    // Hidden sectors
    buffer[28] = partition_start & 0xFF;
    buffer[29] = (partition_start >> 8) & 0xFF;
    buffer[30] = (partition_start >> 16) & 0xFF;
    buffer[31] = (partition_start >> 24) & 0xFF;
    
    // Total sectors 32
    buffer[32] = partition_sectors & 0xFF;
    buffer[33] = (partition_sectors >> 8) & 0xFF;
    buffer[34] = (partition_sectors >> 16) & 0xFF;
    buffer[35] = (partition_sectors >> 24) & 0xFF;
    
    // FAT32 extended BPB
    buffer[36] = fat_size & 0xFF;
    buffer[37] = (fat_size >> 8) & 0xFF;
    buffer[38] = (fat_size >> 16) & 0xFF;
    buffer[39] = (fat_size >> 24) & 0xFF;
    buffer[40] = 0; buffer[41] = 0;  // Ext flags
    buffer[42] = 0; buffer[43] = 0;  // FS version
    buffer[44] = 2; buffer[45] = 0; buffer[46] = 0; buffer[47] = 0;  // Root cluster
    buffer[48] = 1; buffer[49] = 0;  // FS info sector
    buffer[50] = 6; buffer[51] = 0;  // Backup boot sector
    buffer[64] = 0x80;  // Drive number
    buffer[66] = 0x29;  // Boot signature
    
    // Volume ID (random-ish)
    buffer[67] = 0x12; buffer[68] = 0x34; buffer[69] = 0x56; buffer[70] = 0x78;
    
    // Volume label
    const char* label = "BOLT DRIVE ";
    for (int i = 0; i < 11; i++) buffer[71 + i] = label[i];
    
    // FS type
    const char* fstype = "FAT32   ";
    for (int i = 0; i < 8; i++) buffer[82 + i] = fstype[i];
    
    // Boot signature
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    // Write boot sector
    if (device->write_sectors(partition_start, 1, buffer) != IOResult::Success) {
        Heap::free(buffer);
        return false;
    }
    
    // === FSInfo Sector ===
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    buffer[0] = 0x52; buffer[1] = 0x52; buffer[2] = 0x61; buffer[3] = 0x41;  // Lead sig
    buffer[484] = 0x72; buffer[485] = 0x72; buffer[486] = 0x41; buffer[487] = 0x61;  // Struct sig
    buffer[488] = 0xFF; buffer[489] = 0xFF; buffer[490] = 0xFF; buffer[491] = 0xFF;  // Free count
    buffer[492] = 0x03; buffer[493] = 0x00; buffer[494] = 0x00; buffer[495] = 0x00;  // Next free
    buffer[510] = 0x55; buffer[511] = 0xAA;
    
    if (device->write_sectors(partition_start + 1, 1, buffer) != IOResult::Success) {
        Heap::free(buffer);
        return false;
    }
    
    // === Initialize FAT ===
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    
    // First 3 FAT entries
    buffer[0] = media_type;
    buffer[1] = 0xFF; buffer[2] = 0xFF; buffer[3] = 0x0F;  // Cluster 0
    buffer[4] = 0xFF; buffer[5] = 0xFF; buffer[6] = 0xFF; buffer[7] = 0x0F;  // Cluster 1
    buffer[8] = 0xFF; buffer[9] = 0xFF; buffer[10] = 0xFF; buffer[11] = 0x0F;  // Root dir (cluster 2)
    
    u32 fat1_start = partition_start + reserved_sectors;
    u32 fat2_start = fat1_start + fat_size;
    
    // Write FAT1 first sector
    if (device->write_sectors(fat1_start, 1, buffer) != IOResult::Success) {
        Heap::free(buffer);
        return false;
    }
    
    // Write FAT2 first sector
    if (device->write_sectors(fat2_start, 1, buffer) != IOResult::Success) {
        Heap::free(buffer);
        return false;
    }
    
    // Clear remaining FAT sectors (just first few)
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    for (u32 i = 1; i < 16 && i < fat_size; i++) {
        device->write_sectors(fat1_start + i, 1, buffer);
        device->write_sectors(fat2_start + i, 1, buffer);
    }
    
    // === Root Directory ===
    u32 root_start = fat2_start + fat_size;
    
    // Volume label entry
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    for (int i = 0; i < 11; i++) buffer[i] = label[i];
    buffer[11] = 0x08;  // Volume label attribute
    
    if (device->write_sectors(root_start, 1, buffer) != IOResult::Success) {
        Heap::free(buffer);
        return false;
    }
    
    Heap::free(buffer);
    return true;
}

// ===========================================================================
// Copy Kernel from Memory to Disk
// ===========================================================================

static bool copy_kernel(BlockDevice* device, u32 kernel_sectors) {
    // The kernel was loaded at 0x10000 (physical address) by the bootloader
    // We need to copy it to sectors 1 through kernel_sectors on the target disk
    
    Serial::log("INSTALL", LogType::Info, "Copying kernel to disk...");
    
    u8* buffer = static_cast<u8*>(Heap::alloc(512));
    if (!buffer) return false;
    
    // Source: kernel loaded at 0x10000
    volatile u8* kernel_src = (volatile u8*)0x10000;
    
    for (u32 sector = 0; sector < kernel_sectors; sector++) {
        // Copy 512 bytes to buffer
        for (int i = 0; i < 512; i++) {
            buffer[i] = kernel_src[sector * 512 + i];
        }
        
        // Write to target disk (sector 1 + offset)
        IOResult result = device->write_sectors(1 + sector, 1, buffer);
        if (result != IOResult::Success) {
            Serial::log("INSTALL", LogType::Error, "Failed to write kernel sector");
            Heap::free(buffer);
            return false;
        }
        
        // Update progress every 10 sectors
        if (sector % 10 == 0) {
            int progress = 30 + (sector * 40 / kernel_sectors);
            print_progress("Copying kernel...", progress);
        }
    }
    
    Heap::free(buffer);
    return true;
}

// ===========================================================================
// Write Bootloader
// ===========================================================================

static bool write_bootloader(BlockDevice* device, u32 kernel_sectors) {
    // Write the bootloader to sector 0
    // The HDD bootloader code is stored in memory or embedded
    
    u8* buffer = static_cast<u8*>(Heap::alloc(512));
    if (!buffer) return false;
    
    Serial::log("INSTALL", LogType::Info, "Writing bootloader...");
    
    // Try to read the current boot sector from CD-booted environment
    // The CD bootloader would have been loaded at 0x7C00, but since we're
    // running in protected mode, we need to use embedded bootloader
    
    // Use the embedded HDD bootloader template
    for (int i = 0; i < 512; i++) {
        buffer[i] = (i < (int)sizeof(hdd_bootloader)) ? hdd_bootloader[i] : 0;
    }
    
    // Patch kernel sectors count at offset 3
    buffer[3] = (u8)kernel_sectors;
    
    // Ensure boot signature is present
    buffer[510] = 0x55;
    buffer[511] = 0xAA;
    
    // Write to sector 0
    IOResult result = device->write_sectors(0, 1, buffer);
    
    Heap::free(buffer);
    return result == IOResult::Success;
}

// ===========================================================================
// Install Command
// ===========================================================================

void install() {
    Console::set_color(Color::LightCyan);
    Console::println("========================================");
    Console::println("     BOLT OS Installer");
    Console::println("========================================");
    Console::set_color(Color::LightGray);
    Console::println("");
    
    // List available drives
    Console::println("Detecting drives...");
    Console::println("");
    
    u32 drive_count = ATA::get_drive_count();
    if (drive_count == 0) {
        Console::set_color(Color::LightRed);
        Console::println("No drives detected!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Find installable drives (HDDs, not CD-ROMs)
    u32 hdd_count = 0;
    u8 hdd_indices[4] = {0};
    
    for (u32 i = 0; i < drive_count && i < 4; i++) {
        const ATADrive* info = ATA::get_drive(i);
        if (!info || !info->present) continue;
        if (info->is_atapi) continue;  // Skip CD-ROMs
        
        hdd_indices[hdd_count] = i;
        hdd_count++;
        
        Console::print("  ");
        Console::print_dec(hdd_count);
        Console::print(") ");
        Console::set_color(Color::White);
        Console::print(info->model);
        Console::set_color(Color::LightGray);
        Console::print(" - ");
        Console::print_dec(info->size_mb);
        Console::println(" MB");
    }

    u32 total_sectors[4] = {0};
    for (u32 i = 0; i < hdd_count; i++) {
        const ATADrive* d = ATA::get_drive(hdd_indices[i]);
        if (d) total_sectors[i] = d->size_sectors;
    }
    
    if (hdd_count == 0) {
        Console::set_color(Color::LightRed);
        Console::println("No hard drives found!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    Console::println("");
    
    // Select drive
    Console::print("Select drive (1-");
    Console::print_dec(hdd_count);
    Console::print("): ");
    
    u32 selected = 0;
    while (selected == 0 || selected > hdd_count) {
        char c = Keyboard::getchar();  // Blocking read
        if (c >= '1' && c <= (char)('0' + hdd_count)) {
            selected = c - '0';
            Console::print_dec(selected);
            Console::println("");
        }
        if (c == 27) {  // ESC
            Console::println("Cancelled.");
            return;
        }
    }
    
    u8 drive_idx = hdd_indices[selected - 1];
    const ATADrive* drive = ATA::get_drive(drive_idx);

    Console::println("");
    Console::set_color(Color::LightRed);
    Console::println("WARNING: This will ERASE ALL DATA on the selected drive!");
    Console::set_color(Color::LightGray);
    Console::print("Drive: ");
    if (drive) Console::println(drive->model);
    else Console::println("Unknown");
    Console::println("");
    
    if (!confirm("Are you sure you want to continue?")) {
        Console::println("Installation cancelled.");
        return;
    }
    
    Console::println("");
    Console::println("Starting installation...");
    Console::println("");
    
    // Get block device
    BlockDevice* device = nullptr;
    u32 dev_count = BlockDeviceManager::get_device_count();
    u32 target_sectors = total_sectors[selected - 1];
    
    for (u32 i = 0; i < dev_count; i++) {
        BlockDevice* dev = BlockDeviceManager::get_device(i);
        if (!dev) continue;
        if (dev->get_info().type == DeviceType::ATA_HDD) {
            // Find matching drive by size
            if (dev->get_info().total_sectors == target_sectors) {
                device = dev;
                break;
            }
        }
    }
    
    if (!device) {
        Console::set_color(Color::LightRed);
        Console::println("Failed to get block device!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Calculate layout
    u32 disk_total_sectors = device->get_info().total_sectors;
    u32 reserved_kernel_sectors = 256;  // 128KB for kernel
    u32 partition_start = reserved_kernel_sectors + 1;
    u32 partition_sectors = disk_total_sectors - partition_start;
    
    // Step 1: Write bootloader placeholder
    print_progress("Writing bootloader...", 10);
    u32 kernel_secs = get_kernel_size();
    if (kernel_secs == 0) kernel_secs = 178;  // Default if not available
    
    if (!write_bootloader(device, kernel_secs)) {
        Console::println("");
        Console::set_color(Color::LightRed);
        Console::println("Failed to write bootloader!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Step 2: Copy kernel from memory to disk
    print_progress("Copying kernel...", 30);
    if (!copy_kernel(device, kernel_secs)) {
        Console::println("");
        Console::set_color(Color::LightRed);
        Console::println("Failed to copy kernel!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    // Step 3: Create FAT32 filesystem
    print_progress("Creating filesystem...", 75);
    if (!create_fat32(device, partition_start, partition_sectors)) {
        Console::println("");
        Console::set_color(Color::LightRed);
        Console::println("Failed to create filesystem!");
        Console::set_color(Color::LightGray);
        return;
    }
    
    print_progress("Finalizing...", 95);
    
    print_progress("Complete!", 100);
    Console::println("");
    Console::println("");
    
    Console::set_color(Color::LightGreen);
    Console::println("========================================");
    Console::println("     Installation Complete!");
    Console::println("========================================");
    Console::set_color(Color::LightGray);
    Console::println("");
    Console::println("BOLT OS has been installed successfully!");
    Console::println("Remove the installation media and reboot.");
    Console::println("");
    
    if (confirm("Reboot now?")) {
        bolt::io::outb(0x64, 0xFE);
    }
}

} // namespace bolt::shell::cmd
