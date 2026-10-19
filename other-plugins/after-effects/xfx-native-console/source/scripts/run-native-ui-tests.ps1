$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$outDir = Join-Path $root 'native\build\ui-tests'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$sdk = Join-Path $root 'vendor\ae-sdk\Examples'
$compile = "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /W3 /bigobj /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /DMSWindows /I`"$sdk\Headers`" /I`"$sdk\Headers\SP`" /I`"$sdk\Headers\Win`" /I`"$sdk\Resources`" /I`"$sdk\Util`" `"$root\native\tests\native_ui_tests.cpp`" `"$root\native\src\search_core.cpp`" `"$root\native\src\settings.cpp`" `"$root\native\src\hotkey.cpp`" `"$root\native\src\ime_controller.cpp`" `"$root\native\src\ae_script.cpp`" `"$root\native\src\ae_capture.cpp`" `"$root\native\src\console_support.cpp`" `"$root\native\src\catalog_customization.cpp`" `"$root\native\src\text_input_dialog.cpp`" `"$root\native\src\extendscript_bridge.cpp`" `"$root\native\src\ui_theme.cpp`" `"$root\native\src\themed_menu.cpp`" `"$root\native\src\gallery_model.cpp`" `"$root\native\src\thumbnail_list.cpp`" `"$root\native\src\thumbnail_cache.cpp`" `"$sdk\Util\AEGP_SuiteHandler.cpp`" `"$sdk\Util\MissingSuiteError.cpp`" /Fe:tests.exe uxtheme.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib windowscodecs.lib gdiplus.lib comdlg32.lib imm32.lib comctl32.lib"
Push-Location $outDir
try {
    cmd.exe /c $compile
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'artifacts') | Out-Null
    & .\tests.exe (Join-Path $root 'artifacts\ui-preview.png')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }
