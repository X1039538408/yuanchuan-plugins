[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [switch]$Uninstaller,
    [switch]$FixtureTest
)

$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$installerDir = Join-Path $ProjectRoot 'installer'
$source = Join-Path $installerDir 'XFXNativeConsoleInstaller.cpp'
$resource = Join-Path $installerDir 'XFXNativeConsoleInstaller.rc'
$objectDir = Join-Path $installerDir 'obj-installer'
$finalExe = Join-Path $ProjectRoot 'installer\XFXNativeConsoleInstaller.exe'
if ($Uninstaller) { $finalExe = Join-Path $ProjectRoot 'installer\EffectConsoleUninstaller.exe' }
if ($FixtureTest) { $finalExe = Join-Path $ProjectRoot ('artifacts\installer-entry-tests\' + (Split-Path -Leaf $finalExe)) }
$defines = @()
if ($Uninstaller) { $defines += '/DXFX_UNINSTALLER' }
if ($FixtureTest) { $defines += '/DXFX_FIXTURE_TEST' }
$objectDir = Join-Path $objectDir (([int]$Uninstaller.IsPresent).ToString() + '-' + ([int]$FixtureTest.IsPresent))

if (-not (Test-Path -LiteralPath $source)) { throw "找不到安装器源文件：$source" }
foreach ($payload in @('XFXNativeConsole.aex', 'XFXNativeConsole.aliases.tsv',
        'XFXNativeConsole.THIRD-PARTY-NOTICES.txt', 'XFX 中文控制台.jsx')) {
    $payloadPath = Join-Path $installerDir $payload
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf) -or (Get-Item -LiteralPath $payloadPath).Length -eq 0) {
        throw "找不到完整安装资源：$payloadPath"
    }
}

$cl = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
$rc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe'
$link = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe'
$msvcInclude = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include'
$kitRoot = 'C:\Program Files (x86)\Windows Kits\10'
$kitVersion = '10.0.26100.0'
$kitInclude = Join-Path $kitRoot "Include\$kitVersion"
$msvcLib = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\lib\x64'
$kitLib = Join-Path $kitRoot "Lib\$kitVersion"
foreach ($tool in @($cl, $rc, $link, (Join-Path $msvcInclude 'yvals.h'), (Join-Path $kitInclude 'um\windows.h'))) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "找不到编译工具：$tool" }
}

New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $finalExe) | Out-Null
Push-Location $installerDir
try {
    & $cl /nologo /utf-8 /std:c++17 /EHsc /O2 /MT /DUNICODE /D_UNICODE /DWIN32 /D_WINDOWS @defines /I"$msvcInclude" /I"$(Join-Path $kitInclude 'ucrt')" /I"$(Join-Path $kitInclude 'shared')" /I"$(Join-Path $kitInclude 'um')" /c 'XFXNativeConsoleInstaller.cpp' "/Fo$(Join-Path $objectDir 'XFXNativeConsoleInstaller.obj')"
    if ($LASTEXITCODE -ne 0) { throw "安装器 C++ 编译失败，退出码：$LASTEXITCODE" }
    & $rc /nologo @defines /I"$(Join-Path $kitInclude 'um')" /I"$(Join-Path $kitInclude 'shared')" "/fo$(Join-Path $objectDir 'XFXNativeConsoleInstaller.res')" 'XFXNativeConsoleInstaller.rc'
    if ($LASTEXITCODE -ne 0) { throw "安装器资源编译失败，退出码：$LASTEXITCODE" }
    & $link /nologo /SUBSYSTEM:WINDOWS /MACHINE:X64 "/LIBPATH:$msvcLib" "/LIBPATH:$(Join-Path $kitLib 'um\x64')" "/LIBPATH:$(Join-Path $kitLib 'ucrt\x64')" "/OUT:$finalExe" (Join-Path $objectDir 'XFXNativeConsoleInstaller.obj') (Join-Path $objectDir 'XFXNativeConsoleInstaller.res') user32.lib advapi32.lib shell32.lib ole32.lib shell32.lib
    if ($LASTEXITCODE -ne 0) { throw "安装器链接失败，退出码：$LASTEXITCODE" }
}
finally {
    Pop-Location
}

Write-Host "安装器已生成：$finalExe"
