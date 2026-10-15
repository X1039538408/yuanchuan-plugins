param(
  [ValidateSet('help','discover-profiles','list-containers','resolve-assets','list-models','get-model-schema','validate-plan','prepare-video-config','submit','status','resume')]
  [string]$Action = 'help',
  [ValidateSet('canvas','conversation')]
  [string]$Adapter = 'canvas',
  [string]$Profile,
  [string]$ProjectId,
  [string]$SessionId,
  [string]$Prompt,
  [string]$Model,
  [string]$Mode,
  [string]$ParametersJson = '{}',
  [string]$AssetsJson = '[]',
  [string]$AssetManifestJson = '[]',
  [string]$RunId,
  [string]$SubmitId,
  [switch]$Confirmed,
  [switch]$Fresh,
  [string[]]$CommandArgs
)

$ErrorActionPreference = 'Stop'
# The account router emits UTF-8 JSON. PowerShell 7 must decode native output
# as UTF-8 before ConvertFrom-Json; otherwise Chinese text corrupts the JSON.
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$script:Root = Join-Path $env:USERPROFILE '.seedance-director'
$script:RunRoot = Join-Path $script:Root 'dreamina-runs'
$script:Router = Join-Path $env:USERPROFILE 'bin\dreamina-accounts.ps1'

function Json($Value) { $Value | ConvertTo-Json -Depth 30 -Compress }
function FromJson([string]$Text) { if ([string]::IsNullOrWhiteSpace($Text)) { return [pscustomobject]@{} }; return $Text | ConvertFrom-Json }
function Ensure-Dirs { New-Item -ItemType Directory -Force $script:RunRoot | Out-Null }
function New-Id { [guid]::NewGuid().ToString('N') }
function New-Uuid { [guid]::NewGuid().ToString() }
function Get-RunPath([string]$Id) {
  if ($Id -notmatch '^[a-f0-9]{32}$') { throw 'runId 必须是控制器生成的 32 位十六进制标识' }
  Join-Path $script:RunRoot ($Id + '.json')
}
function Save-Run($State) {
  $path = Get-RunPath $State.runId
  $tmp = $path + '.tmp'
  $State.updatedAt = [DateTimeOffset]::UtcNow.ToString('o')
  $State | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $tmp -Encoding utf8
  Move-Item -LiteralPath $tmp -Destination $path -Force
  $State
}
function Load-Run([string]$Id) { $path = Get-RunPath $Id; if (-not (Test-Path $path)) { throw 'run_not_found' }; Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json }

function Invoke-Router([string]$Act, [string]$Id, [string]$UseAdapter, [string[]]$CommandList, [int]$TimeoutSeconds = 60) {
  if (-not (Test-Path $script:Router)) { throw 'dreamina-accounts.ps1 未安装' }
  $result = & $script:Router -Action $Act -Adapter $UseAdapter -Profile $Id -TimeoutSeconds $TimeoutSeconds -CommandArgs $CommandList 2>&1 | Out-String
  try { $json = $result | ConvertFrom-Json } catch { throw '账号路由没有返回 JSON' }
  if (-not $json) { throw '账号路由返回为空' }
  $json
}

function Get-Profiles {
  $result = & $script:Router -Action list -Verify 2>&1 | Out-String
  try { $result | ConvertFrom-Json } catch { throw '账号列表读取失败' }
}

function Get-ContainerList {
  if ($Adapter -eq 'canvas') {
    if (-not $Profile) { throw 'profile_required' }
    $r = Invoke-Router 'exec' $Profile 'canvas' @('canvas','ls','--limit','6')
    if (-not $r.ok) { return [pscustomobject]@{ status='profile_error'; error=$r.cli.error; profile=$Profile; adapter='canvas' } }
    $items = @($r.cli.data.items) | Sort-Object updatedAt -Descending | Select-Object -First 6
    return [pscustomobject]@{ status='ready'; adapter='canvas'; profile=$Profile; containerType='project'; items=$items }
  }
  if (-not $Profile) { throw 'profile_required' }
  $r = Invoke-Router 'exec' $Profile 'conversation' @('session','list')
  if (-not $r.ok) { return [pscustomobject]@{ status='profile_error'; error=$r.cli.error; profile=$Profile; adapter='conversation' } }
  $raw = $r.cli
  $items = @($raw.items)
  if ($items.Count -eq 0 -and $raw.data) { $items = @($raw.data.items) }
  return [pscustomobject]@{ status='ready'; adapter='conversation'; profile=$Profile; containerType='session'; items=($items | Select-Object -First 6) }
}

function Get-Models {
  if (-not $Profile) { throw 'profile_required' }
  if ($Adapter -eq 'canvas') {
    $r = Invoke-Router 'exec' $Profile 'canvas' @('model','list','--type','video')
    if (-not $r.ok) { return [pscustomobject]@{ status='profile_error'; error=$r.cli.error } }
    return [pscustomobject]@{ status='ready'; adapter='canvas'; items=@($r.cli.data.items) }
  }
  $commands = @('text2video','image2video','multiframe2video','multimodal2video')
  $items = @()
  foreach ($command in $commands) {
    $r = Invoke-Router 'exec' $Profile 'conversation' @($command,'-h')
    $raw = [string]($r.cli.raw)
    $items += [pscustomobject]@{ command=$command; help=$raw; models=@([regex]::Matches($raw,'model_version[^\r\n]*?(?:values:|supported values:)[^\r\n]*') | ForEach-Object Value); ratios=@([regex]::Matches($raw,'ratio[^\r\n]*?supported values:[^\r\n]*') | ForEach-Object Value) }
  }
  [pscustomobject]@{ status='ready'; adapter='conversation'; items=$items }
}

function Get-ConversationCommandFromAssets {
  $assets = @(FromJson $AssetsJson)
  if ($assets.Count -eq 0) { return 'text2video' }
  $types = @($assets | ForEach-Object { [string]$_.type } | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique)
  if ($types.Count -eq 1 -and $types[0] -in @('image','picture','photo')) {
    if ($assets.Count -eq 1) { return 'image2video' }
    return 'multiframe2video'
  }
  return 'multimodal2video'
}

function Get-ConversationModelOptions {
  $command = Get-ConversationCommandFromAssets
  $r = Invoke-Router 'exec' $Profile 'conversation' @($command,'-h')
  if (-not $r.ok) { return [pscustomobject]@{ status='profile_error'; error=$r.cli.error; command=$command } }
  $raw = [string]$r.cli.raw
  $extract = {
    param([string[]]$patterns)
    foreach ($pattern in $patterns) {
      $match = [regex]::Match($raw, $pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
      if ($match.Success) { return @($match.Groups[1].Value -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ -and $_ -notmatch '^default' }) }
    }
    return @()
  }
  $modelValues = & $extract @('model_version:\s*([^\r\n]+)','--model_version[^\r\n]*?supported values:\s*([^\r\n]+)')
  $ratioValues = & $extract @('--ratio[^\r\n]*?supported values:\s*([^\r\n]+)','ratio:\s*([^\r\n]+)')
  $resolutionValues = & $extract @('--video_resolution[^\r\n]*?supported values(?: by model)?:\s*([^\r\n]+)')
  $durationMatch = [regex]::Match($raw, 'duration[^\r\n]*?(?:range:\s*|supported range:\s*)(\d+)\s*-\s*(\d+)', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
  $durationValues = if ($durationMatch.Success) { @([int]$durationMatch.Groups[1].Value..[int]$durationMatch.Groups[2].Value) } else { @() }
  return [pscustomobject]@{
    status='ready'; adapter='conversation'; command=$command
    help=$raw
    models=@($modelValues); modelValues=@($modelValues)
    ratios=@($ratioValues); ratioValues=@($ratioValues)
    resolutions=@($resolutionValues); resolutionValues=@($resolutionValues)
    durations=@($durationValues); durationValues=@($durationValues)
  }
}

function Get-LastConfig {
  $path = Join-Path $script:Root 'last-config.json'
  if (-not (Test-Path -LiteralPath $path)) { return $null }
  try { return (Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json) } catch { return $null }
}

function Save-LastConfig($State) {
  $path = Join-Path $script:Root 'last-config.json'
  $safe = [ordered]@{ provider='dreamina'; adapter=$State.adapter; profileId=$State.profileId; containerType=$State.containerType; containerId=$State.containerId; model=$State.model; mode=$State.mode; parameters=$State.parameters; assetBindings=$State.assetBindings; updatedAt=[DateTimeOffset]::UtcNow.ToString('o') }
  $safe | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding utf8
}

function Prepare-VideoConfig {
  Ensure-Dirs
  if (-not $Profile) {
    return [pscustomobject]@{ status='needs_profile'; profiles=@(Get-Profiles); provider='dreamina'; adapter=$Adapter; cachedConfig=Get-LastConfig }
  }
  $containers = Get-ContainerList
  $models = if ($Adapter -eq 'conversation') { Get-ConversationModelOptions } else { Get-Models }
  $selected = if ($Adapter -eq 'canvas') { @($models.items | Select-Object -First 1) } else { @($models.models | Select-Object -First 1) }
  $schema = $null
  if ($selected.Count -gt 0 -and $Adapter -eq 'canvas') {
    $candidate = $selected[0]
    $modelName = if ($candidate.modelName) { [string]$candidate.modelName } elseif ($candidate.name) { [string]$candidate.name } else { $null }
    if ($modelName) { $schema = Get-ModelSchema -Profile $Profile -Model $modelName }
  }
  [pscustomobject]@{
    status='ready'; provider='dreamina'; adapter=$Adapter; profile=$Profile
    profiles=@(Get-Profiles); containers=$containers; models=$models; schema=$schema
    cachedConfig=Get-LastConfig
    cache=[pscustomobject]@{ source='controller'; generatedAt=[DateTimeOffset]::UtcNow.ToString('o'); ttlSeconds=600 }
    capabilityHash=([guid]::NewGuid().ToString('N'))
  }
}

function Get-ModelSchema {
  if (-not $Profile -or -not $Model) { throw 'profile_and_model_required' }
  if ($Adapter -eq 'canvas') {
    $r = Invoke-Router 'exec' $Profile 'canvas' @('model',$Model)
    if (-not $r.ok) { return [pscustomobject]@{ status='profile_error'; error=$r.cli.error } }
    return [pscustomobject]@{ status='ready'; adapter='canvas'; model=$Model; schema=$r.cli.data.matches }
  }
  if (-not $Mode) { throw 'mode_or_command_required' }
  $r = Invoke-Router 'exec' $Profile 'conversation' @($Mode,'-h')
  [pscustomobject]@{ status='ready'; adapter='conversation'; command=$Mode; help=$r.cli.raw }
}

function Resolve-Assets {
  $manifest = @(FromJson $AssetManifestJson)
  $assets = @(FromJson $AssetsJson)
  if ($Adapter -eq 'conversation') {
    $missing = @($assets | Where-Object { $_.path -and -not (Test-Path -LiteralPath $_.path) })
    if ($missing.Count) { return [pscustomobject]@{ status='needs_asset_choice'; missing=$missing } }
    return [pscustomobject]@{ status='ready'; adapter='conversation'; bindings=$assets }
  }
  if (-not $Profile -or -not $ProjectId) { throw 'profile_and_project_required' }
  $refs = [regex]::Matches([string]$Prompt,'\{\{Mixed\s+(\d+)\}\}') | ForEach-Object { [int]$_.Groups[1].Value } | Select-Object -Unique
  $bindings = @(); $unresolved = @()
  foreach ($index in $refs) {
    $item = @($manifest | Where-Object { [int]$_.index -eq $index }) | Select-Object -First 1
    if ($item -and $item.nodeId) { $bindings += [pscustomobject]@{ index=$index; nodeId=$item.nodeId; purpose=$item.purpose }; continue }
    $name = if ($item) { $item.name } else { $null }
    $args = @('node','find','--project-id',$ProjectId,'--limit','50')
    if ($name) { $args += @('--name',[string]$name) }
    if ($item.type) { $args += @('--type',[string]$item.type) }
    $r = Invoke-Router 'exec' $Profile 'canvas' $args
    $found = @($r.cli.data.items)
    if ($found.Count -eq 1) { $bindings += [pscustomobject]@{ index=$index; nodeId=$found[0].nodeId; purpose=$item.purpose } } else { $unresolved += [pscustomobject]@{ index=$index; name=$name; candidates=$found } }
  }
  if ($unresolved.Count) { return [pscustomobject]@{ status='needs_asset_choice'; bindings=$bindings; unresolved=$unresolved } }
  [pscustomobject]@{ status='ready'; adapter='canvas'; bindings=$bindings }
}

function New-Plan {
  $parameters = FromJson $ParametersJson
  if (-not $Prompt) { throw 'prompt_required' }
  if ($Adapter -eq 'canvas' -and -not $ProjectId) { throw 'project_required' }
  if ($Adapter -eq 'conversation' -and $null -eq $SessionId) { throw 'session_required' }
  $assets = Resolve-Assets
  if ($assets.status -ne 'ready') { return [pscustomobject]@{ status=$assets.status; assets=$assets } }
  if (-not $Model) { throw 'model_required' }
  if ($Adapter -eq 'canvas' -and -not $Mode) { throw 'mode_required' }
  $id = if ($RunId) { $RunId } else { New-Id }
  $state = [ordered]@{ version=1; runId=$id; provider='dreamina'; adapter=$Adapter; profileId=$Profile;
    containerType=$(if($Adapter -eq 'canvas'){'project'}else{'session'}); containerId=$(if($Adapter -eq 'canvas'){$ProjectId}else{$SessionId});
    model=$Model; mode=$Mode; parameters=$parameters; assetBindings=@($assets.bindings); promptHash=([BitConverter]::ToString(([Security.Cryptography.SHA256]::Create()).ComputeHash([Text.Encoding]::UTF8.GetBytes($Prompt))).Replace('-','').ToLower());
    stage='plan_validated'; createdAt=[DateTimeOffset]::UtcNow.ToString('o'); updatedAt=[DateTimeOffset]::UtcNow.ToString('o') }
  Save-Run ([pscustomobject]$state) | Out-Null
  [pscustomobject]@{ status='awaiting_final_confirmation'; runId=$id; state=$state; assets=$assets }
}

function Submit-Plan {
  if (-not $RunId) { throw 'run_required' }
  if (-not $Prompt) { throw 'prompt_required_for_submit' }
  $state = Load-Run $RunId
  if ($state.stage -in @('submitted','running','completed')) { return [pscustomobject]@{ status='not_submittable'; state=$state } }
  if (-not $Confirmed) { return [pscustomobject]@{ status='confirmation_required'; state=$state } }
  if ($state.adapter -eq 'conversation') {
    $command = if ($state.parameters.command) { [string]$state.parameters.command } elseif (@($state.assetBindings).Count -eq 0) {'text2video'} elseif (@($state.assetBindings).Count -eq 1) {'image2video'} else {'multimodal2video'}
    $args = @($command,'--prompt',$Prompt,'--session',[string]$state.containerId,'--poll','0')
    foreach ($p in $state.parameters.PSObject.Properties) { if ($p.Name -ne 'command') { $args += @('--' + $p.Name.Replace('_','-'),[string]$p.Value) } }
    foreach ($a in @($state.assetBindings)) { if ($a.path) { $flag = if($a.type -eq 'video'){'--video'}elseif($a.type -eq 'audio'){'--audio'}else{'--image'}; $args += @($flag,[string]$a.path) } }
    $r = Invoke-Router 'exec' $state.profileId 'conversation' $args
    $submit = $r.cli.submit_id
    if (-not $submit) { $state.stage='recoverable'; Save-Run $state | Out-Null; return [pscustomobject]@{ status='recoverable'; state=$state; result=$r } }
     $state.submitId=[string]$submit; $state.stage='submitted'; Save-Run $state | Out-Null; Save-LastConfig $state
    return [pscustomobject]@{ status='submitted'; state=$state; result=$r }
  }
  $args = @('node','create','video','--project-id',[string]$state.containerId,'--prompt',$Prompt,'--model',[string]$state.model,'--mode',[string]$state.mode,'--run','--submit-id',(New-Uuid))
  foreach ($p in $state.parameters.PSObject.Properties) { $args += @('--' + $p.Name.Replace('_','-'),[string]$p.Value) }
  foreach ($a in @($state.assetBindings)) { if ($a.nodeId) { $args += @('--ref','node:' + [string]$a.nodeId) } }
  $r = Invoke-Router 'exec' $state.profileId 'canvas' $args
  $state.operationRef = @($r.operationRefs)[0]
  $state.nodeId = $r.cli.data.nodeId
  $state.stage = if($r.ok){'submitted'}else{'recoverable'}
   Save-Run $state | Out-Null; if ($r.ok) { Save-LastConfig $state }
  [pscustomobject]@{ status=$state.stage; state=$state; result=$r }
}

function Query-Plan {
  $state = Load-Run $RunId
  if ($state.adapter -eq 'conversation') {
    if (-not $state.submitId) { return [pscustomobject]@{ status='recoverable'; state=$state } }
    $r = Invoke-Router 'exec' $state.profileId 'conversation' @('query_result','--submit_id=' + [string]$state.submitId)
  } else {
    if (-not $state.operationRef) { return [pscustomobject]@{ status='recoverable'; state=$state } }
    $r = Invoke-Router 'exec' $state.profileId 'canvas' @('operation','status',[string]$state.operationRef,'--project-id',[string]$state.containerId)
  }
  [pscustomobject]@{ status='queried'; state=$state; result=$r }
}

try {
  Ensure-Dirs
  $out = switch ($Action) {
    'help' { [pscustomobject]@{ name='dreamina-video-flow'; adapters=@('canvas','conversation'); actions=@('discover-profiles','list-containers','resolve-assets','list-models','get-model-schema','validate-plan','prepare-video-config','submit','status','resume'); stateRoot=$script:RunRoot } }
    'discover-profiles' { Get-Profiles }
    'list-containers' { Get-ContainerList }
    'resolve-assets' { Resolve-Assets }
    'list-models' { Get-Models }
    'get-model-schema' { Get-ModelSchema }
     'validate-plan' { New-Plan }
     'prepare-video-config' { Prepare-VideoConfig }
    'submit' { Submit-Plan }
    'status' { Query-Plan }
    'resume' { Query-Plan }
  }
  Json $out
} catch {
  Json ([pscustomobject]@{ status='error'; message=$_.Exception.Message })
  exit 1
}
