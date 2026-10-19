$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\ae-script-tests'
$node = (Get-Command node -ErrorAction Stop).Source
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Push-Location -LiteralPath $outDir
try {
    $compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX `"$root\native\src\ae_script.cpp`" `"$root\native\tests\ae_script_tests.cpp`" /Fe:ae_script_tests.exe"
    & cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & '.\ae_script_tests.exe'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $node '.\ae_script_vm_tests.cjs'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
