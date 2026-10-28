$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\console-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX /DUNICODE /D_UNICODE `"$root\native\tests\console_support_tests.cpp`" /Fe:tests.exe gdiplus.lib ole32.lib user32.lib"
Push-Location $outDir
try {
    cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & .\tests.exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
