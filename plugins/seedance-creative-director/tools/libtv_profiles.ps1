param(
  [ValidateSet('list','login','run')]
  [string]$Action = 'list',
  [string]$Profile,
  [Parameter(ValueFromRemainingArguments=$true)]
  [string[]]$CommandArgs
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $env:USERPROFILE '.libtv-accounts'
$exe = (Get-Command libtv -ErrorAction Stop).Source

function Get-ProfileDir([string]$id) {
  if($id -notmatch '^[a-z0-9][a-z0-9_-]*$'){ throw 'Invalid profile id' }
  return (Join-Path $root $id)
}

New-Item -ItemType Directory -Force $root | Out-Null

switch($Action){
  'list' {
    $rows = @()
    Get-ChildItem -LiteralPath $root -Directory | ForEach-Object {
      $dir = $_.FullName
      $old = $env:LIBTV_CONFIG_DIR
      try {
        $env:LIBTV_CONFIG_DIR = $dir
        $json = (& $exe account info 2>$null | Out-String).Trim()
        $rows += [ordered]@{profile=$_.Name; configDir=$dir; status='ready'; accountInfo=$json}
      } catch {
        $rows += [ordered]@{profile=$_.Name; configDir=$dir; status='error'; error=$_.Exception.Message}
      } finally {
        if($null -eq $old){ Remove-Item Env:LIBTV_CONFIG_DIR -ErrorAction SilentlyContinue } else { $env:LIBTV_CONFIG_DIR=$old }
      }
    }
    $rows | ConvertTo-Json -Depth 8
  }
  'login' {
    if(-not $Profile){ throw 'Profile is required' }
    $dir = Get-ProfileDir $Profile
    New-Item -ItemType Directory -Force $dir | Out-Null
    $env:LIBTV_CONFIG_DIR = $dir
    & $exe login web --open
  }
  'run' {
    if(-not $Profile){ throw 'Profile is required' }
    if(-not $CommandArgs){ throw 'CommandArgs are required' }
    $dir = Get-ProfileDir $Profile
    if(-not (Test-Path (Join-Path $dir 'credentials.json'))){ throw 'Profile is not logged in' }
    $env:LIBTV_CONFIG_DIR = $dir
    & $exe @CommandArgs
  }
}
