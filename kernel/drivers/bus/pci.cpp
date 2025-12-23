/* ===========================================================================
 * BOLT OS - PCI Bus Driver Implementation
 * =========================================================================== */

#include "pci.hpp"
#include "../video/console.hpp"
#include "../serial/serial.hpp"

using namespace bolt;

// Port I/O
static inline void outl(u16 port, u32 value) {
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

namespace bolt::drivers {

// Static storage
PCIDevice PCI::devices[MAX_DEVICES];
u32 PCI::device_count = 0;

void PCI::init() {
    device_count = 0;
    DBG_LOADING("PCI", "Starting PCI bus enumeration...");
    
    enumerate();
    
    // Log completion
    DBG_SUCCESS("PCI", "Enumeration complete");
    
    // Console output (minimal)
    Console::print("[PCI] Found ");
    Console::print_dec(device_count);
    Console::print(" device(s)\n");
}

void PCI::enumerate() {
    // Scan all buses, slots
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            check_device(static_cast<u8>(bus), slot);
        }
    }
}

void PCI::check_device(u8 bus, u8 slot) {
    u16 vendor = get_vendor_id(bus, slot, 0);
    if (vendor == 0xFFFF) return;  // No device
    
    check_function(bus, slot, 0);
    
    // Check if multi-function device
    u8 header = get_header_type(bus, slot, 0);
    if (header & 0x80) {
        // Multi-function - check other functions
        for (u8 func = 1; func < 8; func++) {
            if (get_vendor_id(bus, slot, func) != 0xFFFF) {
                check_function(bus, slot, func);
            }
        }
    }
}

void PCI::check_function(u8 bus, u8 slot, u8 func) {
    add_device(bus, slot, func);
}

void PCI::add_device(u8 bus, u8 slot, u8 func) {
    if (device_count >= MAX_DEVICES) {
        DBG_WARN("PCI", "Device limit reached, skipping device");
        return;
    }
    
    PCIDevice& dev = devices[device_count];
    dev.bus = bus;
    dev.slot = slot;
    dev.func = func;
    
    // Read device info
    u32 reg0 = config_read(bus, slot, func, 0x00);
    u32 reg2 = config_read(bus, slot, func, 0x08);
    u32 reg3 = config_read(bus, slot, func, 0x0C);
    u32 reg15 = config_read(bus, slot, func, 0x3C);
    
    dev.vendor_id = reg0 & 0xFFFF;
    dev.device_id = (reg0 >> 16) & 0xFFFF;
    dev.revision = reg2 & 0xFF;
    dev.prog_if = (reg2 >> 8) & 0xFF;
    dev.subclass = (reg2 >> 16) & 0xFF;
    dev.class_code = (reg2 >> 24) & 0xFF;
    dev.header_type = (reg3 >> 16) & 0xFF;
    dev.interrupt_line = reg15 & 0xFF;
    dev.interrupt_pin = (reg15 >> 8) & 0xFF;
    
    // Read BARs (only for regular devices, header type 0)
    if ((dev.header_type & 0x7F) == 0x00) {
        for (int i = 0; i < 6; i++) {
            dev.bar[i] = config_read(bus, slot, func, 0x10 + i * 4);
        }
    } else {
        for (int i = 0; i < 6; i++) dev.bar[i] = 0;
    }
    
    // Build device info string
    char info[128];
    info[0] = '\0';
    
    // Format: bus:slot.func Class (Vendor:Device)
    char buf[16];
    
    // Bus
    buf[0] = "0123456789ABCDEF"[(bus >> 4) & 0xF];
    buf[1] = "0123456789ABCDEF"[bus & 0xF];
    buf[2] = ':';
    buf[3] = "0123456789ABCDEF"[(slot >> 4) & 0xF];
    buf[4] = "0123456789ABCDEF"[slot & 0xF];
    buf[5] = '.';
    buf[6] = "0123456789ABCDEF"[func & 0xF];
    buf[7] = ' ';
    buf[8] = '\0';
    
    // Copy to info
    char* dst = info;
    const char* src = buf;
    while (*src) *dst++ = *src++;
    
    // Add class name
    src = pci_class_name(dev.class_code);
    while (*src) *dst++ = *src++;
    *dst = '\0';
    
    DBG_DEBUG("PCI", info);
    
    device_count++;
}

u32 PCI::config_read(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = (1u << 31)                    // Enable bit
                | ((u32)bus << 16)
                | ((u32)slot << 11)
                | ((u32)func << 8)
                | (offset & 0xFC);              // 4-byte aligned
    
    outl(CONFIG_ADDRESS, address);
    return inl(CONFIG_DATA);
}

void PCI::config_write(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = (1u << 31)
                | ((u32)bus << 16)
                | ((u32)slot << 11)
                | ((u32)func << 8)
                | (offset & 0xFC);
    
    outl(CONFIG_ADDRESS, address);
    outl(CONFIG_DATA, value);
}

u16 PCI::get_vendor_id(u8 bus, u8 slot, u8 func) {
    return config_read(bus, slot, func, 0x00) & 0xFFFF;
}

u16 PCI::get_device_id(u8 bus, u8 slot, u8 func) {
    return (config_read(bus, slot, func, 0x00) >> 16) & 0xFFFF;
}

u8 PCI::get_class_code(u8 bus, u8 slot, u8 func) {
    return (config_read(bus, slot, func, 0x08) >> 24) & 0xFF;
}

u8 PCI::get_subclass(u8 bus, u8 slot, u8 func) {
    return (config_read(bus, slot, func, 0x08) >> 16) & 0xFF;
}

u8 PCI::get_header_type(u8 bus, u8 slot, u8 func) {
    return (config_read(bus, slot, func, 0x0C) >> 16) & 0xFF;
}

bool PCI::find_device(u8 class_code, u8 subclass, PCIDevice& out) {
    for (u32 i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && 
            devices[i].subclass == subclass) {
            out = devices[i];
            return true;
        }
    }
    return false;
}

bool PCI::find_device_by_vendor(u16 vendor_id, u16 device_id, PCIDevice& out) {
    for (u32 i = 0; i < device_count; i++) {
        if (devices[i].vendor_id == vendor_id && 
            devices[i].device_id == device_id) {
            out = devices[i];
            return true;
        }
    }
    return false;
}

const PCIDevice* PCI::get_device(u32 index) {
    if (index >= device_count) return nullptr;
    return &devices[index];
}

u32 PCI::get_bar_address(const PCIDevice& dev, u8 bar_index) {
    if (bar_index >= 6) return 0;
    u32 bar = dev.bar[bar_index];
    
    if (bar & 0x01) {
        // I/O space - lower 2 bits are flags
        return bar & 0xFFFFFFFC;
    } else {
        // Memory space - lower 4 bits are flags
        return bar & 0xFFFFFFF0;
    }
}

bool PCI::is_bar_io(const PCIDevice& dev, u8 bar_index) {
    if (bar_index >= 6) return false;
    return (dev.bar[bar_index] & 0x01) != 0;
}

void PCI::enable_bus_mastering(const PCIDevice& dev) {
    u32 command = config_read(dev.bus, dev.slot, dev.func, 0x04);
    command |= (1 << 2);  // Bus Master bit
    command |= (1 << 0);  // I/O Space Enable
    command |= (1 << 1);  // Memory Space Enable
    config_write(dev.bus, dev.slot, dev.func, 0x04, command);
}

// Device class names
const char* pci_class_name(u8 class_code) {
    switch (class_code) {
        case 0x00: return "Unclassified";
        case 0x01: return "Mass Storage";
        case 0x02: return "Network";
        case 0x03: return "Display";
        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06: return "Bridge";
        case 0x07: return "Communication";
        case 0x08: return "System";
        case 0x09: return "Input";
        case 0x0A: return "Docking";
        case 0x0B: return "Processor";
        case 0x0C: return "Serial Bus";
        case 0x0D: return "Wireless";
        default: return "Unknown";
    }
}

const char* pci_subclass_name(u8 class_code, u8 subclass) {
    if (class_code == 0x01) {  // Mass Storage
        switch (subclass) {
            case 0x00: return "SCSI";
            case 0x01: return "IDE";
            case 0x02: return "Floppy";
            case 0x05: return "ATA";
            case 0x06: return "SATA";
            case 0x08: return "NVMe";
            default: return "Other Storage";
        }
    } else if (class_code == 0x02) {  // Network
        switch (subclass) {
            case 0x00: return "Ethernet";
            case 0x80: return "Other Network";
            default: return "Network";
        }
    } else if (class_code == 0x03) {  // Display
        switch (subclass) {
            case 0x00: return "VGA";
            case 0x01: return "XGA";
            case 0x02: return "3D";
            default: return "Other Display";
        }
    } else if (class_code == 0x06) {  // Bridge
        switch (subclass) {
            case 0x00: return "Host";
            case 0x01: return "ISA";
            case 0x04: return "PCI-PCI";
            case 0x80: return "Other Bridge";
            default: return "Bridge";
        }
    } else if (class_code == 0x0C) {  // Serial Bus
        switch (subclass) {
            case 0x00: return "FireWire";
            case 0x03: return "USB";
            case 0x05: return "SMBus";
            default: return "Serial";
        }
    }
    return "Unknown";
}

} // namespace bolt::drivers
