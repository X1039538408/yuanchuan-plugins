[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference='Stop'
if (!$ProjectRoot) { $ProjectRoot=Split-Path -Parent $PSScriptRoot }
& pwsh -NoProfile -File (Join-Path $ProjectRoot 'scripts\check-ae-sdk.ps1')
if($LASTEXITCODE -ne 0){throw 'AE SDK check failed'}
& pwsh -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-native-plugin.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'AEX build failed'}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'artifacts\native-plugin\AEGP\XFXNativeConsole.aex') -Destination (Join-Path $ProjectRoot 'installer\XFXNativeConsole.aex') -Force
& pwsh -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-installer-exe.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'Installer build failed'}
& pwsh -NoProfile -File (Join-Path $ProjectRoot 'scripts\build-installer-exe.ps1') -ProjectRoot $ProjectRoot -Uninstaller
if($LASTEXITCODE -ne 0){throw 'Uninstaller build failed'}
& pwsh -NoProfile -File (Join-Path $ProjectRoot 'scripts\test-installer-payloads.ps1') -ProjectRoot $ProjectRoot
if($LASTEXITCODE -ne 0){throw 'Embedded payload check failed'}
Write-Host 'Effect Console release build and four embedded resource hashes passed.'
