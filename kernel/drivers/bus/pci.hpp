#pragma once
/* ===========================================================================
 * BOLT OS - PCI Bus Driver
 * Enumerate and manage PCI devices
 * =========================================================================== */

#include "../../lib/types.hpp"

namespace bolt::drivers {

// PCI Configuration Space
struct PCIDevice {
    u8 bus;
    u8 slot;
    u8 func;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u8 header_type;
    u32 bar[6];         // Base Address Registers
    u8 interrupt_line;
    u8 interrupt_pin;
};

// Common PCI Class Codes
namespace PCIClass {
    constexpr u8 Unclassified = 0x00;
    constexpr u8 MassStorage = 0x01;
    constexpr u8 Network = 0x02;
    constexpr u8 Display = 0x03;
    constexpr u8 Multimedia = 0x04;
    constexpr u8 Memory = 0x05;
    constexpr u8 Bridge = 0x06;
    constexpr u8 SimpleCommunication = 0x07;
    constexpr u8 BaseSystemPeripheral = 0x08;
    constexpr u8 InputDevice = 0x09;
    constexpr u8 DockingStation = 0x0A;
    constexpr u8 Processor = 0x0B;
    constexpr u8 SerialBus = 0x0C;
    constexpr u8 Wireless = 0x0D;
}

// Mass Storage Subclasses
namespace PCIStorageSubclass {
    constexpr u8 SCSI = 0x00;
    constexpr u8 IDE = 0x01;
    constexpr u8 Floppy = 0x02;
    constexpr u8 IPI = 0x03;
    constexpr u8 RAID = 0x04;
    constexpr u8 ATA = 0x05;
    constexpr u8 SATA = 0x06;
    constexpr u8 SAS = 0x07;
    constexpr u8 NVMe = 0x08;
}

// Network Subclasses
namespace PCINetworkSubclass {
    constexpr u8 Ethernet = 0x00;
    constexpr u8 TokenRing = 0x01;
    constexpr u8 FDDI = 0x02;
    constexpr u8 ATM = 0x03;
    constexpr u8 ISDN = 0x04;
}

class PCI {
public:
    static void init();
    
    // Scan for all devices
    static void enumerate();
    
    // Read/write config space
    static u32 config_read(u8 bus, u8 slot, u8 func, u8 offset);
    static void config_write(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
    
    // Find devices by class/subclass
    static bool find_device(u8 class_code, u8 subclass, PCIDevice& out);
    static bool find_device_by_vendor(u16 vendor_id, u16 device_id, PCIDevice& out);
    
    // Get device info
    static u32 get_device_count() { return device_count; }
    static const PCIDevice* get_device(u32 index);
    
    // Get BAR address (handles memory/IO detection)
    static u32 get_bar_address(const PCIDevice& dev, u8 bar_index);
    static bool is_bar_io(const PCIDevice& dev, u8 bar_index);
    
    // Enable bus mastering for DMA
    static void enable_bus_mastering(const PCIDevice& dev);
    
private:
    static constexpr u16 CONFIG_ADDRESS = 0xCF8;
    static constexpr u16 CONFIG_DATA = 0xCFC;
    static constexpr u32 MAX_DEVICES = 32;
    
    static PCIDevice devices[MAX_DEVICES];
    static u32 device_count;
    
    static void check_device(u8 bus, u8 slot);
    static void check_function(u8 bus, u8 slot, u8 func);
    static void add_device(u8 bus, u8 slot, u8 func);
    
    static u16 get_vendor_id(u8 bus, u8 slot, u8 func);
    static u16 get_device_id(u8 bus, u8 slot, u8 func);
    static u8 get_class_code(u8 bus, u8 slot, u8 func);
    static u8 get_subclass(u8 bus, u8 slot, u8 func);
    static u8 get_header_type(u8 bus, u8 slot, u8 func);
};

// Helper to get readable device names
const char* pci_class_name(u8 class_code);
const char* pci_subclass_name(u8 class_code, u8 subclass);

} // namespace bolt::drivers
