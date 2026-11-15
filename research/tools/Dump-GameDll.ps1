# Copies the unpacked game.dll out of a running HELLDIVERS 2 (a HellForge session) into a PE file IDA can load:
# the image as mapped in memory, with each section's file offset set to its virtual address.
# Read-only: it opens the process for reading only and changes nothing in it.
#   .\Dump-GameDll.ps1 [-Out <path>]
param([string]$Out = (Join-Path $PSScriptRoot '..\bin\game.dump.dll'))

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Mem {
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr handle);
}
'@

$process = Get-Process helldivers2 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $process) { throw 'helldivers2.exe is not running. Start a HellForge session and wait for the ship first.' }
$module = $process.Modules | Where-Object ModuleName -eq 'game.dll' | Select-Object -First 1
if (-not $module) { throw 'game.dll is not loaded in helldivers2.exe yet.' }

$base = [Int64]$module.BaseAddress
$size = [int]$module.ModuleMemorySize
"helldivers2.exe pid $($process.Id), game.dll at 0x{0:X}, 0x{1:X} bytes" -f $base, $size

$handle = [Mem]::OpenProcess(0x0410, $false, $process.Id) # PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
if ($handle -eq [IntPtr]::Zero) { throw "OpenProcess failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

$image = New-Object byte[] $size
$page = 0x1000
$chunk = 0x100000
$unreadable = 0
try {
    for ($offset = 0; $offset -lt $size; $offset += $chunk) {
        $length = [Math]::Min($chunk, $size - $offset)
        $buffer = New-Object byte[] $length
        $read = [IntPtr]::Zero
        if ([Mem]::ReadProcessMemory($handle, [IntPtr]($base + $offset), $buffer, [IntPtr]$length, [ref]$read) -and [int64]$read -eq $length) {
            [Array]::Copy($buffer, 0, $image, $offset, $length)
            continue
        }
        # Part of the chunk is not readable: read it page by page, leaving zeros where a page cannot be read.
        for ($p = 0; $p -lt $length; $p += $page) {
            $one = New-Object byte[] $page
            if ([Mem]::ReadProcessMemory($handle, [IntPtr]($base + $offset + $p), $one, [IntPtr]$page, [ref]$read)) {
                [Array]::Copy($one, 0, $image, $offset + $p, $page)
            } else { $unreadable++ }
        }
    }
} finally { [void][Mem]::CloseHandle($handle) }

# Make the headers describe the memory layout: file alignment = section alignment, raw data = virtual address.
$nt = [BitConverter]::ToInt32($image, 0x3C)
if ([BitConverter]::ToUInt32($image, $nt) -ne 0x4550) { throw 'No PE header at the module base.' }
$sections = [BitConverter]::ToUInt16($image, $nt + 6)
$optionalSize = [BitConverter]::ToUInt16($image, $nt + 20)
$optional = $nt + 24
if ([BitConverter]::ToUInt16($image, $optional) -ne 0x20B) { throw 'Not a PE32+ image.' }
[Array]::Copy([BitConverter]::GetBytes([UInt64]$base), 0, $image, $optional + 0x18, 8)               # ImageBase = where it is loaded
$alignment = [BitConverter]::ToUInt32($image, $optional + 0x20)
[Array]::Copy([BitConverter]::GetBytes([UInt32]$alignment), 0, $image, $optional + 0x24, 4)           # FileAlignment
$table = $optional + $optionalSize
for ($i = 0; $i -lt $sections; $i++) {
    $s = $table + 40 * $i
    $virtualSize = [BitConverter]::ToUInt32($image, $s + 8)
    $virtualAddress = [BitConverter]::ToUInt32($image, $s + 12)
    $rawSize = [Math]::Min([UInt64]([Math]::Ceiling($virtualSize / $alignment) * $alignment), [UInt64]($size - $virtualAddress))
    [Array]::Copy([BitConverter]::GetBytes([UInt32]$rawSize), 0, $image, $s + 16, 4)                  # SizeOfRawData
    [Array]::Copy([BitConverter]::GetBytes([UInt32]$virtualAddress), 0, $image, $s + 20, 4)           # PointerToRawData
}

$Out = [IO.Path]::GetFullPath($Out)
[IO.File]::WriteAllBytes($Out, $image)
"Wrote $Out ($unreadable unreadable pages left as zeros)."
"Base 0x{0:X}: game.dll+RVA in HellForge's docs is 0x{0:X} + RVA in this dump." -f $base
