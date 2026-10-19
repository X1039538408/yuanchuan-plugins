[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$output = Join-Path $ProjectRoot 'artifacts\themed-menu-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'themed-menu-tests.exe'
$arguments = @('/nologo', '/utf-8', '/std:c++17', '/EHsc', '/W4', '/WX', '/DUNICODE', '/D_UNICODE',
    "/I$msvc\include", "/I$kit\Include\$version\ucrt", "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um",
    "/I$ProjectRoot\native\src", (Join-Path $ProjectRoot 'native\src\themed_menu.cpp'),
    (Join-Path $ProjectRoot 'native\tests\themed_menu_tests.cpp'), "/Fo$output\", "/Fe$binary", '/link', '/MANIFESTUAC:NO',
    "/LIBPATH:$msvc\lib\x64", "/LIBPATH:$kit\Lib\$version\um\x64", "/LIBPATH:$kit\Lib\$version\ucrt\x64",
    'user32.lib', 'gdi32.lib', 'comctl32.lib')
& "$msvc\bin\Hostx64\x64\cl.exe" @arguments
if ($LASTEXITCODE -ne 0) { throw "Themed menu test build failed: $LASTEXITCODE" }
& $binary
if ($LASTEXITCODE -ne 0) { throw "Themed menu tests failed: $LASTEXITCODE" }
