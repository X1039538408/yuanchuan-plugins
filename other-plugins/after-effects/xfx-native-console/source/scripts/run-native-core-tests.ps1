$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build'
$exe = Join-Path $outDir 'search_core_tests.exe'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /DUNICODE /D_UNICODE /I`"$root\native\src`" `"$root\native\src\search_core.cpp`" `"$root\native\src\settings.cpp`" `"$root\native\src\ime_controller.cpp`" `"$root\native\src\hotkey.cpp`" `"$root\native\tests\search_core_tests.cpp`" /Fe:`"$exe`" user32.lib imm32.lib"
cmd.exe /c $compile
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
