[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$cl = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
$project = Join-Path $ProjectRoot 'native\plugin\XFXNativeConsole.vcxproj'
$output = Join-Path $ProjectRoot 'artifacts\native-plugin'

# Building a loaded AEX can fail halfway through or leave AE with a stale
# module. Keep the same conservative process policy as the installer: never
# terminate AE, and require the caller to close both stable and beta hosts.
$runningAe = @(Get-Process -Name AfterFX, AfterFX.com, AfterFXa, AfterFXBeta, AfterFXBeta.com -ErrorAction SilentlyContinue)
if ($runningAe.Count) {
    $names = ($runningAe | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', '
    throw "After Effects 正在运行（$names）。请先退出所有 AE 后再构建；本脚本不会关闭 AE。"
}

if (-not (Test-Path -LiteralPath $msbuild)) { throw "找不到 MSBuild：$msbuild" }
if (-not (Test-Path -LiteralPath $cl)) { throw "找不到 C++ 编译器：$cl" }
if (-not (Test-Path -LiteralPath $project)) { throw "找不到插件工程：$project" }

New-Item -ItemType Directory -Force -Path $output | Out-Null
$env:AE_PLUGIN_BUILD_DIR = $output

$pluginDir = Join-Path $ProjectRoot 'native\plugin'
$intDir = Join-Path $pluginDir "obj\x64\$Configuration"
$intRelative = "obj\x64\$Configuration"
New-Item -ItemType Directory -Force -Path $intDir | Out-Null
Push-Location $pluginDir
try {
    & $cl /I '..\..\vendor\ae-sdk\Examples\Headers' /EP 'XFXNativeConsole_PiPL.r' | Set-Content -Encoding ascii (Join-Path $intDir 'XFXNativeConsole_PiPL.rr')
    if ($LASTEXITCODE -ne 0) { throw "PiPL 预处理失败，退出码：$LASTEXITCODE" }
    & '..\..\vendor\ae-sdk\Examples\Resources\PiPLtool.exe' "$intRelative\XFXNativeConsole_PiPL.rr" "$intRelative\XFXNativeConsole_PiPL.rrc"
    if ($LASTEXITCODE -ne 0) { throw "PiPLTool 处理失败，退出码：$LASTEXITCODE" }
    & $cl /D MSWindows /EP "$intRelative\XFXNativeConsole_PiPL.rrc" | Set-Content -Encoding ascii (Join-Path $pluginDir 'XFXNativeConsole_PiPL.rc')
    if ($LASTEXITCODE -ne 0) { throw "PiPL 资源生成失败，退出码：$LASTEXITCODE" }
}
finally {
    Pop-Location
}

& $msbuild $project /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /m /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "原生 AE 插件编译失败，退出码：$LASTEXITCODE" }

$plugin = Join-Path $output 'AEGP\XFXNativeConsole.aex'
if (-not (Test-Path -LiteralPath $plugin)) { throw "编译完成但没有找到输出文件：$plugin" }
Write-Host "原生插件已生成：$plugin"
