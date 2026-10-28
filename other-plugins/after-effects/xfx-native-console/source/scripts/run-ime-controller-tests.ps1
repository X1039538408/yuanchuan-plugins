$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\ime-controller-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W3 /O2 /DUNICODE /D_UNICODE `"$root\native\tests\ime_controller_tests.cpp`" `"$root\native\src\ime_controller.cpp`" /Fe:tests.exe imm32.lib user32.lib"
Push-Location $outDir
try {
    cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\tests.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
