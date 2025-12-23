# ==============================================================================
# BOLT OS - Build Script (Structured Build)
# ==============================================================================

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolsDir = Join-Path $ProjectRoot "tools\bin"
$BuildDir = Join-Path $ProjectRoot "build"
$DriveDir = Join-Path $ProjectRoot "drive"
$KernelDir = Join-Path $ProjectRoot "kernel"

$NASM = "C:\Program Files\NASM\nasm.exe"
$GCC = Join-Path $ToolsDir "i686-elf-gcc.exe"
$GXX = Join-Path $ToolsDir "i686-elf-g++.exe"
$LD = Join-Path $ToolsDir "i686-elf-ld.exe"
$OBJCOPY = Join-Path $ToolsDir "i686-elf-objcopy.exe"

# Compiler flags - using Os for size optimization
$CFLAGS = "-ffreestanding -m32 -Os -fno-pic -fno-stack-protector -mno-red-zone -Wall -Wextra -ffunction-sections -fdata-sections"
$CXXFLAGS = "$CFLAGS -fno-exceptions -fno-rtti -fno-use-cxa-atexit"

# Drive configuration
$DriveImage = Join-Path $DriveDir "harddisk.img"
$DriveSize = 64  # MB

# ==============================================================================
# Helper Functions
# ==============================================================================

function New-DirectoryIfNotExists {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Get-OutputPath {
    param([string]$SourceFile, [string]$Extension)
    # Convert source path to output path maintaining structure
    # e.g., "drivers\video\vga.cpp" -> "build\drivers\video\vga.o"
    $relativePath = $SourceFile -replace "\.cpp$|\.c$|\.asm$", $Extension
    return Join-Path $BuildDir $relativePath
}

function New-FAT32Drive {
    param([string]$ImagePath, [int]$SizeMB)
    
    Write-Host ""
    Write-Host "[DRIVE] Creating $SizeMB MB FAT32 drive image..." -ForegroundColor Yellow
    
    # Create raw disk image
    $sizeBytes = $SizeMB * 1024 * 1024
    $sectors = $sizeBytes / 512
    
    # Create empty file
    $fs = [System.IO.File]::Create($ImagePath)
    $fs.SetLength($sizeBytes)
    $fs.Close()
    
    # Create FAT32 filesystem manually
    # This creates a basic FAT32 structure without MBR (superfloppy format)
    
    $image = New-Object byte[] $sizeBytes
    
    # FAT32 Boot Sector (BPB)
    $bytesPerSector = 512
    $sectorsPerCluster = 1  # 512 byte clusters to ensure FAT32 (need >= 65525 clusters)
    $reservedSectors = 32
    $numFATs = 2
    $mediaType = 0xF8  # Fixed disk
    
    # Calculate FAT size
    $totalSectors = $sectors
    $dataSectors = $totalSectors - $reservedSectors
    $fatSize = [Math]::Ceiling(($dataSectors / $sectorsPerCluster * 4) / $bytesPerSector)
    $dataSectors = $totalSectors - $reservedSectors - ($numFATs * $fatSize)
    $totalClusters = [Math]::Floor($dataSectors / $sectorsPerCluster)
    
    # Jump instruction
    $image[0] = 0xEB; $image[1] = 0x58; $image[2] = 0x90
    
    # OEM Name
    $oemName = [System.Text.Encoding]::ASCII.GetBytes("BOLTOS  ")
    [Array]::Copy($oemName, 0, $image, 3, 8)
    
    # BPB (BIOS Parameter Block)
    # Bytes per sector (512)
    $image[11] = 0x00; $image[12] = 0x02
    # Sectors per cluster
    $image[13] = [byte]$sectorsPerCluster
    # Reserved sectors
    $image[14] = [byte]($reservedSectors -band 0xFF)
    $image[15] = [byte](($reservedSectors -shr 8) -band 0xFF)
    # Number of FATs
    $image[16] = [byte]$numFATs
    # Root entries (0 for FAT32)
    $image[17] = 0x00; $image[18] = 0x00
    # Total sectors 16 (0 for FAT32)
    $image[19] = 0x00; $image[20] = 0x00
    # Media type
    $image[21] = [byte]$mediaType
    # FAT size 16 (0 for FAT32)
    $image[22] = 0x00; $image[23] = 0x00
    # Sectors per track
    $image[24] = 0x3F; $image[25] = 0x00
    # Number of heads
    $image[26] = 0xFF; $image[27] = 0x00
    # Hidden sectors
    $image[28] = 0x00; $image[29] = 0x00; $image[30] = 0x00; $image[31] = 0x00
    # Total sectors 32
    $image[32] = [byte]($totalSectors -band 0xFF)
    $image[33] = [byte](($totalSectors -shr 8) -band 0xFF)
    $image[34] = [byte](($totalSectors -shr 16) -band 0xFF)
    $image[35] = [byte](($totalSectors -shr 24) -band 0xFF)
    
    # FAT32 Extended BPB
    # FAT size 32
    $image[36] = [byte]($fatSize -band 0xFF)
    $image[37] = [byte](($fatSize -shr 8) -band 0xFF)
    $image[38] = [byte](($fatSize -shr 16) -band 0xFF)
    $image[39] = [byte](($fatSize -shr 24) -band 0xFF)
    # Ext flags
    $image[40] = 0x00; $image[41] = 0x00
    # FS version
    $image[42] = 0x00; $image[43] = 0x00
    # Root cluster (usually 2)
    $image[44] = 0x02; $image[45] = 0x00; $image[46] = 0x00; $image[47] = 0x00
    # FS info sector
    $image[48] = 0x01; $image[49] = 0x00
    # Backup boot sector
    $image[50] = 0x06; $image[51] = 0x00
    # Reserved (12 bytes)
    for ($i = 52; $i -lt 64; $i++) { $image[$i] = 0x00 }
    # Drive number
    $image[64] = 0x80
    # Reserved
    $image[65] = 0x00
    # Boot signature
    $image[66] = 0x29
    # Volume ID (random)
    $rand = New-Object System.Random
    $image[67] = [byte]$rand.Next(256)
    $image[68] = [byte]$rand.Next(256)
    $image[69] = [byte]$rand.Next(256)
    $image[70] = [byte]$rand.Next(256)
    # Volume label
    $volLabel = [System.Text.Encoding]::ASCII.GetBytes("BOLT DRIVE ")
    [Array]::Copy($volLabel, 0, $image, 71, 11)
    # FS type
    $fsType = [System.Text.Encoding]::ASCII.GetBytes("FAT32   ")
    [Array]::Copy($fsType, 0, $image, 82, 8)
    
    # Boot sector signature
    $image[510] = 0x55
    $image[511] = 0xAA
    
    # FSInfo sector (sector 1)
    $fsInfoOffset = 512
    # Lead signature
    $image[$fsInfoOffset + 0] = 0x52
    $image[$fsInfoOffset + 1] = 0x52
    $image[$fsInfoOffset + 2] = 0x61
    $image[$fsInfoOffset + 3] = 0x41
    # Reserved (480 bytes)
    # Struct signature
    $image[$fsInfoOffset + 484] = 0x72
    $image[$fsInfoOffset + 485] = 0x72
    $image[$fsInfoOffset + 486] = 0x41
    $image[$fsInfoOffset + 487] = 0x61
    # Free cluster count (unknown = 0xFFFFFFFF)
    $image[$fsInfoOffset + 488] = 0xFF
    $image[$fsInfoOffset + 489] = 0xFF
    $image[$fsInfoOffset + 490] = 0xFF
    $image[$fsInfoOffset + 491] = 0xFF
    # Next free cluster
    $image[$fsInfoOffset + 492] = 0x03
    $image[$fsInfoOffset + 493] = 0x00
    $image[$fsInfoOffset + 494] = 0x00
    $image[$fsInfoOffset + 495] = 0x00
    # Trail signature
    $image[$fsInfoOffset + 510] = 0x55
    $image[$fsInfoOffset + 511] = 0xAA
    
    # Copy boot sector to backup (sector 6)
    [Array]::Copy($image, 0, $image, 6 * 512, 512)
    
    # Initialize FATs
    $fat1Offset = $reservedSectors * 512
    $fat2Offset = $fat1Offset + ($fatSize * 512)
    
    # FAT32 reserved entries
    # Cluster 0: Media type
    $image[$fat1Offset + 0] = [byte]$mediaType
    $image[$fat1Offset + 1] = 0xFF
    $image[$fat1Offset + 2] = 0xFF
    $image[$fat1Offset + 3] = 0x0F
    # Cluster 1: End of chain marker
    $image[$fat1Offset + 4] = 0xFF
    $image[$fat1Offset + 5] = 0xFF
    $image[$fat1Offset + 6] = 0xFF
    $image[$fat1Offset + 7] = 0x0F
    # Cluster 2: Root directory (end of chain)
    $image[$fat1Offset + 8] = 0xFF
    $image[$fat1Offset + 9] = 0xFF
    $image[$fat1Offset + 10] = 0xFF
    $image[$fat1Offset + 11] = 0x0F
    
    # Copy FAT1 to FAT2
    [Array]::Copy($image, $fat1Offset, $image, $fat2Offset, $fatSize * 512)
    
    # Root directory (cluster 2) - add volume label
    $rootOffset = $fat1Offset + ($numFATs * $fatSize * 512)
    
    # Volume label entry
    $labelBytes = [System.Text.Encoding]::ASCII.GetBytes("BOLT DRIVE ")
    [Array]::Copy($labelBytes, 0, $image, $rootOffset, 11)
    $image[$rootOffset + 11] = 0x08  # Volume label attribute
    
    # Write image
    [System.IO.File]::WriteAllBytes($ImagePath, $image)
    
    Write-Host "[DRIVE] FAT32 drive created: $ImagePath" -ForegroundColor Green
    Write-Host "[DRIVE] Size: $SizeMB MB, Clusters: $totalClusters" -ForegroundColor Gray
}

# ==============================================================================
# Build Setup
# ==============================================================================

Write-Host ""
Write-Host "========================================" -ForegroundColor Yellow
Write-Host "    BOLT OS Build System (C++)" -ForegroundColor Yellow
Write-Host "========================================" -ForegroundColor Yellow
Write-Host ""

# Create directory structure
New-DirectoryIfNotExists $BuildDir
New-DirectoryIfNotExists (Join-Path $BuildDir "core\arch")
New-DirectoryIfNotExists (Join-Path $BuildDir "core\memory")
New-DirectoryIfNotExists (Join-Path $BuildDir "core\sched")
New-DirectoryIfNotExists (Join-Path $BuildDir "core\sys")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\video")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\input")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\timer")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\serial")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\bus")
New-DirectoryIfNotExists (Join-Path $BuildDir "drivers\storage")
New-DirectoryIfNotExists (Join-Path $BuildDir "fs")
New-DirectoryIfNotExists (Join-Path $BuildDir "storage")
New-DirectoryIfNotExists (Join-Path $BuildDir "lib")
New-DirectoryIfNotExists (Join-Path $BuildDir "shell\commands")
New-DirectoryIfNotExists $DriveDir

# ==============================================================================
# Check/Create FAT32 Drive
# ==============================================================================

if (-not (Test-Path $DriveImage)) {
    New-FAT32Drive -ImagePath $DriveImage -SizeMB $DriveSize
} else {
    Write-Host "[DRIVE] Using existing drive: $DriveImage" -ForegroundColor DarkGray
}

# ==============================================================================
# Compile Bootloader
# ==============================================================================

Write-Host ""
Write-Host "[ASM ] boot.asm" -ForegroundColor Cyan
& $NASM -f bin "$ProjectRoot\boot.asm" -o "$BuildDir\boot.bin"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Bootloader failed" -ForegroundColor Red; exit 1 }

# Compile HDD bootloader early so we can embed it
Write-Host "[ASM ] boot_hdd.asm" -ForegroundColor Cyan
& $NASM -f bin "$ProjectRoot\boot_hdd.asm" -o "$BuildDir\boot_hdd.bin"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] HDD boot assembly failed" -ForegroundColor Red; exit 1 }

# Generate bootloader_data.hpp from boot_hdd.bin
Write-Host "[GEN ] bootloader_data.hpp" -ForegroundColor Cyan
$bootHddBytes = [System.IO.File]::ReadAllBytes("$BuildDir\boot_hdd.bin")

$headerContent = @"
/* ===========================================================================
 * BOLT OS - Bootloader Data Header
 * ===========================================================================
 * AUTO-GENERATED by build.ps1 from boot_hdd.bin
 * DO NOT EDIT MANUALLY
 * =========================================================================== */

#pragma once

#include "../../core/types.hpp"

namespace bolt::shell::cmd {

// HDD bootloader binary (512 bytes)
// Generated from boot_hdd.asm
static const u8 hdd_bootloader[512] = {
"@

$hexLines = @()
for ($i = 0; $i -lt 512; $i += 16) {
    $lineBytes = @()
    for ($j = 0; $j -lt 16 -and ($i + $j) -lt 512; $j++) {
        if (($i + $j) -lt $bootHddBytes.Length) {
            $lineBytes += "0x{0:X2}" -f $bootHddBytes[$i + $j]
        } else {
            $lineBytes += "0x00"
        }
    }
    $hexLines += "    " + ($lineBytes -join ", ") + ","
}

$headerContent += ($hexLines -join "`n")
$headerContent = $headerContent.TrimEnd(",")

$headerContent += @"

};

} // namespace bolt::shell::cmd
"@

$headerPath = Join-Path $KernelDir "shell\commands\bootloader_data.hpp"
$headerContent | Out-File -FilePath $headerPath -Encoding UTF8
Write-Host "[INFO] Generated bootloader header with $($bootHddBytes.Length) bytes" -ForegroundColor Gray

# ==============================================================================
# Compile Assembly Files
# ==============================================================================

$asmFiles = @(
    "core\arch\isr.asm",
    "core\arch\gdt.asm",
    "core\sched\context.asm"
)

foreach ($file in $asmFiles) {
    $src = Join-Path $KernelDir $file
    $obj = Get-OutputPath $file "_asm.o"
    
    New-DirectoryIfNotExists (Split-Path $obj -Parent)
    
    Write-Host "[ASM ] kernel/$file" -ForegroundColor Cyan
    & $NASM -f elf32 $src -o $obj
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] $file failed" -ForegroundColor Red; exit 1 }
}

# ==============================================================================
# Compile C Entry
# ==============================================================================

Write-Host "[CC  ] kernel/entry.c" -ForegroundColor Cyan
& $GCC $CFLAGS.Split(" ") -c "$KernelDir\entry.c" -o "$BuildDir\entry.o"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] entry.c failed" -ForegroundColor Red; exit 1 }

# ==============================================================================
# Compile C++ Files
# ==============================================================================

$cppFiles = @(
    # Core - Memory
    "core\memory\heap.cpp",
    "core\memory\pmm.cpp",
    "core\memory\vmm.cpp",
    # Core - Scheduler
    "core\sched\task.cpp",
    # Core - Architecture
    "core\arch\gdt.cpp",
    "core\arch\idt.cpp",
    # Core - System
    "core\sys\events.cpp",
    "core\sys\log.cpp",
    "core\sys\panic.cpp",
    "core\sys\system.cpp",
    # Library
    "lib\string.cpp",
    # Drivers - Video
    "drivers\video\vga.cpp",
    "drivers\video\framebuffer.cpp",
    "drivers\video\console.cpp",
    "drivers\video\graphics.cpp",
    "drivers\video\font.cpp",
    "drivers\video\font8x16.cpp",
    # Drivers - Input
    "drivers\input\keyboard.cpp",
    "drivers\input\mouse.cpp",
    # Drivers - Timer
    "drivers\timer\pit.cpp",
    "drivers\timer\rtc.cpp",
    # Drivers - Serial
    "drivers\serial\serial.cpp",
    # Drivers - Bus
    "drivers\bus\pci.cpp",
    # Drivers - Storage
    "drivers\storage\ata.cpp",
    # Filesystem (legacy)
    "fs\ramfs.cpp",
    "fs\fat32.cpp",
    # Storage Subsystem (new)
    "storage\block.cpp",
    "storage\partition.cpp",
    "storage\vfs.cpp",
    "storage\detect.cpp",
    "storage\ramfs.cpp",
    "storage\fat32fs.cpp",
    "storage\ata_device.cpp",
    "storage\storage.cpp",
    # Shell
    "shell\shell.cpp",
    "shell\commands\filesystem.cpp",
    "shell\commands\system.cpp",
    "shell\commands\misc.cpp",
    "shell\commands\installer.cpp",
    # Kernel Main
    "kernel.cpp"
)

foreach ($file in $cppFiles) {
    $src = Join-Path $KernelDir $file
    $obj = Get-OutputPath $file ".o"
    
    New-DirectoryIfNotExists (Split-Path $obj -Parent)
    
    Write-Host "[CXX ] kernel/$file" -ForegroundColor Cyan
    & $GXX $CXXFLAGS.Split(" ") -I"$KernelDir" -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] $file failed" -ForegroundColor Red; exit 1 }
}

# ==============================================================================
# Gather Object Files
# ==============================================================================

$objects = @(
    "$BuildDir\entry.o"
)

# Add assembly objects
foreach ($file in $asmFiles) {
    $objects += Get-OutputPath $file "_asm.o"
}

# Add C++ objects
foreach ($file in $cppFiles) {
    $objects += Get-OutputPath $file ".o"
}

# ==============================================================================
# Link Kernel
# ==============================================================================

Write-Host "[LINK] kernel.elf" -ForegroundColor Cyan
& $LD -m elf_i386 --gc-sections -T "$KernelDir\linker.ld" -o "$BuildDir\kernel.elf" $objects
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] Link failed" -ForegroundColor Red; exit 1 }

# Extract binary
& $OBJCOPY -O binary "$BuildDir\kernel.elf" "$BuildDir\kernel.bin"

# ==============================================================================
# Create Disk Image (Floppy - Legacy)
# ==============================================================================

$kernelSize = (Get-Item "$BuildDir\kernel.bin").Length
$sectorsNeeded = [Math]::Ceiling($kernelSize / 512)
if ($sectorsNeeded -lt 1) { $sectorsNeeded = 1 }
if ($sectorsNeeded -gt 200) { 
    Write-Host "[ERROR] Kernel too large ($kernelSize bytes, max ~100KB)" -ForegroundColor Red
    exit 1
}
Write-Host "[INFO] Kernel size: $kernelSize bytes ($sectorsNeeded sectors)" -ForegroundColor Gray

Write-Host "[IMG ] bolt_os.img (floppy)" -ForegroundColor Cyan

$bootBin = [System.IO.File]::ReadAllBytes("$BuildDir\boot.bin")
$kernelBin = [System.IO.File]::ReadAllBytes("$BuildDir\kernel.bin")

# Patch the kernel sector count at offset 3 in boot sector
$bootBin[3] = [byte]$sectorsNeeded
Write-Host "[INFO] Patched boot sector: $sectorsNeeded sectors" -ForegroundColor Gray

# Create 1.44MB floppy image
$imageSize = 1474560
$image = New-Object byte[] $imageSize

# Copy boot sector (with patched sector count)
[Array]::Copy($bootBin, 0, $image, 0, [Math]::Min($bootBin.Length, 512))

# Copy kernel starting at sector 2 (offset 512)
[Array]::Copy($kernelBin, 0, $image, 512, $kernelBin.Length)

[System.IO.File]::WriteAllBytes("$BuildDir\bolt_os.img", $image)

# ==============================================================================
# Create Bootable Hard Disk Image (Persistent)
# ==============================================================================

Write-Host ""
Write-Host "[IMG ] Creating bootable hard disk..." -ForegroundColor Yellow

# Use already-compiled HDD bootloader
$bootHddBin = [System.IO.File]::ReadAllBytes("$BuildDir\boot_hdd.bin")

# Patch kernel sectors in HDD boot
$bootHddBin[3] = [byte]$sectorsNeeded

# Calculate layout:
# - Sector 0: Boot sector
# - Sectors 1-N: Kernel (N = sectorsNeeded)
# - Sector N+1 onwards: FAT32 filesystem
# We'll reserve 256 sectors (128KB) for kernel to allow growth

$reservedKernelSectors = 256

# Total disk size: 64MB
$diskSizeMB = 64
$diskSizeBytes = $diskSizeMB * 1024 * 1024
$totalSectors = $diskSizeBytes / 512

Write-Host "[INFO] Disk layout:" -ForegroundColor Gray
Write-Host "       Sector 0: Boot sector" -ForegroundColor DarkGray
Write-Host "       Sectors 1-${reservedKernelSectors}: Kernel (${sectorsNeeded} used)" -ForegroundColor DarkGray
Write-Host "       Sectors $($reservedKernelSectors + 1)+: FAT32 filesystem" -ForegroundColor DarkGray

# Create the bootable HDD image
$hddImage = New-Object byte[] $diskSizeBytes

# Copy HDD boot sector
[Array]::Copy($bootHddBin, 0, $hddImage, 0, [Math]::Min($bootHddBin.Length, 512))

# Copy kernel starting at sector 1
[Array]::Copy($kernelBin, 0, $hddImage, 512, $kernelBin.Length)

# Create FAT32 filesystem starting at the reserved offset
# Adjust FAT32 parameters for the remaining space
$fat32StartSector = $reservedKernelSectors + 1
$fat32StartOffset = $fat32StartSector * 512
$fat32Sectors = $totalSectors - $fat32StartSector

$bytesPerSector = 512
$sectorsPerCluster = 1
$reservedSectors = 32
$numFATs = 2
$mediaType = 0xF8

# Calculate FAT size for the filesystem portion
$dataSectors = $fat32Sectors - $reservedSectors
$fatSize = [Math]::Ceiling(($dataSectors / $sectorsPerCluster * 4) / $bytesPerSector)
$dataSectors = $fat32Sectors - $reservedSectors - ($numFATs * $fatSize)
$totalClusters = [Math]::Floor($dataSectors / $sectorsPerCluster)

Write-Host "[INFO] FAT32: $fat32Sectors sectors, $totalClusters clusters" -ForegroundColor DarkGray

# FAT32 Boot Sector at fat32StartOffset
$fs = $fat32StartOffset

# Jump instruction
$hddImage[$fs + 0] = 0xEB; $hddImage[$fs + 1] = 0x58; $hddImage[$fs + 2] = 0x90

# OEM Name
$oemName = [System.Text.Encoding]::ASCII.GetBytes("BOLTOS  ")
[Array]::Copy($oemName, 0, $hddImage, $fs + 3, 8)

# BPB
$hddImage[$fs + 11] = 0x00; $hddImage[$fs + 12] = 0x02  # Bytes per sector
$hddImage[$fs + 13] = [byte]$sectorsPerCluster
$hddImage[$fs + 14] = [byte]($reservedSectors -band 0xFF)
$hddImage[$fs + 15] = [byte](($reservedSectors -shr 8) -band 0xFF)
$hddImage[$fs + 16] = [byte]$numFATs
$hddImage[$fs + 17] = 0x00; $hddImage[$fs + 18] = 0x00  # Root entries (0 for FAT32)
$hddImage[$fs + 19] = 0x00; $hddImage[$fs + 20] = 0x00  # Total sectors 16
$hddImage[$fs + 21] = [byte]$mediaType
$hddImage[$fs + 22] = 0x00; $hddImage[$fs + 23] = 0x00  # FAT size 16
$hddImage[$fs + 24] = 0x3F; $hddImage[$fs + 25] = 0x00  # Sectors per track
$hddImage[$fs + 26] = 0x10; $hddImage[$fs + 27] = 0x00  # Number of heads (16)

# Hidden sectors = sectors before FAT32 partition
$hddImage[$fs + 28] = [byte]($fat32StartSector -band 0xFF)
$hddImage[$fs + 29] = [byte](($fat32StartSector -shr 8) -band 0xFF)
$hddImage[$fs + 30] = [byte](($fat32StartSector -shr 16) -band 0xFF)
$hddImage[$fs + 31] = [byte](($fat32StartSector -shr 24) -band 0xFF)

# Total sectors 32
$hddImage[$fs + 32] = [byte]($fat32Sectors -band 0xFF)
$hddImage[$fs + 33] = [byte](($fat32Sectors -shr 8) -band 0xFF)
$hddImage[$fs + 34] = [byte](($fat32Sectors -shr 16) -band 0xFF)
$hddImage[$fs + 35] = [byte](($fat32Sectors -shr 24) -band 0xFF)

# FAT32 Extended BPB
$hddImage[$fs + 36] = [byte]($fatSize -band 0xFF)
$hddImage[$fs + 37] = [byte](($fatSize -shr 8) -band 0xFF)
$hddImage[$fs + 38] = [byte](($fatSize -shr 16) -band 0xFF)
$hddImage[$fs + 39] = [byte](($fatSize -shr 24) -band 0xFF)
$hddImage[$fs + 40] = 0x00; $hddImage[$fs + 41] = 0x00  # Ext flags
$hddImage[$fs + 42] = 0x00; $hddImage[$fs + 43] = 0x00  # FS version
$hddImage[$fs + 44] = 0x02; $hddImage[$fs + 45] = 0x00; $hddImage[$fs + 46] = 0x00; $hddImage[$fs + 47] = 0x00  # Root cluster
$hddImage[$fs + 48] = 0x01; $hddImage[$fs + 49] = 0x00  # FS info sector
$hddImage[$fs + 50] = 0x06; $hddImage[$fs + 51] = 0x00  # Backup boot sector
$hddImage[$fs + 64] = 0x80  # Drive number (HDD)
$hddImage[$fs + 66] = 0x29  # Boot signature

# Volume ID
$rand = New-Object System.Random
$hddImage[$fs + 67] = [byte]$rand.Next(256)
$hddImage[$fs + 68] = [byte]$rand.Next(256)
$hddImage[$fs + 69] = [byte]$rand.Next(256)
$hddImage[$fs + 70] = [byte]$rand.Next(256)

# Volume label
$volLabel = [System.Text.Encoding]::ASCII.GetBytes("BOLT DRIVE ")
[Array]::Copy($volLabel, 0, $hddImage, $fs + 71, 11)

# FS type
$fsType = [System.Text.Encoding]::ASCII.GetBytes("FAT32   ")
[Array]::Copy($fsType, 0, $hddImage, $fs + 82, 8)

# Boot sector signature
$hddImage[$fs + 510] = 0x55
$hddImage[$fs + 511] = 0xAA

# FSInfo sector
$fsInfoOffset = $fs + 512
$hddImage[$fsInfoOffset + 0] = 0x52; $hddImage[$fsInfoOffset + 1] = 0x52
$hddImage[$fsInfoOffset + 2] = 0x61; $hddImage[$fsInfoOffset + 3] = 0x41
$hddImage[$fsInfoOffset + 484] = 0x72; $hddImage[$fsInfoOffset + 485] = 0x72
$hddImage[$fsInfoOffset + 486] = 0x41; $hddImage[$fsInfoOffset + 487] = 0x61
$hddImage[$fsInfoOffset + 488] = 0xFF; $hddImage[$fsInfoOffset + 489] = 0xFF
$hddImage[$fsInfoOffset + 490] = 0xFF; $hddImage[$fsInfoOffset + 491] = 0xFF
$hddImage[$fsInfoOffset + 492] = 0x03; $hddImage[$fsInfoOffset + 493] = 0x00
$hddImage[$fsInfoOffset + 494] = 0x00; $hddImage[$fsInfoOffset + 495] = 0x00
$hddImage[$fsInfoOffset + 510] = 0x55; $hddImage[$fsInfoOffset + 511] = 0xAA

# Backup boot sector at sector 6
[Array]::Copy($hddImage, $fs, $hddImage, $fs + (6 * 512), 512)

# Initialize FATs
$fat1Offset = $fs + ($reservedSectors * 512)
$fat2Offset = $fat1Offset + ($fatSize * 512)

$hddImage[$fat1Offset + 0] = [byte]$mediaType
$hddImage[$fat1Offset + 1] = 0xFF; $hddImage[$fat1Offset + 2] = 0xFF; $hddImage[$fat1Offset + 3] = 0x0F
$hddImage[$fat1Offset + 4] = 0xFF; $hddImage[$fat1Offset + 5] = 0xFF; $hddImage[$fat1Offset + 6] = 0xFF; $hddImage[$fat1Offset + 7] = 0x0F
$hddImage[$fat1Offset + 8] = 0xFF; $hddImage[$fat1Offset + 9] = 0xFF; $hddImage[$fat1Offset + 10] = 0xFF; $hddImage[$fat1Offset + 11] = 0x0F

# Copy FAT1 to FAT2
[Array]::Copy($hddImage, $fat1Offset, $hddImage, $fat2Offset, $fatSize * 512)

# Root directory with volume label
$rootOffset = $fat1Offset + ($numFATs * $fatSize * 512)
$labelBytes = [System.Text.Encoding]::ASCII.GetBytes("BOLT DRIVE ")
[Array]::Copy($labelBytes, 0, $hddImage, $rootOffset, 11)
$hddImage[$rootOffset + 11] = 0x08  # Volume label attribute

# Write the bootable HDD image
$bootableHddPath = Join-Path $DriveDir "harddisk.img"
[System.IO.File]::WriteAllBytes($bootableHddPath, $hddImage)

Write-Host "[INFO] Created bootable HDD: $bootableHddPath" -ForegroundColor Green

# ==============================================================================
# Create Bootable ISO (El Torito)
# ==============================================================================

Write-Host ""
Write-Host "[ISO ] Creating bootable ISO image..." -ForegroundColor Yellow

# Assemble CD bootloader
Write-Host "[ASM ] boot_cd.asm" -ForegroundColor Cyan
& $NASM -f bin "$ProjectRoot\boot_cd.asm" -o "$BuildDir\boot_cd.bin"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] CD boot assembly failed" -ForegroundColor Red; exit 1 }

$bootCdBin = [System.IO.File]::ReadAllBytes("$BuildDir\boot_cd.bin")
# Patch kernel sectors
$bootCdBin[3] = [byte]$sectorsNeeded

# Create ISO 9660 image with El Torito boot
# ISO structure:
# - Sector 0-15: System Area (zeroed)
# - Sector 16: Primary Volume Descriptor
# - Sector 17: Boot Record Volume Descriptor (El Torito)
# - Sector 18: Volume Descriptor Set Terminator
# - Sector 19: Boot Catalog
# - Sector 20: Boot Image (our CD bootloader - 2048 bytes)
# - Sector 21+: Kernel data

$isoSectorSize = 2048

# Calculate sectors needed for kernel (in 2048-byte sectors)
$kernelCdSectors = [Math]::Ceiling($kernelBin.Length / $isoSectorSize)
$totalIsoSectors = 22 + $kernelCdSectors  # 0-21 + kernel sectors
$isoSize = $totalIsoSectors * $isoSectorSize

$isoImage = New-Object byte[] $isoSize

# Helper function to write string padded to length
function Write-IsoString {
    param($array, $offset, $str, $len, $padChar = 0x20)
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($str)
    for ($i = 0; $i -lt $len; $i++) {
        if ($i -lt $bytes.Length) {
            $array[$offset + $i] = $bytes[$i]
        } else {
            $array[$offset + $i] = $padChar
        }
    }
}

# Helper to write both-endian 32-bit value
function Write-IsoBoth32 {
    param($array, $offset, $value)
    # Little-endian
    $array[$offset + 0] = [byte]($value -band 0xFF)
    $array[$offset + 1] = [byte](($value -shr 8) -band 0xFF)
    $array[$offset + 2] = [byte](($value -shr 16) -band 0xFF)
    $array[$offset + 3] = [byte](($value -shr 24) -band 0xFF)
    # Big-endian
    $array[$offset + 4] = [byte](($value -shr 24) -band 0xFF)
    $array[$offset + 5] = [byte](($value -shr 16) -band 0xFF)
    $array[$offset + 6] = [byte](($value -shr 8) -band 0xFF)
    $array[$offset + 7] = [byte]($value -band 0xFF)
}

# Helper to write both-endian 16-bit value
function Write-IsoBoth16 {
    param($array, $offset, $value)
    # Little-endian
    $array[$offset + 0] = [byte]($value -band 0xFF)
    $array[$offset + 1] = [byte](($value -shr 8) -band 0xFF)
    # Big-endian
    $array[$offset + 2] = [byte](($value -shr 8) -band 0xFF)
    $array[$offset + 3] = [byte]($value -band 0xFF)
}

# ===== Primary Volume Descriptor (Sector 16) =====
$pvdOffset = 16 * $isoSectorSize
$isoImage[$pvdOffset + 0] = 0x01  # Type: Primary Volume Descriptor
Write-IsoString $isoImage ($pvdOffset + 1) "CD001" 5 0x00  # Standard Identifier
$isoImage[$pvdOffset + 6] = 0x01  # Version
$isoImage[$pvdOffset + 7] = 0x00  # Unused
# System Identifier (32 a-characters)
Write-IsoString $isoImage ($pvdOffset + 8) "BOLT OS" 32
# Volume Identifier (32 d-characters)
Write-IsoString $isoImage ($pvdOffset + 40) "BOLT_OS_INSTALL" 32
# Unused (8 bytes at 72-79)
# Volume Space Size (total sectors) at offset 80-87
Write-IsoBoth32 $isoImage ($pvdOffset + 80) $totalIsoSectors
# Unused (32 bytes at 88-119)
# Volume Set Size at 120-123
Write-IsoBoth16 $isoImage ($pvdOffset + 120) 1
# Volume Sequence Number at 124-127
Write-IsoBoth16 $isoImage ($pvdOffset + 124) 1
# Logical Block Size at 128-131
Write-IsoBoth16 $isoImage ($pvdOffset + 128) $isoSectorSize
# Path Table Size at 132-139
Write-IsoBoth32 $isoImage ($pvdOffset + 132) 10
# Type L Path Table Location at 140-143
$isoImage[$pvdOffset + 140] = 0x13  # Sector 19 placeholder
# Optional Type L Path Table at 144-147
# Type M Path Table Location at 148-151
$isoImage[$pvdOffset + 151] = 0x13
# Optional Type M Path Table at 152-155

# Root Directory Record at offset 156 (34 bytes)
$rootDirOffset = $pvdOffset + 156
$isoImage[$rootDirOffset + 0] = 34  # Directory record length
$isoImage[$rootDirOffset + 1] = 0   # Extended attribute length
# Location of extent - both endian
$rootLoc = 21  # Root directory at sector 21
$isoImage[$rootDirOffset + 2] = [byte]($rootLoc -band 0xFF)
$isoImage[$rootDirOffset + 3] = [byte](($rootLoc -shr 8) -band 0xFF)
$isoImage[$rootDirOffset + 4] = [byte](($rootLoc -shr 16) -band 0xFF)
$isoImage[$rootDirOffset + 5] = [byte](($rootLoc -shr 24) -band 0xFF)
$isoImage[$rootDirOffset + 6] = [byte](($rootLoc -shr 24) -band 0xFF)
$isoImage[$rootDirOffset + 7] = [byte](($rootLoc -shr 16) -band 0xFF)
$isoImage[$rootDirOffset + 8] = [byte](($rootLoc -shr 8) -band 0xFF)
$isoImage[$rootDirOffset + 9] = [byte]($rootLoc -band 0xFF)
# Data length - both endian
Write-IsoBoth32 $isoImage ($rootDirOffset + 10) $isoSectorSize
# Recording date/time (7 bytes)
$isoImage[$rootDirOffset + 18] = 125  # Year since 1900 (2025)
$isoImage[$rootDirOffset + 19] = 12   # Month
$isoImage[$rootDirOffset + 20] = 23   # Day
$isoImage[$rootDirOffset + 21] = 0    # Hour
$isoImage[$rootDirOffset + 22] = 0    # Minute
$isoImage[$rootDirOffset + 23] = 0    # Second
$isoImage[$rootDirOffset + 24] = 0    # GMT offset
# Flags
$isoImage[$rootDirOffset + 25] = 0x02  # Directory
$isoImage[$rootDirOffset + 26] = 0     # File unit size
$isoImage[$rootDirOffset + 27] = 0     # Interleave gap
# Volume sequence number
Write-IsoBoth16 $isoImage ($rootDirOffset + 28) 1
# File identifier length
$isoImage[$rootDirOffset + 32] = 1
# File identifier (root = 0x00)
$isoImage[$rootDirOffset + 33] = 0x00

# Volume Set Identifier at 190 (128 bytes)
Write-IsoString $isoImage ($pvdOffset + 190) "BOLT_OS" 128
# Publisher at 318 (128 bytes)
Write-IsoString $isoImage ($pvdOffset + 318) "" 128
# Data Preparer at 446 (128 bytes)
Write-IsoString $isoImage ($pvdOffset + 446) "" 128
# Application at 574 (128 bytes)
Write-IsoString $isoImage ($pvdOffset + 574) "" 128
# Copyright at 702 (37 bytes)
Write-IsoString $isoImage ($pvdOffset + 702) "" 37
# Abstract at 739 (37 bytes)
Write-IsoString $isoImage ($pvdOffset + 739) "" 37
# Bibliographic at 776 (37 bytes)
Write-IsoString $isoImage ($pvdOffset + 776) "" 37

# Volume Creation Date at 813 (17 bytes)
Write-IsoString $isoImage ($pvdOffset + 813) "2025122300000000" 16 0x30
$isoImage[$pvdOffset + 829] = 0x30
$isoImage[$pvdOffset + 830] = 0x00  # GMT offset

# Modification/Expiration/Effective dates at 831, 848, 865 (17 bytes each)
Write-IsoString $isoImage ($pvdOffset + 831) "2025122300000000" 16 0x30
$isoImage[$pvdOffset + 847] = 0x30; $isoImage[$pvdOffset + 848] = 0x00

# File Structure Version at 881
$isoImage[$pvdOffset + 881] = 0x01

# ===== Boot Record Volume Descriptor (Sector 17) - El Torito =====
$brOffset = 17 * $isoSectorSize
$isoImage[$brOffset + 0] = 0x00  # Type: Boot Record
Write-IsoString $isoImage ($brOffset + 1) "CD001" 5 0x00  # Standard ID
$isoImage[$brOffset + 6] = 0x01  # Version
# Boot System Identifier (32 bytes) - MUST be "EL TORITO SPECIFICATION" padded with zeros
Write-IsoString $isoImage ($brOffset + 7) "EL TORITO SPECIFICATION" 32 0x00
# Boot Identifier (32 bytes) - unused, zeros
# Boot Catalog pointer at offset 71 (4 bytes, little-endian)
$bootCatalogSector = 19
$isoImage[$brOffset + 71] = [byte]($bootCatalogSector -band 0xFF)
$isoImage[$brOffset + 72] = [byte](($bootCatalogSector -shr 8) -band 0xFF)
$isoImage[$brOffset + 73] = [byte](($bootCatalogSector -shr 16) -band 0xFF)
$isoImage[$brOffset + 74] = [byte](($bootCatalogSector -shr 24) -band 0xFF)

# ===== Volume Descriptor Set Terminator (Sector 18) =====
$termOffset = 18 * $isoSectorSize
$isoImage[$termOffset + 0] = 0xFF  # Type: Terminator
Write-IsoString $isoImage ($termOffset + 1) "CD001" 5 0x00
$isoImage[$termOffset + 6] = 0x01  # Version

# ===== Boot Catalog (Sector 19) =====
$catOffset = 19 * $isoSectorSize

# Validation Entry (32 bytes)
$isoImage[$catOffset + 0] = 0x01   # Header ID (must be 1)
$isoImage[$catOffset + 1] = 0x00   # Platform ID (0 = 80x86)
$isoImage[$catOffset + 2] = 0x00   # Reserved
$isoImage[$catOffset + 3] = 0x00   # Reserved
# ID String (24 bytes)
Write-IsoString $isoImage ($catOffset + 4) "BOLT OS" 24 0x00
# Checksum (2 bytes at offset 28-29) - calculate to make sum = 0
# Key bytes first
$isoImage[$catOffset + 30] = 0x55
$isoImage[$catOffset + 31] = 0xAA

# Calculate checksum (sum of all 16 words must equal 0)
$checksum = 0
for ($i = 0; $i -lt 32; $i += 2) {
    if ($i -ne 28) {  # Skip checksum field itself
        $word = [int]$isoImage[$catOffset + $i] + ([int]$isoImage[$catOffset + $i + 1] -shl 8)
        $checksum = ($checksum + $word) -band 0xFFFF
    }
}
$checksumVal = (0x10000 - $checksum) -band 0xFFFF
$isoImage[$catOffset + 28] = [byte]($checksumVal -band 0xFF)
$isoImage[$catOffset + 29] = [byte](($checksumVal -shr 8) -band 0xFF)

# Initial/Default Entry (32 bytes, starts at offset 32)
$defOffset = $catOffset + 32
$isoImage[$defOffset + 0] = 0x88   # Boot Indicator: 0x88 = bootable
$isoImage[$defOffset + 1] = 0x00   # Boot Media Type: 0 = no emulation
$isoImage[$defOffset + 2] = 0x00   # Load Segment (low byte) - 0 means use default 0x7C0
$isoImage[$defOffset + 3] = 0x00   # Load Segment (high byte)
$isoImage[$defOffset + 4] = 0x00   # System Type (copy of partition table byte)
$isoImage[$defOffset + 5] = 0x00   # Unused
# Sector Count - number of 512-byte virtual sectors to load
$bootLoadSectors = 4  # Load 4 * 512 = 2048 bytes (one CD sector with our bootloader)
$isoImage[$defOffset + 6] = [byte]($bootLoadSectors -band 0xFF)
$isoImage[$defOffset + 7] = [byte](($bootLoadSectors -shr 8) -band 0xFF)
# Load RBA - absolute sector number of boot image
$bootImageSector = 20
$isoImage[$defOffset + 8] = [byte]($bootImageSector -band 0xFF)
$isoImage[$defOffset + 9] = [byte](($bootImageSector -shr 8) -band 0xFF)
$isoImage[$defOffset + 10] = [byte](($bootImageSector -shr 16) -band 0xFF)
$isoImage[$defOffset + 11] = [byte](($bootImageSector -shr 24) -band 0xFF)
# Rest is unused (selection criteria)

# ===== Boot Image (Sector 20) =====
$bootImageOffset = 20 * $isoSectorSize
[Array]::Copy($bootCdBin, 0, $isoImage, $bootImageOffset, [Math]::Min($bootCdBin.Length, $isoSectorSize))

# ===== Kernel data (Sector 21+) =====
$kernelIsoOffset = 21 * $isoSectorSize
[Array]::Copy($kernelBin, 0, $isoImage, $kernelIsoOffset, $kernelBin.Length)

# Write the ISO
$isoPath = Join-Path $DriveDir "bolt_os.iso"
[System.IO.File]::WriteAllBytes($isoPath, $isoImage)

Write-Host "[INFO] Created bootable ISO: $isoPath" -ForegroundColor Green
Write-Host "[INFO] ISO Size: $([Math]::Round($isoSize / 1024)) KB ($totalIsoSectors sectors)" -ForegroundColor DarkGray

# ==============================================================================
# Build Summary
# ==============================================================================

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "    Build Successful!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Output Structure:" -ForegroundColor DarkGray
Write-Host "  build/" -ForegroundColor Gray
Write-Host "    core/arch/     - Architecture objects" -ForegroundColor DarkGray
Write-Host "    core/memory/   - Memory management" -ForegroundColor DarkGray
Write-Host "    core/sched/    - Scheduler" -ForegroundColor DarkGray
Write-Host "    core/sys/      - System services" -ForegroundColor DarkGray
Write-Host "    drivers/       - Device drivers" -ForegroundColor DarkGray
Write-Host "    fs/            - Filesystems" -ForegroundColor DarkGray
Write-Host "    shell/         - Shell & commands" -ForegroundColor DarkGray
Write-Host "  drive/" -ForegroundColor Gray
Write-Host "    harddisk.img   - FAT32 virtual drive" -ForegroundColor DarkGray
Write-Host ""
