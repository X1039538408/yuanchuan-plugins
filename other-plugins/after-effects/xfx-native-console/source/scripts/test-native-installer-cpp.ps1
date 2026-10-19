[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$output = Join-Path $ProjectRoot 'artifacts\native-installer-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'native-installer-tests.exe'
$compileArguments = @('/nologo', '/utf-8', '/std:c++17', '/EHsc', '/O2', '/DUNICODE', '/D_UNICODE',
    "/I$msvc\include", "/I$kit\Include\$version\ucrt", "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um",
    '/c', (Join-Path $ProjectRoot 'installer\XFXNativeConsoleInstaller.cpp'), "/Fo$output\installer-cli-check.obj")
& "$msvc\bin\Hostx64\x64\cl.exe" @compileArguments
if ($LASTEXITCODE -ne 0) { throw "Native installer CLI source build failed: $LASTEXITCODE" }
$arguments = @('/nologo', '/utf-8', '/std:c++17', '/EHsc', '/O2', '/DUNICODE', '/D_UNICODE',
    "/I$msvc\include", "/I$kit\Include\$version\ucrt", "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um",
    (Join-Path $ProjectRoot 'installer\tests\native-installer-tests.cpp'), "/Fo$output\native-installer-tests.obj", "/Fe$binary",
    '/link', '/SUBSYSTEM:CONSOLE', '/MANIFESTUAC:NO', "/LIBPATH:$msvc\lib\x64", "/LIBPATH:$kit\Lib\$version\um\x64",
    "/LIBPATH:$kit\Lib\$version\ucrt\x64", 'user32.lib', 'advapi32.lib', 'shell32.lib', 'ole32.lib')
& "$msvc\bin\Hostx64\x64\cl.exe" @arguments
if ($LASTEXITCODE -ne 0) { throw "Native installer test build failed: $LASTEXITCODE" }
& $binary
if ($LASTEXITCODE -ne 0) { throw "Native installer tests failed: $LASTEXITCODE" }
