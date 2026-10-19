[CmdletBinding()]
param([string]$ProjectRoot, [string]$InstallerPath)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
if (-not $InstallerPath) { $InstallerPath = Join-Path $ProjectRoot 'installer\XFXNativeConsoleInstaller.exe' }
$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path
# Load the executable strictly as a resource/data file: no entry point or UAC launch.
if (-not ('XfxInstallerPayloadReader' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class XfxInstallerPayloadReader {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", EntryPoint="FindResourceW", ExactSpelling=true, SetLastError=true)]
    static extern IntPtr FindResource(IntPtr module, IntPtr id, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool FreeLibrary(IntPtr module);
    public static byte[] Read(string path, int id) {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 0x02 | 0x20);
        if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            IntPtr resource = FindResource(module, new IntPtr(id), new IntPtr(10));
            if (resource == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            uint size = SizeofResource(module, resource);
            if (size == 0 || size > Int32.MaxValue) throw new InvalidOperationException("Invalid payload length");
            IntPtr loaded = LoadResource(module, resource);
            if (loaded == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr bytes = LockResource(loaded);
            if (bytes == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] copy = new byte[(int)size];
            Marshal.Copy(bytes, copy, 0, copy.Length);
            return copy;
        } finally { FreeLibrary(module); }
    }
}
'@
}
$payloads = @(
    @{Id=101;File='XFXNativeConsole.aex'},
    @{Id=102;File='XFXNativeConsole.aliases.tsv'},
    @{Id=103;File='XFXNativeConsole.THIRD-PARTY-NOTICES.txt'},
    @{Id=104;File='XFX 中文控制台.jsx'}
)
foreach ($payload in $payloads) {
    $bytes = [XfxInstallerPayloadReader]::Read($InstallerPath, $payload.Id)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { $actual = ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-','') }
    finally { $algorithm.Dispose() }
    $expected = (Get-FileHash -LiteralPath (Join-Path $ProjectRoot ('installer\' + $payload.File)) -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "Embedded installer payload mismatch: $($payload.File)" }
    Write-Host "PASS actual EXE resource $($payload.Id): $($payload.File) SHA256 matches"
}
