[CmdletBinding()]
param(
    [string]$JsxPath = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $PSScriptRoot 'dist' }
$compiler = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw '需要系统 .NET Framework 4 C# 编译器；本脚本不会下载组件。' }
if (-not $JsxPath) {
    $packaged = Join-Path $PSScriptRoot '..\AE 智能分层.jsx'
    $JsxPath = if (Test-Path -LiteralPath $packaged -PathType Leaf) { $packaged } else { Join-Path $PSScriptRoot '..\..\..\AE 智能分层.jsx' }
}
$JsxPath = (Resolve-Path -LiteralPath $JsxPath).Path
$expected = '2EB5FA596DFF774F50AA4029AAAAE8B3907F540475F6642E9825B52BBAAE279D'
if ((Get-FileHash -LiteralPath $JsxPath -Algorithm SHA256).Hash -ne $expected) { throw '源 JSX 的 SHA-256 与已审核版本不符，拒绝打包。' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$uninstaller = Join-Path $OutputDirectory 'uninstaller.exe'
$installer = Join-Path $OutputDirectory 'AE 智能分层.exe'
$common = @('/nologo', '/target:winexe', '/platform:anycpu', '/optimize+', '/codepage:65001', '/reference:System.dll', '/reference:System.Core.dll', '/reference:System.Xml.dll', '/reference:System.Drawing.dll', '/reference:System.Windows.Forms.dll')
$sources = @((Join-Path $PSScriptRoot 'Core.cs'), (Join-Path $PSScriptRoot 'Program.cs'))
& $compiler @common '/define:UNINSTALLER' "/out:$uninstaller" @sources
if ($LASTEXITCODE -ne 0) { throw "卸载程序编译失败：$LASTEXITCODE" }
& $compiler @common "/out:$installer" "/resource:$JsxPath,payload.jsx" "/resource:$uninstaller,uninstaller.exe" @sources
if ($LASTEXITCODE -ne 0) { throw "安装程序编译失败：$LASTEXITCODE" }
if ((Get-FileHash -LiteralPath $JsxPath -Algorithm SHA256).Hash -ne $expected) { throw '源 JSX 在构建过程中变化，禁止发布。' }
$build = [ordered]@{
    schema = 1
    payloadSha256 = $expected
    compiler = $compiler
    framework = '.NET Framework 4 / WinForms'
    installer = [ordered]@{ name = 'AE 智能分层.exe'; sha256 = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash; bytes = (Get-Item -LiteralPath $installer).Length }
    uninstaller = [ordered]@{ name = 'uninstaller.exe'; sha256 = (Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash; bytes = (Get-Item -LiteralPath $uninstaller).Length }
}
$build | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'build-report.json') -Encoding UTF8
Write-Output "已构建：$installer"
Write-Output "已构建：$uninstaller"
