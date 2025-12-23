# ==============================================================================
# BOLT OS - Run Script (Bootable HDD - Persistent)
# ==============================================================================

param(
    [switch]$ISO,       # Force ISO installer mode
    [switch]$Fresh      # Create a fresh target HDD for installation
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"
$DriveDir = Join-Path $ProjectRoot "drive"
$HardDisk = Join-Path $DriveDir "harddisk.img"
$ISOImage = Join-Path $DriveDir "bolt_os.iso"
$SerialLog = Join-Path $BuildDir "serial.log"

$QEMU = "C:\Program Files\qemu\qemu-system-i386.exe"

# Function to check if harddisk has a bootable OS (FAT32 at sector 257)
function Test-BootableHardDisk {
    param($diskPath)
    
    if (-not (Test-Path $diskPath)) { return $false }
    
    try {
        $fs = [System.IO.File]::OpenRead($diskPath)
        
        # Check boot sector signature at sector 0 (0xAA55 at offset 510-511)
        $fs.Seek(510, [System.IO.SeekOrigin]::Begin) | Out-Null
        $sig1 = $fs.ReadByte()
        $sig2 = $fs.ReadByte()
        if ($sig1 -ne 0x55 -or $sig2 -ne 0xAA) {
            $fs.Close()
            return $false
        }
        
        # Check FAT32 at sector 257 (our boot+kernel layout)
        # Sector 257 = byte offset 257 * 512 = 131584
        $fat32Offset = 257 * 512
        
        # Check boot signature at sector 257
        $fs.Seek($fat32Offset + 510, [System.IO.SeekOrigin]::Begin) | Out-Null
        $sig1 = $fs.ReadByte()
        $sig2 = $fs.ReadByte()
        if ($sig1 -ne 0x55 -or $sig2 -ne 0xAA) {
            $fs.Close()
            return $false
        }
        
        # Check FAT32 signature string at offset 82 within the boot sector
        $fs.Seek($fat32Offset + 82, [System.IO.SeekOrigin]::Begin) | Out-Null
        $fsTypeBytes = New-Object byte[] 8
        $fs.Read($fsTypeBytes, 0, 8) | Out-Null
        $fsType = [System.Text.Encoding]::ASCII.GetString($fsTypeBytes)
        
        $fs.Close()
        
        return ($fsType -eq "FAT32   ")
    } catch {
        return $false
    }
}

# Determine boot mode
$BootFromHDD = $false
$BootFromISO = $false

if ($Fresh) {
    # Fresh install requested - force ISO mode
    $BootFromISO = $true
    Write-Host "[INFO] Fresh install requested - using ISO installer" -ForegroundColor Yellow
} elseif (Test-BootableHardDisk $HardDisk) {
    # Harddisk has bootable OS
    $BootFromHDD = $true
    Write-Host "[INFO] Bootable OS detected on harddisk.img" -ForegroundColor Green
} elseif (Test-Path $ISOImage) {
    # No bootable HDD, but ISO exists - use installer
    $BootFromISO = $true
    Write-Host "[INFO] No bootable OS on harddisk - using ISO installer" -ForegroundColor Yellow
} elseif (Test-Path $HardDisk) {
    # HDD exists but no FAT32 - try booting anyway
    $BootFromHDD = $true
    Write-Host "[WARN] Harddisk exists but no FAT32 detected - attempting boot" -ForegroundColor Yellow
} else {
    Write-Host "[ERROR] No bootable media found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

# Override with -ISO flag if specified
if ($ISO -and (Test-Path $ISOImage)) {
    $BootFromISO = $true
    $BootFromHDD = $false
    Write-Host "[INFO] ISO mode forced via -ISO flag" -ForegroundColor Yellow
}

if ($BootFromISO) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host "    BOLT OS - ISO Installer Mode" -ForegroundColor Yellow  
    Write-Host "========================================" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Booting from: $ISOImage" -ForegroundColor Cyan
    Write-Host "Target HDD:   $HardDisk" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "The installer will install BOLT OS to the hard disk." -ForegroundColor Green
    Write-Host ""
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "    BOLT OS - QEMU Virtual Machine" -ForegroundColor Cyan  
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Hardware Configuration:" -ForegroundColor Yellow
Write-Host "  CPU:      Pentium III (i686)" -ForegroundColor Gray
Write-Host "  RAM:      256 MB" -ForegroundColor Gray
Write-Host "  VGA:      Standard VESA (Bochs VBE)" -ForegroundColor Gray
Write-Host "  Storage:  64MB IDE HDD (bootable) + CD-ROM" -ForegroundColor Gray
Write-Host "  Network:  Intel E1000 Gigabit Ethernet" -ForegroundColor Gray
Write-Host "  Audio:    Intel AC97" -ForegroundColor Gray
Write-Host "  Input:    PS/2 Keyboard & Mouse" -ForegroundColor Gray
Write-Host ""
Write-Host "  ** PERSISTENT MODE - Changes saved to harddisk.img **" -ForegroundColor Green
Write-Host ""

# Clear previous serial log
if (Test-Path $SerialLog) {
    Remove-Item $SerialLog -Force
}

# Create a PowerShell script for the serial monitor window
$MonitorScript = @"
`$Host.UI.RawUI.WindowTitle = 'BOLT OS - Serial Debug Console'
`$Host.UI.RawUI.BackgroundColor = 'Black'
`$Host.UI.RawUI.ForegroundColor = 'Green'
Clear-Host
Write-Host '========================================' -ForegroundColor Cyan
Write-Host '  BOLT OS - Serial Debug Output' -ForegroundColor Cyan
Write-Host '========================================' -ForegroundColor Cyan
Write-Host ''
Write-Host 'Waiting for serial output...' -ForegroundColor DarkGray
Write-Host ''

`$logFile = '$SerialLog'
`$lastPos = 0

while (`$true) {
    if (Test-Path `$logFile) {
        `$content = Get-Content `$logFile -Raw -ErrorAction SilentlyContinue
        if (`$content -and `$content.Length -gt `$lastPos) {
            `$newContent = `$content.Substring(`$lastPos)
            Write-Host `$newContent -NoNewline
            `$lastPos = `$content.Length
        }
    }
    Start-Sleep -Milliseconds 100
}
"@

$MonitorScriptPath = Join-Path $BuildDir "serial_monitor.ps1"
$MonitorScript | Out-File -FilePath $MonitorScriptPath -Encoding UTF8

# Start serial monitor in separate window
Write-Host "[INFO] Opening serial debug console..." -ForegroundColor DarkCyan
Start-Process powershell -ArgumentList "-NoExit", "-ExecutionPolicy", "Bypass", "-File", $MonitorScriptPath

Write-Host "[INFO] Starting QEMU..." -ForegroundColor DarkCyan
Write-Host ""

if ($BootFromISO) {
    # Boot from ISO with harddisk attached for installation
    & $QEMU `
        -m 256 `
        -cpu pentium3 `
        -drive file=$HardDisk,format=raw,if=ide,index=0,media=disk `
        -cdrom $ISOImage `
        -boot d `
        -vga std `
        -device e1000,netdev=net0 `
        -netdev user,id=net0 `
        -device AC97 `
        -rtc base=localtime `
        -serial file:$SerialLog
} else {
    # Boot from HDD (normal mode)
    & $QEMU `
        -m 256 `
        -cpu pentium3 `
        -drive file=$HardDisk,format=raw,if=ide,index=0,media=disk `
        -drive if=ide,index=1,media=cdrom `
        -boot c `
        -vga std `
        -device e1000,netdev=net0 `
        -netdev user,id=net0 `
        -device AC97 `
        -rtc base=localtime `
        -serial file:$SerialLog
}
