[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference='Stop'
if (!$ProjectRoot) { $ProjectRoot=Split-Path -Parent $PSScriptRoot }
# Prefer PowerShell 7 when installed, but Windows PowerShell 5.1 can run
# these scripts too. Do not assume pwsh is on PATH on an AE workstation.
$pwshCommand = Get-Command pwsh -ErrorAction SilentlyContinue
$shell = if ($pwshCommand) { $pwshCommand.Source } else { Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe' }
& $shell -NoProfile -File (Join-Path $ProjectRoot 'scripts\check-ae-sdk.ps1')
if($LASTEXITCODE -ne 0){throw 'AE SDK check failed'}
& $shell -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-native-plugin.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'AEX build failed'}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'artifacts\native-plugin\AEGP\XFXNativeConsole.aex') -Destination (Join-Path $ProjectRoot 'installer\XFXNativeConsole.aex') -Force
& $shell -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-installer-exe.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'Installer build failed'}
& $shell -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-installer-exe.ps1') -ProjectRoot $ProjectRoot -Uninstaller
if($LASTEXITCODE -ne 0){throw 'Uninstaller build failed'}
& $shell -NoProfile -File (Join-Path $ProjectRoot 'scripts\test-installer-payloads.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'Embedded payload check failed'}
Write-Host 'Effect Console release build and four embedded resource hashes passed.'
