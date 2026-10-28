[CmdletBinding()]
param([string]$ProjectRoot)

$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$msvc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$version = '10.0.26100.0'
$output = Join-Path $ProjectRoot 'artifacts\fxs-archive-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$binary = Join-Path $output 'fxs-archive-tests.exe'
$cl = "$msvc\bin\Hostx64\x64\cl.exe"
$include = @("/I$msvc\include", "/I$kit\Include\$version\ucrt",
    "/I$kit\Include\$version\shared", "/I$kit\Include\$version\um",
    "/I$ProjectRoot\native\src", "/I$ProjectRoot\vendor\zlib-inflate")
$zlib_objects = @()
foreach ($source in @('adler32.c', 'inflate.c', 'inffast.c', 'inftrees.c', 'zutil.c', 'crc32.c')) {
    $object = Join-Path $output ($source -replace '\.c$', '.obj')
    & $cl /nologo /TC /O2 @include /c (Join-Path $ProjectRoot "vendor\zlib-inflate\$source") "/Fo$object"
    if ($LASTEXITCODE -ne 0) { throw "zlib source build failed for ${source}: $LASTEXITCODE" }
    $zlib_objects += $object
}
$production_object = Join-Path $output 'fxs_archive.obj'
$test_object = Join-Path $output 'fxs_archive_tests.obj'
& $cl /nologo /TP /utf-8 /std:c++17 /EHsc /W4 /WX @include /c `
    (Join-Path $ProjectRoot 'native\src\fxs_archive.cpp') "/Fo$production_object"
if ($LASTEXITCODE -ne 0) { throw "FXS archive C++ source build failed: $LASTEXITCODE" }
& $cl /nologo /TP /utf-8 /std:c++17 /EHsc /W4 /WX @include /c `
    (Join-Path $ProjectRoot 'native\tests\fxs_archive_tests.cpp') "/Fo$test_object"
if ($LASTEXITCODE -ne 0) { throw "FXS archive C++ test build failed: $LASTEXITCODE" }
& $cl /nologo $zlib_objects $production_object $test_object `
    "/Fe$binary" '/link' '/MANIFESTUAC:NO' `
    "/LIBPATH:$msvc\lib\x64" `
    "/LIBPATH:$kit\Lib\$version\um\x64" `
    "/LIBPATH:$kit\Lib\$version\ucrt\x64"
if ($LASTEXITCODE -ne 0) { throw "FXS archive test build failed: $LASTEXITCODE" }
& $binary
if ($LASTEXITCODE -ne 0) { throw "FXS archive tests failed: $LASTEXITCODE" }
