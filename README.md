# Custom OS FLASH

A custom 32-bit operating system built from scratch, featuring a bootloader, kernel, and shell environment.

## Features

- **Custom Bootloader**: Supports both CD and HDD boot modes
- **32-bit Protected Mode Kernel**: Written in C/C++ with assembly for low-level operations
- **Memory Management**: Physical and virtual memory management with heap allocation
- **Device Drivers**:
  - VGA/Framebuffer graphics
  - PS/2 Keyboard and Mouse
  - ATA/IDE storage
  - PIT/RTC timers
  - Serial port communication
  - PCI bus enumeration
- **File Systems**: FAT32 and RAM filesystem support
- **Virtual File System (VFS)**: Unified file system interface
- **Task Scheduler**: Basic multitasking support
- **Interactive Shell**: Command-line interface with various built-in commands

## Project Structure

```
├── boot.asm          # Main bootloader
├── boot_cd.asm       # CD boot support
├── boot_hdd.asm      # HDD boot support
├── kernel/           # Kernel source code
│   ├── core/         # Core subsystems (GDT, IDT, memory, scheduler)
│   ├── drivers/      # Hardware drivers
│   ├── fs/           # File system implementations
│   ├── lib/          # Standard library functions
│   ├── shell/        # Shell and commands
│   └── storage/      # Storage abstraction layer
├── scripts/          # Build and run scripts
├── tools/            # Cross-compiler toolchain (i686-elf-gcc)
└── build/            # Build output directory
```

## Requirements

- Windows with PowerShell
- i686-elf cross-compiler toolchain (included in `tools/`)
- QEMU for emulation (for testing)
- NASM assembler

## Building

Run the build script from PowerShell:

```powershell
.\scripts\build.ps1
```

## Running

Run the OS in QEMU:

```powershell
.\scripts\run.ps1
```

## Architecture

The OS targets the **i686 (x86 32-bit)** architecture and runs in protected mode.

## License

[Add your license here]

## Author

[Your name here]
