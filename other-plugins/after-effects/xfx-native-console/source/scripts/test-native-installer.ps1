[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('xfx-installer-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$sourceRoot = Join-Path $testRoot 'payload'
New-Item -ItemType Directory -Path (Join-Path $sourceRoot 'installer') | Out-Null
[IO.File]::WriteAllText((Join-Path $sourceRoot 'installer\XFXNativeConsole.aex'), 'new-plugin')
[IO.File]::WriteAllText((Join-Path $sourceRoot 'installer\XFXNativeConsole.aliases.tsv'), 'new-defaults')
[IO.File]::WriteAllText((Join-Path $sourceRoot 'installer\XFX 中文控制台.jsx'), 'new-panel')
$script = Join-Path $ProjectRoot 'scripts\install-native-plugin.ps1'
function Assert-True($Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Get-FixtureSnapshot([string]$Root) {
    $resolved = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $entries = @(Get-ChildItem -LiteralPath $resolved -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($resolved.Length + 1)
        if ($_.PSIsContainer) { 'D:' + $relative }
        else { 'F:' + $relative + '=' + [Convert]::ToBase64String([IO.File]::ReadAllBytes($_.FullName)) }
    })
    return (($entries | Sort-Object) -join "`n")
}

function New-AeFixture([string]$Name) {
    $path = Join-Path $testRoot "$Name\Support Files"
    New-Item -ItemType Directory -Path (Join-Path $path 'Plug-ins') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $path 'AfterFX.exe'), 'test-fixture')
    return $path
}

function Invoke-TestInstall([string]$Target) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -ProjectRoot $sourceRoot -AeRoot $Target -Silent -LogPath (Join-Path $testRoot 'install.log') 2>&1
        return @{ Code = $LASTEXITCODE; Output = ($output -join "`n") }
    } finally { $ErrorActionPreference = $previous }
}
try {
    $fresh = New-AeFixture 'fresh'
    $result = Invoke-TestInstall $fresh
    Assert-True ($result.Code -eq 0) "Fresh install failed: $($result.Output)"
    Assert-True ([IO.File]::ReadAllText((Join-Path $fresh 'Plug-ins\XFXNativeConsole.aex')) -eq 'new-plugin') 'Fresh plugin missing'
    Assert-True (Test-Path -LiteralPath (Join-Path $fresh 'Plug-ins\XFXNativeConsole.aliases.default.tsv')) 'Fresh installation must create the default dictionary'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $fresh 'Plug-ins\XFXNativeConsole.aliases.tsv'))) 'Fresh installation must leave legacy dictionary absent'
    Assert-True ([IO.File]::ReadAllText((Join-Path $fresh 'Scripts\ScriptUI Panels\效果控制台.jsx')) -eq 'new-panel') 'Fresh dockable panel payload missing or incorrect'
    Write-Host 'PASS fresh install'

    $upgrade = New-AeFixture 'upgrade'
    $plugin = Join-Path $upgrade 'Plug-ins\XFXNativeConsole.aex'
    $legacy = Join-Path $upgrade 'Plug-ins\XFXNativeConsole.aliases.tsv'
    $defaults = Join-Path $upgrade 'Plug-ins\XFXNativeConsole.aliases.default.tsv'
    [IO.File]::WriteAllText($plugin, 'old-plugin')
    [IO.File]::WriteAllText($legacy, 'user-customization')
    [IO.File]::WriteAllText($defaults, 'old-defaults')
    $bridge = Join-Path $upgrade 'Scripts\ScriptUI Panels\XFXEnhancerBridge.jsx'
    New-Item -ItemType Directory -Path (Split-Path -Parent $bridge) -Force | Out-Null
    [IO.File]::WriteAllText($bridge, 'original-bridge')
    $panel = Join-Path $upgrade 'Scripts\ScriptUI Panels\效果控制台.jsx'
    $vendorPanel = Join-Path $upgrade 'Scripts\ScriptUI Panels\FX Console.jsx'
    [IO.File]::WriteAllText($panel, 'old-panel')
    [IO.File]::WriteAllText($vendorPanel, 'original-vendor-panel')
    $result = Invoke-TestInstall $upgrade
    Assert-True ($result.Code -eq 0) "Upgrade failed: $($result.Output)"
    Assert-True ([IO.File]::ReadAllText($legacy) -eq 'user-customization') 'Upgrade overwrote legacy user dictionary'
    Assert-True ([IO.File]::ReadAllText($defaults) -eq 'new-defaults') 'Upgrade did not update defaults'
    $backups = @(Get-ChildItem -LiteralPath (Split-Path -Parent $plugin) -Filter 'XFXNativeConsole.aex.backup-*')
    Assert-True ($backups.Count -eq 1) 'Upgrade must retain old plugin backup'
    Assert-True ([IO.File]::ReadAllText($backups[0].FullName) -eq 'old-plugin') 'Plugin backup has wrong content'
    $disabled = @(Get-ChildItem -LiteralPath (Split-Path -Parent $bridge) -Filter 'XFXEnhancerBridge.jsx.disabled-*')
    Assert-True ($disabled.Count -eq 1) 'Upgrade must retain disabled bridge backup'
    Assert-True ([IO.File]::ReadAllText($disabled[0].FullName) -eq 'original-bridge') 'Bridge backup has wrong content'
    Assert-True ([IO.File]::ReadAllText($panel) -eq 'new-panel') 'Chinese panel was not upgraded'
    Assert-True ([IO.File]::ReadAllText($vendorPanel) -eq 'original-vendor-panel') 'Upgrade changed the original vendor panel'
    $panelBackups = @(Get-ChildItem -LiteralPath (Split-Path -Parent $panel) -Filter '效果控制台.jsx.backup-*')
    Assert-True ($panelBackups.Count -eq 1) 'Upgrade must retain exactly one old Chinese panel backup'
    Assert-True ([IO.File]::ReadAllText($panelBackups[0].FullName) -eq 'old-panel') 'Chinese panel backup lost the old payload'
    Write-Host 'PASS upgrade preserves user dictionary, vendor panel and Chinese panel/bridge backups'

    $rollback = New-AeFixture 'rollback'
    $rollbackPlugin = Join-Path $rollback 'Plug-ins\XFXNativeConsole.aex'
    $rollbackDefaults = Join-Path $rollback 'Plug-ins\XFXNativeConsole.aliases.default.tsv'
    [IO.File]::WriteAllText($rollbackPlugin, 'old-plugin')
    [IO.File]::WriteAllText($rollbackDefaults, 'old-defaults')
    $rollbackBefore = Get-FixtureSnapshot $rollback
    $locked = [IO.File]::Open($rollbackDefaults, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { $result = Invoke-TestInstall $rollback } finally { $locked.Dispose() }
    Assert-True ($result.Code -ne 0) 'Locked default dictionary must fail install'
    Assert-True ([IO.File]::ReadAllText($rollbackPlugin) -eq 'old-plugin') 'Rollback failed to restore old plugin'
    Assert-True ([IO.File]::ReadAllText($rollbackDefaults) -eq 'old-defaults') 'Rollback changed old dictionary'
    Assert-True ((Get-FixtureSnapshot $rollback) -eq $rollbackBefore) 'Rollback left transaction files or newly created Scripts directories'
    Assert-True (@(Get-ChildItem -LiteralPath (Join-Path $rollback 'Plug-ins') -Filter '*.stage-*').Count -eq 0) 'Rollback left staged files'
    Write-Host 'PASS locked-file rollback cleans transaction files and newly created Scripts directories'

    $panelRollback = New-AeFixture 'panel-rollback'
    $panelRollbackDirectory = Join-Path $panelRollback 'Scripts\ScriptUI Panels'
    New-Item -ItemType Directory -Path $panelRollbackDirectory -Force | Out-Null
    $lockedPanel = Join-Path $panelRollbackDirectory 'XFX 中文控制台.jsx'
    [IO.File]::WriteAllText((Join-Path $panelRollback 'Plug-ins\XFXNativeConsole.aex'), 'old-plugin')
    [IO.File]::WriteAllText((Join-Path $panelRollback 'Plug-ins\XFXNativeConsole.aliases.default.tsv'), 'old-defaults')
    [IO.File]::WriteAllText((Join-Path $panelRollback 'Plug-ins\XFXNativeConsole.aliases.tsv'), 'custom-legacy')
    [IO.File]::WriteAllText($lockedPanel, 'old-panel')
    [IO.File]::WriteAllText((Join-Path $panelRollbackDirectory 'FX Console.jsx'), 'original-vendor-panel')
    [IO.File]::WriteAllText((Join-Path $panelRollbackDirectory 'XFXEnhancerBridge.jsx'), 'old-bridge')
    $panelRollbackBefore = Get-FixtureSnapshot $panelRollback
    $locked = [IO.File]::Open($lockedPanel, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { $result = Invoke-TestInstall $panelRollback } finally { $locked.Dispose() }
    Assert-True ($result.Code -ne 0) 'Locked Chinese panel must fail install'
    Assert-True ((Get-FixtureSnapshot $panelRollback) -eq $panelRollbackBefore) 'Panel failure must restore the complete old file tree and leave no transaction debris'
    Write-Host 'PASS locked Chinese panel rolls back plugin, dictionary, panel and bridge state'

    $missingPanel = New-AeFixture 'missing-panel'
    [IO.File]::WriteAllText((Join-Path $missingPanel 'Plug-ins\XFXNativeConsole.aex'), 'old-plugin')
    $missingPanelBefore = Get-FixtureSnapshot $missingPanel
    $panelSource = Join-Path $sourceRoot 'installer\XFX 中文控制台.jsx'
    $heldPanelSource = $panelSource + '.missing-fixture'
    Move-Item -LiteralPath $panelSource -Destination $heldPanelSource
    try { $result = Invoke-TestInstall $missingPanel } finally { Move-Item -LiteralPath $heldPanelSource -Destination $panelSource }
    Assert-True ($result.Code -ne 0) 'Missing panel payload must reject installation'
    Assert-True ((Get-FixtureSnapshot $missingPanel) -eq $missingPanelBefore) 'Missing panel payload changed the AE target'
    [IO.File]::WriteAllText($panelSource, '')
    try { $result = Invoke-TestInstall $missingPanel } finally { [IO.File]::WriteAllText($panelSource, 'new-panel') }
    Assert-True ($result.Code -ne 0) 'Empty panel payload must reject installation'
    Assert-True ((Get-FixtureSnapshot $missingPanel) -eq $missingPanelBefore) 'Empty panel payload changed the AE target'
    Write-Host 'PASS missing and empty panel payloads leave targets unchanged'

    # Exercise the same transaction directly with two isolated AE roots.
    . $script -ProjectRoot $sourceRoot -LogPath (Join-Path $testRoot 'install.log')
    $createdScripts = New-AeFixture 'created-scripts-rollback'
    $blockedScripts = New-AeFixture 'blocked-scripts-rollback'
    [IO.File]::WriteAllText((Join-Path $blockedScripts 'Scripts'), 'user-file-not-directory')
    $createdBefore = Get-FixtureSnapshot $createdScripts
    $blockedBefore = Get-FixtureSnapshot $blockedScripts
    $rejected = $false
    try {
        Invoke-XfxNativeInstall -Targets @($createdScripts,$blockedScripts) -PluginSource (Join-Path $sourceRoot 'installer\XFXNativeConsole.aex') -AliasesSource (Join-Path $sourceRoot 'installer\XFXNativeConsole.aliases.tsv') -PanelSource $panelSource
    } catch { $rejected = $true }
    Assert-True $rejected 'Blocked Scripts path must fail directory setup'
    Assert-True ((Get-FixtureSnapshot $createdScripts) -eq $createdBefore) 'Directory setup failure leaked newly created empty Scripts directories'
    Assert-True ((Get-FixtureSnapshot $blockedScripts) -eq $blockedBefore) 'Directory setup failure changed a pre-existing Scripts file'
    Write-Host 'PASS directory setup failure cleans only directories created by this transaction'

    $invalid = Join-Path $testRoot 'not-ae'
    New-Item -ItemType Directory -Path $invalid | Out-Null
    $result = Invoke-TestInstall $invalid
    Assert-True ($result.Code -ne 0) 'Non-AE directory must be rejected'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $invalid 'Plug-ins'))) 'Rejected directory was mutated'
    Write-Host 'PASS non-AE directory rejection'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $temporary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolved.StartsWith($temporary, [StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolved).StartsWith('xfx-installer-tests-')) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
exit 0
