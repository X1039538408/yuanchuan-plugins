# MCP stdio host for the locally installed FrameFit desktop application.
$ErrorActionPreference = 'Stop'
$installRoot = Join-Path $env:LOCALAPPDATA 'FrameFit'
$bridgePath = Join-Path $env:LOCALAPPDATA 'FrameFitCodex\bridge.json'
$versionRoot = $null
if (Test-Path -LiteralPath $bridgePath) {
    $bridge = Get-Content -LiteralPath $bridgePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($bridge.installRoot -and $bridge.runtimeRoot) {
        $installRoot = [IO.Path]::GetFullPath($bridge.installRoot)
        $versionRoot = [IO.Path]::GetFullPath($bridge.runtimeRoot)
    }
}
$currentPath = Join-Path $installRoot 'current.json'
if (-not $versionRoot -and -not (Test-Path -LiteralPath $currentPath)) {
    [Console]::Error.WriteLine('请先安装并启动 FrameFit 桌面软件。安装说明：https://github.com/X1039538408/yuanchuan-plugins/blob/main/plugins/framefit/README.md')
    exit 1
}
if (-not $versionRoot) {
    $current = Get-Content -LiteralPath $currentPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $versionRoot = [IO.Path]::GetFullPath((Join-Path $installRoot $current.versionDirectory))
}
$versionsRoot = [IO.Path]::GetFullPath((Join-Path $installRoot 'versions'))
if (-not $versionRoot.StartsWith($versionsRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    [Console]::Error.WriteLine('FrameFit 安装目录无效，请重新安装桌面软件。')
    exit 1
}
$hostPath = Join-Path $versionRoot 'host\FrameFitHost.exe'
if (-not (Test-Path -LiteralPath $hostPath)) {
    [Console]::Error.WriteLine('本机 FrameFit MCP 组件缺失，请重新安装桌面软件。')
    exit 1
}
& $hostPath --state-dir (Join-Path $env:LOCALAPPDATA 'FrameFitCodex')
exit $LASTEXITCODE
