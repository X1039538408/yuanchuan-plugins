$ErrorActionPreference = 'Stop'

$sdk = if ($env:XFX_AE_SDK_DIR) { $env:XFX_AE_SDK_DIR } else { Join-Path (Split-Path -Parent $PSScriptRoot) 'vendor\ae-sdk' }
$required = @(
    (Join-Path $sdk 'Examples\Headers\AEConfig.h'),
    (Join-Path $sdk 'Examples\Util\AEGP_SuiteHandler.h'),
    (Join-Path $sdk 'Examples\Headers\AE_Effect.h')
)

Write-Host "检查 AE SDK：$sdk"
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -gt 0) {
    Write-Host '缺少以下 SDK 文件：'
    $missing | ForEach-Object { Write-Host "- $_" }
    Write-Host '请从 Adobe Developer 的 After Effects SDK 页面下载 SDK，并解压到 vendor\ae-sdk，或设置 XFX_AE_SDK_DIR。'
    exit 1
}

Write-Host 'AE SDK 检查通过。'
