#pragma once
/* ===========================================================================
 * BOLT OS - System Configuration
 * ===========================================================================
 * Central place for ALL system-wide constants and configuration.
 * Change these values to adjust system limits and behavior.
 * 
 * Organization:
 *   - System Info
 *   - Memory Layout
 *   - Video Configuration
 *   - Shell Configuration
 *   - Filesystem Limits
 *   - Hardware Constants
 * =========================================================================== */

namespace bolt::config {

// ===========================================================================
// System Information
// ===========================================================================

constexpr const char* VERSION = "0.4";
constexpr const char* NAME = "BOLT OS";
constexpr const char* CODENAME = "BOLT";

// ===========================================================================
// Memory Layout (Physical Addresses)
// ===========================================================================

// Low memory (< 1MB) - used by BIOS, bootloader, etc.
constexpr unsigned long BIOS_DATA_START     = 0x00000;    // BIOS data area
constexpr unsigned long STACK_TOP           = 0x90000;    // Kernel stack
constexpr unsigned long BOOTLOADER_ADDR     = 0x7C00;     // Where BIOS loads bootloader

// High memory (>= 1MB) - kernel and heap
constexpr unsigned long KERNEL_LOAD_ADDR    = 0x100000;   // 1MB - kernel load address
constexpr unsigned long PMM_BITMAP_START    = 0x200000;   // 2MB - PMM bitmap
constexpr unsigned long HEAP_START          = 0x210000;   // 2MB + 64KB - heap after PMM bitmap
constexpr unsigned long HEAP_SIZE           = 0x400000;   // 4MB heap (expandable)
constexpr unsigned long MAX_MEMORY          = 0x10000000; // 256MB max supported

// Page size
constexpr unsigned long PAGE_SIZE           = 0x1000;     // 4KB pages

// ===========================================================================
// Video Configuration
// ===========================================================================

// VGA Text Mode (Mode 3: 80x25)
constexpr unsigned int VGA_TEXT_WIDTH       = 80;
constexpr unsigned int VGA_TEXT_HEIGHT      = 25;
constexpr unsigned long VGA_TEXT_BUFFER     = 0xB8000;
constexpr unsigned int VGA_TEXT_SIZE        = VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT * 2;

// VGA Graphics Mode (Mode 13h: 320x200x256)
constexpr unsigned int VGA_GFX_WIDTH        = 320;
constexpr unsigned int VGA_GFX_HEIGHT       = 200;
constexpr unsigned long VGA_GFX_BUFFER      = 0xA0000;

// VESA/Framebuffer (detected at runtime, these are defaults)
constexpr unsigned int FB_DEFAULT_WIDTH     = 1024;
constexpr unsigned int FB_DEFAULT_HEIGHT    = 768;
constexpr unsigned int FB_DEFAULT_BPP       = 32;

// Console configuration
constexpr unsigned int CONSOLE_SCROLLBACK   = 1000;       // Lines of scrollback
constexpr unsigned int CONSOLE_TAB_WIDTH    = 4;

// ===========================================================================
// Shell Configuration
// ===========================================================================

constexpr unsigned int MAX_CMD_LENGTH       = 256;
constexpr unsigned int MAX_ARGS             = 16;
constexpr unsigned int HISTORY_SIZE         = 20;
constexpr const char* DEFAULT_PROMPT        = "user@bolt:/$";

// ===========================================================================
// Filesystem Limits
// ===========================================================================

// RAMFS limits
constexpr unsigned int RAMFS_MAX_FILES      = 64;
constexpr unsigned int RAMFS_MAX_FILE_SIZE  = 65536;      // 64KB per file
constexpr unsigned int RAMFS_MAX_NAME       = 64;

// FAT32 limits  
constexpr unsigned int FAT32_MAX_PATH       = 260;
constexpr unsigned int FAT32_SECTOR_SIZE    = 512;

// VFS limits
constexpr unsigned int VFS_MAX_MOUNTS       = 8;
constexpr unsigned int VFS_MAX_OPEN_FILES   = 32;
constexpr unsigned int VFS_MAX_PATH         = 256;

// ===========================================================================
// Hardware Constants
// ===========================================================================

// PIC (Programmable Interrupt Controller)
constexpr unsigned int PIC1_COMMAND         = 0x20;
constexpr unsigned int PIC1_DATA            = 0x21;
constexpr unsigned int PIC2_COMMAND         = 0xA0;
constexpr unsigned int PIC2_DATA            = 0xA1;

// PS/2 Controller
constexpr unsigned int PS2_DATA_PORT        = 0x60;
constexpr unsigned int PS2_STATUS_PORT      = 0x64;
constexpr unsigned int PS2_COMMAND_PORT     = 0x64;

// PIT (Programmable Interval Timer)
constexpr unsigned int PIT_CHANNEL0         = 0x40;
constexpr unsigned int PIT_COMMAND          = 0x43;
constexpr unsigned long PIT_FREQUENCY       = 1193182;    // Base frequency
constexpr unsigned int PIT_TARGET_HZ        = 1000;       // 1ms ticks

// Serial ports
constexpr unsigned int COM1_PORT            = 0x3F8;
constexpr unsigned int COM2_PORT            = 0x2F8;
constexpr unsigned int SERIAL_BAUD_RATE     = 115200;

// ATA/IDE
constexpr unsigned int ATA_PRIMARY_IO       = 0x1F0;
constexpr unsigned int ATA_PRIMARY_CTRL     = 0x3F6;
constexpr unsigned int ATA_SECONDARY_IO     = 0x170;
constexpr unsigned int ATA_SECONDARY_CTRL   = 0x376;

// ===========================================================================
// Kernel Limits
// ===========================================================================

constexpr unsigned long MAX_KERNEL_SIZE     = 131072;     // 128KB (256 sectors)
constexpr unsigned int MAX_TASKS            = 32;
constexpr unsigned int MAX_INTERRUPTS       = 256;

// ===========================================================================
// Backward Compatibility Aliases
// ===========================================================================

// Legacy names (deprecated, use new names above)
constexpr unsigned int VGA_WIDTH = VGA_TEXT_WIDTH;
constexpr unsigned int VGA_HEIGHT = VGA_TEXT_HEIGHT;
constexpr unsigned long VGA_BUFFER = VGA_TEXT_BUFFER;

} // namespace bolt::config
