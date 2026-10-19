[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [Alias('Target')][string]$AeRoot,
    [switch]$Silent,
    [switch]$List,
    [string]$LogPath
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
if (-not $LogPath) { $LogPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'LoYoi\XFXNative\install.log' }

function Test-XfxAeRoot([string]$Path) {
    return ((Test-Path -LiteralPath (Join-Path $Path 'AfterFX.exe') -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $Path 'AfterFXBeta.exe') -PathType Leaf)) -and
           (Test-Path -LiteralPath (Join-Path $Path 'Plug-ins') -PathType Container)
}
function Get-XfxAeProcesses {
    # .NET removes .exe from ProcessName, but retains the .com extension.
    return @(Get-Process -Name AfterFX, AfterFX.com, AfterFXa, AfterFXBeta, AfterFXBeta.com -ErrorAction SilentlyContinue)
}
function Assert-XfxAeClosed {
    if (@(Get-XfxAeProcesses).Count) {
        throw 'After Effects 正在运行。请先保存所有工程，自行退出所有 AE 后重新运行。本程序不会关闭 AE。'
    }
}
function Get-XfxAeRoots {
    $candidates = New-Object 'System.Collections.Generic.List[string]'
    foreach ($registry in @('HKLM:\SOFTWARE\Adobe\After Effects', 'HKCU:\SOFTWARE\Adobe\After Effects',
                            'HKLM:\SOFTWARE\WOW6432Node\Adobe\After Effects', 'HKCU:\SOFTWARE\WOW6432Node\Adobe\After Effects')) {
        if (-not (Test-Path -LiteralPath $registry)) { continue }
        foreach ($version in @(Get-ChildItem -LiteralPath $registry -ErrorAction SilentlyContinue)) {
            $values = Get-ItemProperty -LiteralPath $version.PSPath -ErrorAction SilentlyContinue
            foreach ($name in @('PluginInstallPath', 'InstallPath', 'Path')) {
                $value = $values.$name
                if ($value) { $candidates.Add([Environment]::ExpandEnvironmentVariables([string]$value).TrimEnd('\','/')) }
            }
        }
    }
    foreach ($programFiles in @($env:ProgramFiles, ${env:ProgramW6432})) {
        if (-not $programFiles) { continue }
        $adobe = Join-Path $programFiles 'Adobe'
        if (Test-Path -LiteralPath $adobe -PathType Container) {
            foreach ($directory in @(Get-ChildItem -LiteralPath $adobe -Directory -Filter 'Adobe After Effects*' -ErrorAction SilentlyContinue)) {
                $candidates.Add((Join-Path $directory.FullName 'Support Files'))
            }
        }
    }
    $roots = @()
    foreach ($candidate in $candidates) {
        if ((Split-Path -Leaf $candidate) -eq 'Plug-ins') { $candidate = Split-Path -Parent $candidate }
        if (-not (Test-XfxAeRoot $candidate)) { $candidate = Join-Path $candidate 'Support Files' }
        if (Test-XfxAeRoot $candidate) { $roots += [IO.Path]::GetFullPath($candidate) }
    }
    return @($roots | Sort-Object -Unique)
}
function Write-XfxInstallLog([string]$Message) {
    $parent = Split-Path -Parent $LogPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $line = ('{0:yyyy-MM-dd HH:mm:ss.fff} {1}{2}' -f (Get-Date), $Message, [Environment]::NewLine)
    [IO.File]::AppendAllText($LogPath, $line, [Text.UTF8Encoding]::new($false))
}
function Invoke-XfxNativeInstall([string[]]$Targets, [string]$PluginSource, [string]$AliasesSource, [string]$PanelSource) {
    Assert-XfxAeClosed
    if (-not $Targets.Count) { throw '未找到有效 AE 目录。请用 -AeRoot 指定 Support Files 目录。' }
    foreach ($targetRoot in $Targets) {
        if (-not (Test-XfxAeRoot $targetRoot)) { throw "目标不是有效 AE Support Files 目录（需要 AfterFX.exe 和 Plug-ins）：$targetRoot" }
    }
    if (!$PanelSource) { $PanelSource=Join-Path (Split-Path -Parent $AliasesSource) 'XFX 中文控制台.jsx' }
    foreach ($source in @($PluginSource, $AliasesSource, $PanelSource)) {
        if (-not (Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -eq 0) {
            throw "安装包资源缺失或为空：$source"
        }
    }
    $suffix = (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N')
    $changes = New-Object 'System.Collections.Generic.List[object]'
    $createdDirectories = New-Object 'System.Collections.Generic.List[string]'
    $payloadFiles = @(@{ Name = 'XFXNativeConsole.aex'; Source = $PluginSource },
                      @{ Name = 'XFXNativeConsole.aliases.default.tsv'; Source = $AliasesSource })
    $notices = Join-Path (Split-Path -Parent $AliasesSource) 'XFXNativeConsole.THIRD-PARTY-NOTICES.txt'
    if (Test-Path -LiteralPath $notices -PathType Leaf) {
        $payloadFiles += @{ Name = 'XFXNativeConsole.THIRD-PARTY-NOTICES.txt'; Source = $notices }
    }
    foreach ($targetRoot in $Targets) {
        $pluginDirectory = Join-Path $targetRoot 'Plug-ins'
        foreach ($file in $payloadFiles) {
            $targetPath = Join-Path $pluginDirectory $file.Name
            $changes.Add([pscustomobject]@{ Target = $targetPath; Stage = "$targetPath.stage-$suffix"; Backup = "$targetPath.backup-$suffix";
                                           Source = $file.Source; OldMoved = $false; NewMoved = $false })
        }
        $scriptDirectory = Join-Path $targetRoot 'Scripts\ScriptUI Panels'
        $panelTarget=Join-Path $scriptDirectory '效果控制台.jsx'
        $changes.Add([pscustomobject]@{Target=$panelTarget;Stage="$panelTarget.stage-$suffix";Backup="$panelTarget.backup-$suffix";
                                      Source=$PanelSource;OldMoved=$false;NewMoved=$false})
        if (Test-Path -LiteralPath $scriptDirectory -PathType Container) {
            $legacyPanel = Join-Path $scriptDirectory 'XFX 中文控制台.jsx'
            if (Test-Path -LiteralPath $legacyPanel -PathType Leaf) {
                $changes.Add([pscustomobject]@{Target=$legacyPanel;Stage=$null;Backup="$legacyPanel.backup-$suffix";
                                               Source=$null;OldMoved=$false;NewMoved=$false})
            }
            foreach ($name in @('XFXEnhancerBridge.jsx', 'XFXEnhancerBridge.jsxbin')) {
                $bridge = Join-Path $scriptDirectory $name
                if (Test-Path -LiteralPath $bridge -PathType Leaf) {
                    $changes.Add([pscustomobject]@{ Target = $bridge; Stage = $null; Backup = "$bridge.disabled-$suffix";
                                                   Source = $null; OldMoved = $false; NewMoved = $false })
                }
            }
        }
    }
    try {
        foreach ($targetRoot in $Targets) {
            foreach ($directory in @((Join-Path $targetRoot 'Scripts'),(Join-Path $targetRoot 'Scripts\ScriptUI Panels'))) {
                if (!(Test-Path -LiteralPath $directory)) {
                    [IO.Directory]::CreateDirectory($directory) | Out-Null
                    $createdDirectories.Add($directory)
                } elseif (!(Test-Path -LiteralPath $directory -PathType Container)) { throw "脚本目录被同名文件占用：$directory" }
            }
        }
        # Stage every payload for every version before moving any installed file.
        foreach ($change in $changes) {
            if ($change.Stage) { [IO.File]::Copy($change.Source, $change.Stage, $false) }
        }
        foreach ($change in $changes) {
            Assert-XfxAeClosed
            if (Test-Path -LiteralPath $change.Target) {
                if (-not (Test-Path -LiteralPath $change.Target -PathType Leaf)) { throw "安装目标被同名目录占用：$($change.Target)" }
                [IO.File]::Move($change.Target, $change.Backup)
                $change.OldMoved = $true
            }
            if ($change.Stage) {
                [IO.File]::Move($change.Stage, $change.Target)
                $change.NewMoved = $true
            }
        }
    } catch {
        $originalFailure = $_.Exception.Message
        $rollbackErrors = @()
        for ($index = $changes.Count - 1; $index -ge 0; $index--) {
            $change = $changes[$index]
            try {
                if ($change.NewMoved) { [IO.File]::Delete($change.Target) }
                if ($change.OldMoved) { [IO.File]::Move($change.Backup, $change.Target) }
                if ($change.Stage -and [IO.File]::Exists($change.Stage)) { [IO.File]::Delete($change.Stage) }
            } catch { $rollbackErrors += $_.Exception.Message }
        }
        for ($index=$createdDirectories.Count-1; $index -ge 0; $index--) {
            try { [IO.Directory]::Delete($createdDirectories[$index],$false) } catch { $rollbackErrors += $_.Exception.Message }
        }
        if ($rollbackErrors.Count) { throw "$originalFailure`n部分回滚失败，请保留备份并查看日志：$($rollbackErrors -join '; ')" }
        throw "$originalFailure`n已回滚本次文件改动。"
    }
}

if ($MyInvocation.InvocationName -eq '.') { return }
try {
    Write-XfxInstallLog 'PowerShell 安装器启动'
    $targets = if ($AeRoot) { @([IO.Path]::GetFullPath($AeRoot)) } else { @(Get-XfxAeRoots) }
    Write-XfxInstallLog ("目标目录：" + ($targets -join '; '))
    if ($List) {
        $targets | Write-Output
        if (-not $targets.Count) { throw '未检测到有效 AE 目录。' }
        exit 0
    }
    Assert-XfxAeClosed
    $pluginSource = Join-Path $ProjectRoot 'installer\XFXNativeConsole.aex'
    $builtSource = Join-Path $ProjectRoot 'artifacts\native-plugin\AEGP\XFXNativeConsole.aex'
    if (Test-Path -LiteralPath $builtSource -PathType Leaf) { $pluginSource = $builtSource }
    Invoke-XfxNativeInstall -Targets $targets -PluginSource $pluginSource -AliasesSource (Join-Path $ProjectRoot 'installer\XFXNativeConsole.aliases.tsv')
    try { Write-XfxInstallLog ("安装成功：" + ($targets -join '; ')) } catch { }
    if (-not $Silent) { Write-Host ("效果控制台安装完成：`n" + ($targets -join "`n") + "`n日志：$LogPath") }
    exit 0
} catch {
    $failure = $_.Exception.Message
    try { Write-XfxInstallLog ("安装失败：" + $failure) } catch { }
    [Console]::Error.WriteLine("安装没有完成。$failure`n日志：$LogPath")
    exit 1
}
