$ErrorActionPreference = 'Stop'
$ae = @(Get-Process | Where-Object { $_.ProcessName -match '(?i)afterfx|after.?effects' })
if ($ae.Count) { throw 'After Effects must be exited before snapshot test compilation; no process was terminated.' }
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\snapshot-save-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$sdk = Join-Path $root 'vendor\ae-sdk\Examples'
# Do not add SDK Headers\Win: its legacy stdint.h shadows MSVC's standard library.
$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W3 /bigobj /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /DMSWindows /I`"$sdk\Headers`" /I`"$sdk\Headers\SP`" /I`"$sdk\Resources`" /I`"$sdk\Util`" `"$root\native\tests\snapshot_save_tests.cpp`" `"$root\native\src\search_core.cpp`" `"$root\native\src\settings.cpp`" `"$root\native\src\hotkey.cpp`" `"$root\native\src\ime_controller.cpp`" `"$root\native\src\ae_script.cpp`" `"$root\native\src\ae_capture.cpp`" `"$root\native\src\console_support.cpp`" `"$root\native\src\catalog_customization.cpp`" `"$root\native\src\text_input_dialog.cpp`" `"$root\native\src\extendscript_bridge.cpp`" `"$root\native\src\ui_theme.cpp`" `"$root\native\src\themed_menu.cpp`" `"$root\native\src\gallery_model.cpp`" `"$root\native\src\thumbnail_list.cpp`" `"$root\native\src\thumbnail_cache.cpp`" `"$sdk\Util\AEGP_SuiteHandler.cpp`" `"$sdk\Util\MissingSuiteError.cpp`" /Fe:tests.exe uxtheme.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib windowscodecs.lib gdiplus.lib comdlg32.lib imm32.lib comctl32.lib"
Push-Location $outDir
try {
    cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'artifacts') | Out-Null
    & .\tests.exe
    if ($LASTEXITCODE -ne 0) { throw "Snapshot save tests failed with native exit code $LASTEXITCODE" }
} finally { Pop-Location }

