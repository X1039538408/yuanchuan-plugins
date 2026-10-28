[CmdletBinding()]
param([string]$ProjectRoot)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$sdk = Join-Path $ProjectRoot 'vendor\ae-sdk\Examples'
$output = Join-Path $ProjectRoot 'artifacts\ae-capture-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'ae-capture-tests.exe'
$arguments = @('/nologo', '/utf-8', '/std:c++20', '/EHsc', '/W4', '/WX', '/DWIN32', '/D_WINDOWS',
    '/DUNICODE', '/D_UNICODE', '/DMSWindows', '/external:W0',
    "/external:I$msvc\include", "/external:I$kit\Include\$version\ucrt", "/external:I$kit\Include\$version\shared",
    "/external:I$kit\Include\$version\um", "/external:I$kit\Include\$version\winrt", "/external:I$sdk\Headers", "/external:I$sdk\Headers\SP",
    "/external:I$sdk\Headers\Win", "/external:I$sdk\Resources", "/I$ProjectRoot\native\src",
    (Join-Path $ProjectRoot 'native\src\ae_capture.cpp'), (Join-Path $ProjectRoot 'native\tests\ae_capture_tests.cpp'),
    "/Fo$output\", "/Fe$binary", '/link', '/MANIFESTUAC:NO', "/LIBPATH:$msvc\lib\x64",
    "/LIBPATH:$kit\Lib\$version\um\x64", "/LIBPATH:$kit\Lib\$version\ucrt\x64",
    'user32.lib', 'gdi32.lib', 'ole32.lib', 'windowscodecs.lib')
& "$msvc\bin\Hostx64\x64\cl.exe" @arguments
if ($LASTEXITCODE -ne 0) { throw "AE capture test build failed: $LASTEXITCODE" }
& $binary
if ($LASTEXITCODE -ne 0) { throw "AE capture tests failed: $LASTEXITCODE" }
