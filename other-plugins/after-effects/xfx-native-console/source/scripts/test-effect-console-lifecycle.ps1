[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference='Stop'
if (!$ProjectRoot) { $ProjectRoot=Split-Path -Parent $PSScriptRoot }
foreach($uninstall in @($false,$true)) {
    & (Join-Path $PSScriptRoot 'build-installer-exe.ps1') -ProjectRoot $ProjectRoot -FixtureTest -Uninstaller:$uninstall
}
$installer=Join-Path $ProjectRoot 'artifacts\installer-entry-tests\XFXNativeConsoleInstaller.exe'
$uninstaller=Join-Path $ProjectRoot 'artifacts\installer-entry-tests\EffectConsoleUninstaller.exe'
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('effects-console-lifecycle-'+[guid]::NewGuid().ToString('N'))
$target=Join-Path $fixture 'AE 中文\Support Files'
$owned=$null
function Assert($value,[string]$detail) { if(!$value){throw $detail} }
function Invoke-Tool([string]$Exe,[string]$Target=$target) {
    $arguments='--silent --target "'+$Target+'" --log "'+(Join-Path $fixture 'lifecycle.log')+'"'
    $result=Start-Process -FilePath $Exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    return $result.ExitCode
}
try {
    New-Item -ItemType Directory -Force (Join-Path $target 'Plug-ins') | Out-Null
    [IO.File]::WriteAllText((Join-Path $target 'AfterFX.exe'),'fixture only, never run')
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\PING.EXE') -Destination (Join-Path $fixture 'AfterFX.exe')
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName=Join-Path $fixture 'AfterFX.exe'
    $start.Arguments='-t 127.0.0.1'
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $owned=[Diagnostics.Process]::Start($start)
    Assert ((Invoke-Tool $installer) -eq 1) 'Installer entry must reject running AE'
    Assert ((Invoke-Tool $uninstaller) -eq 1) 'Uninstaller entry must reject running AE'
    Assert (!$owned.HasExited) 'A tool closed the owned AE probe'
    Assert (!(Test-Path (Join-Path $target 'Plug-ins\XFXNativeConsole.aex'))) 'Blocked installer changed target'
    $log=Get-Content -LiteralPath (Join-Path $fixture 'lifecycle.log') -Raw -Encoding utf8
    Assert ($log.Contains('保存') -and $log.Contains('退出')) 'Blocked tools did not explain save and exit'
    . (Join-Path $PSScriptRoot 'install-native-plugin.ps1') -ProjectRoot $ProjectRoot
    $rejected=$false
    try { Assert-XfxAeClosed } catch { $rejected=$true }
    Assert ($rejected -and !$owned.HasExited) 'PowerShell installer must not close AE'
    # Terminate only the PING fixture created above, never any discovered real AE.
    $owned.Kill(); $owned.WaitForExit(5000) | Out-Null; $owned.Dispose(); $owned=$null
    Write-Host 'PASS GUI entry paths (silent mode) reject running AE; process survives; save-and-exit message; PowerShell parity'

    Assert ((Invoke-Tool $installer) -eq 0) 'Fresh install failed'
    $payloads=@{'installer\XFXNativeConsole.aex'='Plug-ins\XFXNativeConsole.aex';
        'installer\XFXNativeConsole.aliases.tsv'='Plug-ins\XFXNativeConsole.aliases.default.tsv';
        'installer\XFXNativeConsole.THIRD-PARTY-NOTICES.txt'='Plug-ins\XFXNativeConsole.THIRD-PARTY-NOTICES.txt';
        'installer\XFX 中文控制台.jsx'='Scripts\ScriptUI Panels\效果控制台.jsx'}
    foreach($source in $payloads.Keys) {
        Assert ((Get-FileHash -LiteralPath (Join-Path $ProjectRoot $source)).Hash -eq
            (Get-FileHash -LiteralPath (Join-Path $target $payloads[$source])).Hash) "Entry payload mismatch $source"
    }
    [IO.File]::WriteAllText((Join-Path $target 'Scripts\ScriptUI Panels\XFX 中文控制台.jsx'),'old panel')
    [IO.File]::WriteAllText((Join-Path $target 'Plug-ins\XFXNativeConsole.aliases.tsv'),'user aliases')
    [IO.File]::WriteAllText((Join-Path $target 'Plug-ins\Vendor.aex'),'unrelated plugin')
    Assert ((Invoke-Tool $installer) -eq 0) 'Upgrade failed'
    Assert (!(Test-Path (Join-Path $target 'Scripts\ScriptUI Panels\XFX 中文控制台.jsx'))) 'Legacy panel still active'
    Assert (@(Get-ChildItem (Join-Path $target 'Scripts\ScriptUI Panels') -Filter 'XFX 中文控制台.jsx.backup-*').Count -eq 1) 'Legacy panel not backed up'
    Assert ((Invoke-Tool $uninstaller) -eq 0) 'Uninstall failed'
    foreach($relative in $payloads.Values) { Assert (!(Test-Path (Join-Path $target $relative))) "Uninstall left $relative" }
    Assert ([IO.File]::ReadAllText((Join-Path $target 'Plug-ins\XFXNativeConsole.aliases.tsv')) -eq 'user aliases') 'User aliases removed'
    Assert ([IO.File]::ReadAllText((Join-Path $target 'Plug-ins\Vendor.aex')) -eq 'unrelated plugin') 'Vendor plugin removed'
    Assert ((Invoke-Tool $uninstaller) -eq 0) 'Repeated uninstall failed'
    Write-Host 'PASS entry-point install/upgrade/uninstall/repeat, Unicode paths, four payload hashes, renamed toolbar, user/vendor preservation'

    $outside=Join-Path $fixture 'outside'
    New-Item -ItemType Directory -Force (Join-Path $outside 'ScriptUI Panels') | Out-Null
    $victim=Join-Path $outside 'ScriptUI Panels\效果控制台.jsx'
    [IO.File]::WriteAllText($victim,'must remain outside')
    $junctionRoot=Join-Path $fixture 'junction\Support Files'
    New-Item -ItemType Directory -Force (Join-Path $junctionRoot 'Plug-ins') | Out-Null
    [IO.File]::WriteAllText((Join-Path $junctionRoot 'AfterFX.exe'),'fixture')
    New-Item -ItemType Junction -Path (Join-Path $junctionRoot 'Scripts') -Target $outside | Out-Null
    Assert ((Invoke-Tool $uninstaller $junctionRoot) -eq 1) 'Uninstaller accepted a directory junction'
    Assert ([IO.File]::ReadAllText($victim) -eq 'must remain outside') 'Junction redirected removal outside AE'
    Write-Host 'PASS junction path rejected without changing outside file'
} finally {
    if($owned){if(!$owned.HasExited){$owned.Kill();$owned.WaitForExit(5000)|Out-Null};$owned.Dispose()}
    # Keep fixtures and logs for review; no recursive deletion across a junction.
    Write-Host "Lifecycle fixtures: $fixture"
}
