[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'This test requires an already elevated session; it does not request elevation.'
}
$installer = Join-Path $ProjectRoot 'installer\XFXNativeConsoleInstaller.exe'
$payloads = @(
    @{ Source='installer\XFXNativeConsole.aex'; Target='Plug-ins\XFXNativeConsole.aex' },
    @{ Source='installer\XFXNativeConsole.aliases.tsv'; Target='Plug-ins\XFXNativeConsole.aliases.default.tsv' },
    @{ Source='installer\XFXNativeConsole.THIRD-PARTY-NOTICES.txt'; Target='Plug-ins\XFXNativeConsole.THIRD-PARTY-NOTICES.txt' },
    @{ Source='installer\XFX 中文控制台.jsx'; Target='Scripts\ScriptUI Panels\效果控制台.jsx' }
)
foreach ($source in @($installer) + @($payloads | ForEach-Object { Join-Path $ProjectRoot $_.Source })) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing input: $source" }
}
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('xfx-exe-test-' + [guid]::NewGuid().ToString('N'))
$target = Join-Path $testRoot '中文安装路径\Support Files'
function Assert-True($Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Invoke-Installer {
    $log = Join-Path $testRoot '安装日志.txt'
    $arguments = '--silent --target "' + $target + '" --log "' + $log + '"'
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    Assert-True ($process.ExitCode -eq 0) "Installer failed with exit code $($process.ExitCode). Log: $log"
}
function Assert-Payloads {
    foreach ($payload in $payloads) {
        $installed = Join-Path $target $payload.Target
        Assert-True (Test-Path -LiteralPath $installed -PathType Leaf) "Installed payload missing: $($payload.Target)"
        $expected = (Get-FileHash -LiteralPath (Join-Path $ProjectRoot $payload.Source) -Algorithm SHA256).Hash
        Assert-True ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -eq $expected) "Embedded payload mismatch: $($payload.Target)"
    }
}
try {
    New-Item -ItemType Directory -Path (Join-Path $target 'Plug-ins') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $target 'AfterFX.exe'), 'fixture, never executed')
    Invoke-Installer
    # Check the newly embedded panel before checking hashes of older resources.
    Assert-True (Test-Path -LiteralPath (Join-Path $target $payloads[3].Target) -PathType Leaf) 'Embedded ScriptUI panel missing'
    Assert-Payloads
    Write-Host 'PASS actual EXE fresh install: four embedded payload hashes and Unicode paths'

    $panel = Join-Path $target $payloads[3].Target
    $original = Join-Path (Split-Path -Parent $panel) 'FX Console.jsx'
    $legacy = Join-Path $target 'Plug-ins\XFXNativeConsole.aliases.tsv'
    [IO.File]::WriteAllText($panel, 'older Chinese panel')
    [IO.File]::WriteAllText($original, 'original vendor panel')
    [IO.File]::WriteAllText($legacy, 'user dictionary')
    Invoke-Installer
    Assert-Payloads
    Assert-True ([IO.File]::ReadAllText($original) -eq 'original vendor panel') 'Original vendor panel changed'
    Assert-True ([IO.File]::ReadAllText($legacy) -eq 'user dictionary') 'User dictionary changed'
    $backups = @(Get-ChildItem -LiteralPath (Split-Path -Parent $panel) -Filter '效果控制台.jsx.backup-*')
    Assert-True ($backups.Count -eq 1 -and [IO.File]::ReadAllText($backups[0].FullName) -eq 'older Chinese panel') 'Chinese panel backup is missing or incorrect'
    Write-Host 'PASS actual EXE upgrade: four payloads, old panel backup, vendor panel and user dictionary'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if ($resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolved).StartsWith('xfx-exe-test-')) {
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
}
