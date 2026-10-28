[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
. (Join-Path $ProjectRoot 'scripts\install-native-plugin.ps1') -ProjectRoot $ProjectRoot
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('xfx-process-name-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
$owned = @()
try {
    foreach ($name in @('AfterFX.exe', 'AfterFX.com', 'AfterFXBeta.exe', 'AFTERFXBETA.COM', 'MyAfterFX.com', 'AfterFX.exe.old')) {
        $fixture = Join-Path $fixtureRoot $name
        Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\PING.EXE') -Destination $fixture
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $fixture
        $start.Arguments = '-n 20 127.0.0.1'
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $process = [Diagnostics.Process]::Start($start)
        $owned += $process
        $expected = $name -notin @('MyAfterFX.com', 'AfterFX.exe.old')
        $matches = @(Get-XfxAeProcesses | Where-Object Id -eq $process.Id)
        if (($matches.Count -gt 0) -ne $expected) { throw "Incorrect AE process match for $name (ProcessName=$($process.ProcessName))" }
        Write-Host "PASS $name -> ProcessName=$($process.ProcessName); recognized=$expected"
    }
} finally {
    foreach ($process in $owned) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
        $process.Dispose()
    }
    $resolved = [IO.Path]::GetFullPath($fixtureRoot)
    if ($resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolved).StartsWith('xfx-process-name-tests-')) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
