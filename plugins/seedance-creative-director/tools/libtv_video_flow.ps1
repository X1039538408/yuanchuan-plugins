[CmdletBinding()]
param(
    [ValidateSet('discover-profiles','list-projects','resolve-assets','list-models','get-model-schema','validate-plan','prepare-video-config','submit','status','resume','help')]
    [string]$Action = 'help',
    [string]$Profile,
    [string]$Project,
    [string]$FolderId,
    [string]$Model,
    [string]$ModeType,
    [string]$Prompt,
    [string]$ParametersJson = '{}',
    [string]$AssetManifestJson = '[]',
    [string]$AssetRefsJson = '[]',
    [string]$RunId,
    [string]$TaskId,
    [switch]$Fresh,
    [switch]$Confirmed
)

$ErrorActionPreference = 'Stop'
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$script:FlowRoot = Join-Path $env:USERPROFILE '.seedance-director\video-flow'
$script:RunRoot = Join-Path $env:USERPROFILE '.seedance-director\video-runs'
$script:ProfileRoot = Join-Path $env:USERPROFILE '.libtv-accounts'
$script:LibTvExe = $null

function Get-LibTvExe {
    if ($script:LibTvExe) { return $script:LibTvExe }
    $command = Get-Command libtv -ErrorAction Stop
    $script:LibTvExe = $command.Source
    return $script:LibTvExe
}

function Select-FirstValue([object[]]$Values) {
    foreach ($value in $Values) {
        if ($null -ne $value -and [string]$value -ne '') { return $value }
    }
    return $null
}

function Ensure-FlowDirectories {
    New-Item -ItemType Directory -Force -Path $script:FlowRoot | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $script:FlowRoot 'cache') | Out-Null
    New-Item -ItemType Directory -Force -Path $script:RunRoot | Out-Null
}

function ConvertTo-PlainObject($Value) {
    if ($null -eq $Value) { return $null }
    if ($Value -is [string] -or $Value -is [ValueType]) { return $Value }
    if ($Value -is [System.Collections.IDictionary]) {
        $out = [ordered]@{}
        foreach ($key in $Value.Keys) { $out[$key] = ConvertTo-PlainObject $Value[$key] }
        return [pscustomobject]$out
    }
    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        return ,(@($Value | ForEach-Object { ConvertTo-PlainObject $_ }))
    }
    $result = [ordered]@{}
    foreach ($property in $Value.PSObject.Properties) {
        $result[$property.Name] = ConvertTo-PlainObject $property.Value
    }
    return [pscustomobject]$result
}

function ConvertFrom-FlowJson([string]$Raw) {
    if ([string]::IsNullOrWhiteSpace($Raw)) { return $null }
    try {
        return $Raw | ConvertFrom-Json
    } catch {
        $items = @()
        foreach ($line in ($Raw -split "`r?`n")) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { $items += ($line | ConvertFrom-Json) } catch { }
        }
        if ($items.Count -gt 0) { return $items }
        throw "Unable to parse JSON output: $($_.Exception.Message)"
    }
}

function Write-FlowJson($Value) {
    ConvertTo-PlainObject $Value | ConvertTo-Json -Depth 100
}

function Write-AtomicJson([string]$Path, $Value) {
    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $tmp = "$Path.tmp.$([guid]::NewGuid().ToString('N'))"
    (ConvertTo-PlainObject $Value | ConvertTo-Json -Depth 100) | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}

function Get-Sha256([string]$Text) {
    $bytes = [Text.Encoding]::UTF8.GetBytes($Text)
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
    return ([BitConverter]::ToString($hash) -replace '-', '').ToLowerInvariant()
}

function Get-FileSha256([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $hash = [Security.Cryptography.SHA256]::Create().ComputeHash([IO.File]::ReadAllBytes($Path))
    return ([BitConverter]::ToString($hash) -replace '-', '').ToLowerInvariant()
}

function Get-CachePath([string]$Kind, [string]$Key) {
    Ensure-FlowDirectories
    $safeKey = Get-Sha256 $Key
    return Join-Path (Join-Path $script:FlowRoot 'cache') "$Kind-$safeKey.json"
}

function Read-FlowCache([string]$Kind, [string]$Key, [int]$TtlSeconds, [switch]$Force) {
    $path = Get-CachePath $Kind $Key
    if ($Force -or -not (Test-Path -LiteralPath $path)) { return $null }
    try { $entry = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json } catch { return $null }
    $stored = [DateTimeOffset]::Parse($entry.storedAt)
    if (([DateTimeOffset]::UtcNow - $stored).TotalSeconds -gt $TtlSeconds) { return $null }
    if ($null -eq $entry.value) { return $null }
    return $entry.value
}

function Write-FlowCache([string]$Kind, [string]$Key, $Value) {
    Write-AtomicJson (Get-CachePath $Kind $Key) ([ordered]@{ storedAt = [DateTimeOffset]::UtcNow.ToString('o'); value = $Value })
}

function Get-RunPath([string]$Id) {
    Ensure-FlowDirectories
    return Join-Path $script:RunRoot "$Id.json"
}

function Get-RunState([string]$Id) {
    if ([string]::IsNullOrWhiteSpace($Id)) { throw 'runId is required' }
    $path = Get-RunPath $Id
    if (-not (Test-Path -LiteralPath $path)) { throw "Unknown runId: $Id" }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
}

function Save-RunState($State) {
    Write-AtomicJson (Get-RunPath $State.runId) $State
    return $State
}

function Get-AccountSummary($Info) {
    if ($null -eq $Info) { return $null }
    $active = $Info.activeAccount
    $member = if ($active) { $active.memberAccount } else { $null }
    return [pscustomobject]@{
        nickname = if ($Info.user) { $Info.user.nickname } else { $null }
        accountName = if ($active) { $active.accountName } else { $null }
        accountType = if ($active) { $active.accountType } else { $null }
        accountScope = if ($active -and ($active.accountType -eq 2 -or $active.ownerType -eq 2)) { 'team' } else { 'personal' }
        teamRole = if ($active) { $active.teamRole } else { $null }
        plan = if ($member) { $member.memberName } else { $null }
        accountLevel = if ($member) { $member.accountLevel } else { $null }
        teamId = $Info.teamId
        accountsCount = $Info.accountsCount
    }
}

function New-ProfileStatusRecord([string]$ProfileId, [string]$ConfigDir, $Info, [int]$ExitCode, [string]$Raw) {
    if ($ExitCode -ne 0 -or $null -eq $Info) {
        $message = Select-FirstValue @($Raw, "account info failed with exit code $ExitCode")
        return [pscustomobject]@{ profile = $ProfileId; configDir = $ConfigDir; status = 'error'; error = [string]$message }
    }
    return [pscustomobject]@{
        profile = $ProfileId
        configDir = $ConfigDir
        status = 'ready'
        accountInfo = $Info
        accountSummary = Get-AccountSummary $Info
        balance = if ($Info.activeAccount.memberAccount.memberName) { $Info.activeAccount.memberAccount.memberName } else { 'unavailable' }
    }
}

function New-FlowState([string]$RunId, [string]$Profile, [string]$Project, [string]$Prompt) {
    if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = [guid]::NewGuid().ToString('N') }
    return [pscustomobject][ordered]@{
        schemaVersion = 1
        runId = $RunId
        stage = 'prompt_ready'
        profile = $Profile
        projectUuid = $Project
        prompt = $Prompt
        promptSha256 = Get-Sha256 ([string](Select-FirstValue @($Prompt, '')))
        idempotencyKey = "seedance-$RunId"
        assetRefs = @()
        model = $null
        modelKey = $null
        schemaHash = $null
        schemaFetchedAt = $null
        modeType = $null
        parameters = [pscustomobject]@{}
        taskId = $null
        nodeKey = $null
        lastNode = $null
        createdAt = [DateTimeOffset]::UtcNow.ToString('o')
        updatedAt = [DateTimeOffset]::UtcNow.ToString('o')
        error = $null
    }
}

function Test-SchemaFingerprint($State, $SchemaResult) {
    if ($null -eq $State -or $null -eq $SchemaResult) { return $false }
    if ([string]::IsNullOrWhiteSpace([string]$State.schemaHash)) { return $false }
    return [string]$State.schemaHash -ne [string]$SchemaResult.schemaHash
}

function Refresh-RunSchema($State) {
    try {
        $fresh = Get-ModelSchema $State.profile $State.model -Force
    } catch {
        $State.error = "schema_refresh_failed: $($_.Exception.Message)"
        Save-RunState $State | Out-Null
        return [pscustomobject]@{ status = 'unavailable'; error = $State.error; state = $State }
    }
    if (Test-SchemaFingerprint $State $fresh) {
        $oldHash = $State.schemaHash
        $State.schemaHash = $fresh.schemaHash
        $State.schemaFetchedAt = $fresh.schemaFetchedAt
        $State.error = 'schema_changed_reconfirm'
        Save-RunState $State | Out-Null
        return [pscustomobject]@{ status = 'changed'; oldSchemaHash = $oldHash; newSchemaHash = $fresh.schemaHash; schema = $fresh; state = $State }
    }
    if (-not $State.schemaHash) {
        $State.schemaHash = $fresh.schemaHash
        $State.schemaFetchedAt = $fresh.schemaFetchedAt
        Save-RunState $State | Out-Null
    }
    return [pscustomobject]@{ status = 'unchanged'; schema = $fresh; state = $State }
}

function Test-FlowTransition([string]$From, [string]$To) {
    $allowed = @{
        prompt_ready = @('profile_selected','failed','recoverable')
        profile_selected = @('project_selected','failed','recoverable')
        project_selected = @('assets_resolved','failed','recoverable')
        assets_resolved = @('model_selected','failed','recoverable')
        model_selected = @('parameters_selected','failed','recoverable')
        parameters_selected = @('plan_validated','failed','recoverable')
        plan_validated = @('awaiting_final_confirmation','failed','recoverable')
        awaiting_final_confirmation = @('submitted','failed','recoverable')
        submitted = @('running','completed','failed','recoverable')
        running = @('completed','failed','recoverable')
        completed = @()
        failed = @('prompt_ready','awaiting_final_confirmation')
        recoverable = @('running','completed','failed','awaiting_final_confirmation')
    }
    return $allowed.ContainsKey($From) -and ($allowed[$From] -contains $To)
}

function Set-FlowStage($State, [string]$Stage) {
    if ($State.stage -ne $Stage -and -not (Test-FlowTransition $State.stage $Stage)) {
        throw "Illegal flow transition: $($State.stage) -> $Stage"
    }
    $State.stage = $Stage
    $State.updatedAt = [DateTimeOffset]::UtcNow.ToString('o')
    return $State
}

function Test-CanSubmit($State) {
    return $null -ne $State -and $State.stage -eq 'awaiting_final_confirmation' -and [string]::IsNullOrWhiteSpace([string]$State.taskId)
}

function Invoke-LibTv([string]$ProfileId, [string[]]$Arguments) {
    if ([string]::IsNullOrWhiteSpace($ProfileId)) { throw 'profile is required' }
    $profileDir = Join-Path $script:ProfileRoot $ProfileId
    if (-not (Test-Path -LiteralPath (Join-Path $profileDir 'credentials.json'))) {
        throw "Profile is not logged in: $ProfileId"
    }
    $exe = Get-LibTvExe
    $stderrPath = Join-Path $env:TEMP "seedance-libtv-$([guid]::NewGuid().ToString('N')).err"
    $previous = $env:LIBTV_CONFIG_DIR
    try {
        $env:LIBTV_CONFIG_DIR = $profileDir
        $stdout = @(& $exe @Arguments 2> $stderrPath | ForEach-Object { [string]$_ }) -join "`n"
        $exitCode = $LASTEXITCODE
        $stderr = if (Test-Path -LiteralPath $stderrPath) { Get-Content -Raw -LiteralPath $stderrPath } else { '' }
    } finally {
        if ($null -eq $previous) { Remove-Item Env:LIBTV_CONFIG_DIR -ErrorAction SilentlyContinue } else { $env:LIBTV_CONFIG_DIR = $previous }
        Remove-Item -LiteralPath $stderrPath -Force -ErrorAction SilentlyContinue
    }
    $json = $null
    if (-not [string]::IsNullOrWhiteSpace($stdout)) {
        try { $json = ConvertFrom-FlowJson $stdout } catch { }
    }
    [pscustomobject]@{ exitCode = $exitCode; stdout = $stdout; stderr = $stderr; json = $json }
}

function Get-ProfileStatuses([switch]$Force) {
    Ensure-FlowDirectories
    if (-not (Test-Path -LiteralPath $script:ProfileRoot)) { return @() }
    $cached = Read-FlowCache 'profiles' 'all' 600 -Force:$Force
    if ($cached) { return @($cached) }
    $exe = Get-LibTvExe
    if (-not (Get-Command Start-ThreadJob -ErrorAction SilentlyContinue)) {
        $sequential = @()
        foreach ($directory in (Get-ChildItem -LiteralPath $script:ProfileRoot -Directory)) {
            $credentialPath = Join-Path $directory.FullName 'credentials.json'
            if (-not (Test-Path -LiteralPath $credentialPath)) {
                $sequential += [pscustomobject]@{ profile = $directory.Name; configDir = $directory.FullName; status = 'not_logged_in' }
                continue
            }
            $old = $env:LIBTV_CONFIG_DIR
            try {
                $env:LIBTV_CONFIG_DIR = $directory.FullName
                $raw = @(& $exe account info 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
                $exitCode = $LASTEXITCODE
                $info = $null
                if (-not [string]::IsNullOrWhiteSpace($raw)) { try { $info = $raw | ConvertFrom-Json } catch { $info = $null } }
                $sequential += New-ProfileStatusRecord $directory.Name $directory.FullName $info $exitCode $raw
            } catch {
                $sequential += [pscustomobject]@{ profile = $directory.Name; configDir = $directory.FullName; status = 'error'; error = $_.Exception.Message }
            } finally {
                if ($null -eq $old) { Remove-Item Env:LIBTV_CONFIG_DIR -ErrorAction SilentlyContinue } else { $env:LIBTV_CONFIG_DIR = $old }
            }
        }
        $sorted = @($sequential | Sort-Object profile)
        Write-FlowCache 'profiles' 'all' $sorted
        return $sorted
    }
    $jobs = @()
    foreach ($directory in (Get-ChildItem -LiteralPath $script:ProfileRoot -Directory)) {
        $jobs += Start-ThreadJob -ArgumentList $exe, $directory.Name, $directory.FullName -ScriptBlock {
            param($Executable, $ProfileId, $ProfileDir)
            $credentialPath = Join-Path $ProfileDir 'credentials.json'
            if (-not (Test-Path -LiteralPath $credentialPath)) {
                return [pscustomobject]@{ profile = $ProfileId; configDir = $ProfileDir; status = 'not_logged_in' }
            }
            $old = $env:LIBTV_CONFIG_DIR
            try {
                $env:LIBTV_CONFIG_DIR = $ProfileDir
                $raw = @(& $Executable account info 2>&1 | ForEach-Object { [string]$_ }) -join "`n"
                $exitCode = $LASTEXITCODE
                $info = $null
                if (-not [string]::IsNullOrWhiteSpace($raw)) { try { $info = $raw | ConvertFrom-Json } catch { $info = $null } }
                if ($exitCode -ne 0 -or $null -eq $info) {
                    $errorText = if (-not [string]::IsNullOrWhiteSpace($raw)) { $raw } else { "account info failed with exit code $exitCode" }
                    return [pscustomobject]@{ profile = $ProfileId; configDir = $ProfileDir; status = 'error'; error = $errorText }
                }
                $active = $info.activeAccount
                $member = if ($active) { $active.memberAccount } else { $null }
                return [pscustomobject]@{ profile = $ProfileId; configDir = $ProfileDir; status = 'ready'; accountInfo = $info; accountSummary = [pscustomobject]@{ nickname = if ($info.user) { $info.user.nickname } else { $null }; accountName = if ($active) { $active.accountName } else { $null }; accountType = if ($active) { $active.accountType } else { $null }; accountScope = if ($active -and ($active.accountType -eq 2 -or $active.ownerType -eq 2)) { 'team' } else { 'personal' }; teamRole = if ($active) { $active.teamRole } else { $null }; plan = if ($member) { $member.memberName } else { $null }; accountLevel = if ($member) { $member.accountLevel } else { $null }; teamId = $info.teamId; accountsCount = $info.accountsCount }; balance = if ($member.memberName) { $member.memberName } else { 'unavailable' } }
            } catch {
                return [pscustomobject]@{ profile = $ProfileId; configDir = $ProfileDir; status = 'error'; error = $_.Exception.Message }
            } finally {
                if ($null -eq $old) { Remove-Item Env:LIBTV_CONFIG_DIR -ErrorAction SilentlyContinue } else { $env:LIBTV_CONFIG_DIR = $old }
            }
        }
    }
    if ($jobs.Count -gt 0) { Wait-Job -Job $jobs | Out-Null }
    $results = @($jobs | ForEach-Object { Receive-Job -Job $_ -ErrorAction SilentlyContinue; Remove-Job -Job $_ -Force -ErrorAction SilentlyContinue })
    $sortedResults = @($results | Sort-Object profile)
    Write-FlowCache 'profiles' 'all' $sortedResults
    return $sortedResults
}

function Get-ProjectResultStatus([bool]$UsedCachedFolder, [int]$ProjectCount) {
    if ($ProjectCount -gt 0) { return 'ready' }
    if ($UsedCachedFolder) { return 'folder_mapping_invalid' }
    return 'no_projects'
}

function Get-ProjectList([string]$ProfileId, [string]$Folder, [switch]$Force) {
    $usedCachedFolder = $false
    if ([string]::IsNullOrWhiteSpace($Folder)) {
        $folderCache = Read-FlowCache 'folder' $ProfileId 31536000
        if ($folderCache) { $Folder = [string]$folderCache.folderId; $usedCachedFolder = $true }
    }
    if ([string]::IsNullOrWhiteSpace($Folder)) {
        return [pscustomobject]@{ status = 'folder_required'; profile = $ProfileId; projects = @() }
    }
    $cacheKey = "$ProfileId|$Folder"
    $cached = Read-FlowCache 'projects' $cacheKey 60 -Force:$Force
    if ($cached) { return $cached }
    $all = @()
    for ($page = 1; $page -le 20; $page++) {
        $result = Invoke-LibTv $ProfileId @('project','list','-p', $page, '-s', '100', '-o', 'updated_at_desc')
        if ($result.exitCode -ne 0) { throw "project list failed: $($result.stderr)" }
        $payload = $result.json
        $items = if ($payload.projectMetaList) { @($payload.projectMetaList) } elseif ($payload.projects) { @($payload.projects) } else { @($payload) }
        if ($items.Count -eq 0) { break }
        $all += $items
        $total = [int](Select-FirstValue @($payload.total, $all.Count))
        if ($all.Count -ge $total -or $items.Count -lt 100) { break }
    }
    $selected = @($all | Where-Object { [string]$_.folderId -eq [string]$Folder } | Sort-Object @{Expression={ [int64](Select-FirstValue @($_.updatedAtMs, $_.editTimeMs, 0)) }; Descending=$true} | Select-Object -First 6)
    $selectionStatus = Get-ProjectResultStatus $usedCachedFolder $selected.Count
    $value = [pscustomobject]@{ status = $selectionStatus; profile = $ProfileId; folderId = [string]$Folder; count = $selected.Count; projects = @($selected) }
    Write-FlowCache 'projects' $cacheKey $value
    if (-not (Read-FlowCache 'folder' $ProfileId 31536000)) { Write-FlowCache 'folder' $ProfileId ([pscustomobject]@{ folderId = [string]$Folder; confirmedAt = [DateTimeOffset]::UtcNow.ToString('o') }) }
    return $value
}

function Get-ModelCatalog([string]$ProfileId, [string]$Query, [switch]$Force) {
    $cacheKey = "$ProfileId|$Query"
    $cached = Read-FlowCache 'models' $cacheKey 600 -Force:$Force
    if ($cached) { return $cached }
    $args = @('model','search','-t','video')
    if (-not [string]::IsNullOrWhiteSpace($Query)) { $args += $Query }
    $result = Invoke-LibTv $ProfileId $args
    if ($result.exitCode -ne 0) { throw "model search failed: $($result.stderr)" }
    $value = [pscustomobject]@{ status = 'ready'; profile = $ProfileId; query = $Query; models = @(Select-FirstValue @($result.json.matches, $result.json)) }
    Write-FlowCache 'models' $cacheKey $value
    return $value
}

function Get-ModelSchema([string]$ProfileId, [string]$ModelName, [switch]$Force) {
    if ([string]::IsNullOrWhiteSpace($ModelName)) { throw 'model is required' }
    $cacheKey = "$ProfileId|$ModelName"
    $cached = Read-FlowCache 'schema' $cacheKey 600 -Force:$Force
    if ($cached -and $cached.modelKey) { return $cached }
    $result = Invoke-LibTv $ProfileId @('model', $ModelName)
    if ($result.exitCode -ne 0) { throw "model schema failed: $($result.stderr)" }
    $rawSchema = if ($result.json -is [array] -and $result.json.Count -eq 1) { $result.json[0] } else { $result.json }
    $schema = if ($rawSchema.schema -and $rawSchema.schema.properties) { $rawSchema.schema } else { $rawSchema }
    $schemaText = $schema | ConvertTo-Json -Depth 100 -Compress
    $modelKey = [string](Select-FirstValue @($result.json.modelKey, $schema.modelKey))
    $value = [pscustomobject]@{ status = 'ready'; profile = $ProfileId; modelName = $ModelName; modelKey = $modelKey; schemaHash = Get-Sha256 $schemaText; schemaFetchedAt = [DateTimeOffset]::UtcNow.ToString('o'); schema = $schema }
    Write-FlowCache 'schema' $cacheKey $value
    return $value
}

function Get-LastVideoConfig {
    $path = Join-Path $script:FlowRoot 'last-config.json'
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { return (Get-Content -LiteralPath $path -Raw -Encoding utf8 | ConvertFrom-Json) } catch { return $null }
}

function Save-LastVideoConfig($State) {
    $safe = [ordered]@{
        provider = 'libtv'; profileId = $State.profile; projectId = $State.projectUuid
        model = $State.model; modeType = $State.modeType; parameters = $State.parameters
        assetRefs = $State.assetRefs; updatedAt = [DateTimeOffset]::UtcNow.ToString('o')
    }
    Write-AtomicJson (Join-Path $script:FlowRoot 'last-config.json') $safe
}

function Prepare-VideoConfig {
    Ensure-FlowDirectories
    $profiles = @(Get-ProfileStatuses -Force:$Fresh)
    if (-not $Profile) {
        return [pscustomobject]@{ status = 'needs_profile'; provider = 'libtv'; profiles = $profiles; cachedConfig = Get-LastVideoConfig }
    }
    $projects = Get-ProjectList $Profile $FolderId -Force:$Fresh
    $models = Get-ModelCatalog $Profile $Model -Force:$Fresh
    $schema = $null
    $firstModel = @($models.models | Select-Object -First 1)
    if ($firstModel.Count -gt 0) {
        $name = [string](Select-FirstValue @($firstModel[0].modelName, $firstModel[0].name, $firstModel[0].model))
        if ($name) { $schema = Get-ModelSchema $Profile $name -Force:$Fresh }
    }
    [pscustomobject]@{
        status = 'ready'; provider = 'libtv'; profile = $Profile; profiles = $profiles
        projects = $projects; models = $models; schema = $schema
        cachedConfig = Get-LastVideoConfig
        cache = [pscustomobject]@{ source = 'controller'; generatedAt = [DateTimeOffset]::UtcNow.ToString('o'); ttlSeconds = 600 }
        capabilityHash = (Get-Sha256 ((ConvertTo-Json @($models,$schema) -Depth 50)))
    }
}

function Get-EnumValues($Definition) {
    if ($null -eq $Definition -or $null -eq $Definition.enum) { return @() }
    return @($Definition.enum | ForEach-Object { if ($_.PSObject.Properties.Name -contains 'value') { $_.value } else { $_ } })
}

function Get-SettingFields($Schema, [string]$ModeType) {
    $settings = $Schema.config.settings
    if ($null -eq $settings) { return @() }
    if ($settings -is [System.Collections.IEnumerable] -and $settings -isnot [string]) {
        return @($settings | ForEach-Object { [string]$_ })
    }
    if ($settings.PSObject.Properties.Name -contains $ModeType) {
        return @($settings.$ModeType | ForEach-Object { [string]$_ })
    }
    return @()
}

function Get-RequiredFieldNames($Schema) {
    $required = @()
    foreach ($candidate in @($Schema.required, $Schema.requiredFields)) {
        if ($null -ne $candidate) { $required += @($candidate | ForEach-Object { [string]$_ }) }
    }
    if ($Schema.properties) {
        foreach ($property in $Schema.properties.PSObject.Properties) {
            if ($property.Value.required -eq $true) { $required += $property.Name }
        }
    }
    return @($required | Where-Object { $_ } | Select-Object -Unique)
}

function Get-DynamicParameterOptions($Schema, [string]$ModeType) {
    $properties = $Schema.properties
    $fields = @(Get-SettingFields $Schema $ModeType)
    if ($fields.Count -eq 0 -and $properties) { $fields = @($properties.PSObject.Properties.Name | Where-Object { $_ -notin @('modeType','prompt') }) }
    $requiredFields = @(Get-RequiredFieldNames $Schema)
    $options = [ordered]@{}
    foreach ($field in $fields) {
        $definition = if ($properties.PSObject.Properties.Name -contains $field) { $properties.$field } else { $null }
        $isRequired = $requiredFields -contains $field
        if ($null -eq $definition) { $options[$field] = [pscustomobject]@{ unsupported = $true; required = $isRequired }; continue }
        $entry = [ordered]@{ required = $isRequired; displayName = (Select-FirstValue @($definition.displayName, $field)) }
        $values = Get-EnumValues $definition
        if ($values.Count -gt 0) { $entry.values = $values }
        if ($null -ne $definition.min) { $entry.min = $definition.min }
        if ($null -ne $definition.max) { $entry.max = $definition.max }
        if ($null -ne $definition.step) { $entry.step = $definition.step }
        if ($null -ne $definition.default) { $entry.default = $definition.default }
        $options[$field] = [pscustomobject]$entry
    }
    return [pscustomobject][ordered]@{ modeType = $ModeType; options = [pscustomobject]$options }
}

function Test-GenerationPlan($Schema, [string]$ModeType, $Parameters, [string]$Prompt, $AssetRefs = @()) {
    if ($null -eq $Parameters) { $Parameters = [pscustomobject]@{} }
    $properties = $Schema.properties
    $modeDef = if ($properties.PSObject.Properties.Name -contains 'modeType') { $properties.modeType } else { $null }
    $supportedModes = if ($modeDef -and $modeDef.items) { @($modeDef.items.PSObject.Properties.Name) } else { @() }
    if ($supportedModes.Count -gt 0 -and $supportedModes -notcontains $ModeType) {
        return [pscustomobject]@{ valid = $false; reason = "Unsupported modeType: $ModeType" }
    }
    $assetCheck = Test-ModeAssetConstraints -Schema $Schema -ModeType $ModeType -AssetRefs $AssetRefs
    if (-not $assetCheck.valid) { return $assetCheck }
    if ([string]::IsNullOrWhiteSpace($Prompt)) {
        $rules = @($Schema.rules)
        $requiresPrompt = $rules | Where-Object { ($_.require -contains 'prompt') -and ($_.mode -ne 'any' -or $_.require.Count -eq 1) }
        if ($requiresPrompt.Count -gt 0) { return [pscustomobject]@{ valid = $false; reason = 'Prompt is required by model schema' } }
    }
    $options = Get-DynamicParameterOptions $Schema $ModeType
    foreach ($property in $options.options.PSObject.Properties) {
        $key = $property.Name
        $definition = $property.Value
        if ($definition.unsupported) {
            if ($definition.required) { return [pscustomobject]@{ valid = $false; reason = "Unsupported required field: $key" } }
            continue
        }
        $hasValue = $Parameters.PSObject.Properties.Name -contains $key
        $value = if ($hasValue) { $Parameters.$key } elseif ($null -ne $definition.default) { $definition.default } else { $null }
        if (-not $hasValue -and $null -eq $value -and $definition.required) { return [pscustomobject]@{ valid = $false; reason = "Missing required field: $key" } }
        if ($null -eq $value) { continue }
        if ($definition.values -and @($definition.values) -notcontains $value) { return [pscustomobject]@{ valid = $false; reason = "Unsupported value for ${key}: $value" } }
        if ($null -ne $definition.min) {
            try { if ([double]$value -lt [double]$definition.min) { return [pscustomobject]@{ valid = $false; reason = "$key is below minimum" } } } catch { return [pscustomobject]@{ valid = $false; reason = "Invalid numeric value for $key" } }
        }
        if ($null -ne $definition.max) {
            try { if ([double]$value -gt [double]$definition.max) { return [pscustomobject]@{ valid = $false; reason = "$key is above maximum" } } } catch { return [pscustomobject]@{ valid = $false; reason = "Invalid numeric value for $key" } }
        }
    }
    return [pscustomobject]@{ valid = $true; reason = $null; options = $options }
}

function Get-AssetType($Asset) {
    $value = Select-FirstValue @($Asset.type, $Asset.nodeType, $Asset.kind, $Asset.mediaType)
    if ($null -eq $value) { return 'unknown' }
    return ([string]$value).ToLowerInvariant() -replace '[-_ ]', ''
}

function Test-ModeAssetConstraints($Schema, [string]$ModeType, $AssetRefs = @()) {
    $modeDef = if ($Schema.properties -and ($Schema.properties.PSObject.Properties.Name -contains 'modeType')) { $Schema.properties.modeType } else { $null }
    if ($null -eq $modeDef -or $null -eq $modeDef.items -or -not ($modeDef.items.PSObject.Properties.Name -contains $ModeType)) {
        return [pscustomobject]@{ valid = $true; reason = $null }
    }
    $range = @($modeDef.items.$ModeType)
    $count = @($AssetRefs).Count
    $min = if ($range.Count -gt 0) { [int]$range[0] } else { 0 }
    $max = if ($range.Count -gt 1) { [int]$range[1] } else { $min }
    if ($count -lt $min -or $count -gt $max) {
        return [pscustomobject]@{ valid = $false; reason = "Asset count $count is outside $ModeType range [$min,$max]" }
    }
    $config = $modeDef.mixed2videoConfig
    if ($ModeType -eq 'mixed2video' -and $config) {
        foreach ($limit in @('imageMax','videoMax','audioMax')) {
            if ($null -eq $config.$limit) { continue }
            $kind = $limit -replace 'Max$',''
            $actual = @($AssetRefs | Where-Object { (Get-AssetType $_) -eq $kind.ToLowerInvariant() }).Count
            if ($actual -gt [int]$config.$limit) {
                return [pscustomobject]@{ valid = $false; reason = "$kind asset count $actual exceeds $ModeType limit $($config.$limit)" }
            }
        }
    }
    if ($ModeType -eq 'videoEdit2video' -and $modeDef.videoEdit2videoConfig) {
        $videoMax = $modeDef.videoEdit2videoConfig.videoMax
        if ($null -ne $videoMax) {
            $actualVideo = @($AssetRefs | Where-Object { (Get-AssetType $_) -eq 'video' }).Count
            if ($actualVideo -gt [int]$videoMax) {
                return [pscustomobject]@{ valid = $false; reason = "video asset count $actualVideo exceeds videoEdit2video limit $videoMax" }
            }
        }
    }
    return [pscustomobject]@{ valid = $true; reason = $null }
}

function Get-ManifestHash($Item) {
    $declared = Select-FirstValue @($Item.sha256, $Item.fileHash, $Item.hash, $Item.assetHash)
    if ($declared) { return ([string]$declared).ToLowerInvariant() }
    $path = Select-FirstValue @($Item.path, $Item.file, $Item.filePath, $Item.localPath)
    if ($path) { return Get-FileSha256 ([string]$path) }
    return $null
}

function Get-NodeHashCandidates($Node) {
    $values = @()
    foreach ($name in @('sha256','fileHash','hash','assetHash','md5')) {
        if ($Node.PSObject.Properties.Name -contains $name -and $Node.$name) { $values += [string]$Node.$name }
    }
    foreach ($containerName in @('data','meta','asset','resource')) {
        if ($Node.PSObject.Properties.Name -contains $containerName -and $Node.$containerName) {
            $container = $Node.$containerName
            foreach ($name in @('sha256','fileHash','hash','assetHash','md5')) {
                if ($container.PSObject.Properties.Name -contains $name -and $container.$name) { $values += [string]$container.$name }
            }
        }
    }
    return @($values | Where-Object { $_ } | ForEach-Object { $_.ToLowerInvariant() } | Select-Object -Unique)
}

function Resolve-AssetRefs([string]$ProfileId, [string]$ProjectUuid, [string]$Prompt, $Manifest) {
    $matches = [regex]::Matches([string](Select-FirstValue @($Prompt, '')), '\{\{Mixed\s+(\d+)\}\}') | ForEach-Object { [int]$_.Groups[1].Value } | Select-Object -Unique
    if ($matches.Count -eq 0) { return [pscustomobject]@{ status = 'ready'; refs = @() } }
    $projectResult = Invoke-LibTv $ProfileId @('project', $ProjectUuid)
    if ($projectResult.exitCode -ne 0) { throw "project detail failed: $($projectResult.stderr)" }
    $nodeList = @()
    if ($projectResult.json.nodes) { $nodeList = @($projectResult.json.nodes) }
    elseif ($projectResult.json.nodeList) { $nodeList = @($projectResult.json.nodeList) }
    $refs = @(); $unresolved = @()
    foreach ($index in $matches) {
        $item = @($Manifest | Where-Object { [int]$_.index -eq $index }) | Select-Object -First 1
        $name = if ($item) { [string](Select-FirstValue @($item.name, $item.displayName, $item.nodeName)) } else { '' }
        $manifestHash = if ($item) { Get-ManifestHash $item } else { $null }
        $explicitKey = if ($item) { [string](Select-FirstValue @($item.nodeKey, $item.nodeId, $item.uuid, $item.id)) } else { '' }
        $candidates = @()
        if ($explicitKey) {
            $candidates = @($nodeList | Where-Object { [string](Select-FirstValue @($_.nodeKey, $_.id, $_.uuid, $_.key)) -eq $explicitKey })
        }
        if ($manifestHash) {
            if ($candidates.Count -eq 0) { $candidates = @($nodeList | Where-Object { (Get-NodeHashCandidates $_) -contains $manifestHash }) }
        }
        if ($candidates.Count -eq 0 -and $name) {
            $candidates = @($nodeList | Where-Object { (Select-FirstValue @($_.name, $_.displayName, $_.nodeName, $_.title)) -eq $name })
        }
        if ($candidates.Count -eq 1) {
            $node = $candidates[0]
            $key = [string](Select-FirstValue @($node.nodeKey, $node.id, $node.uuid, $node.key))
            $matchMethod = if ($explicitKey) { 'explicit' } elseif ($manifestHash) { 'sha256' } else { 'name' }
            $refs += [pscustomobject]@{ index = $index; nodeKey = $key; name = $name; type = (Select-FirstValue @($node.type, $node.nodeType)); match = $matchMethod; sourceHash = $manifestHash }
        } else {
            $unresolved += [pscustomobject]@{ index = $index; name = $name; sourceHash = $manifestHash; candidates = $candidates }
        }
    }
    if ($unresolved.Count -gt 0) { return [pscustomobject]@{ status = 'needs_asset_choice'; refs = @($refs); unresolved = @($unresolved) } }
    return [pscustomobject]@{ status = 'ready'; refs = @($refs) }
}

function Invoke-ValidatePlan {
    Ensure-FlowDirectories
    $parameters = ConvertFrom-FlowJson $ParametersJson
    $assetRefs = @(ConvertFrom-FlowJson $AssetRefsJson)
    $schemaResult = Get-ModelSchema $Profile $Model
    $result = Test-GenerationPlan $schemaResult.schema $ModeType $parameters $Prompt $assetRefs
    if (-not $result.valid) { return [pscustomobject]@{ status = 'invalid'; runId = $RunId; validation = $result } }
    $state = New-FlowState $RunId $Profile $Project $Prompt
    $state.model = $Model
    $state.modelKey = $schemaResult.modelKey
    $state.schemaHash = $schemaResult.schemaHash
    $state.schemaFetchedAt = $schemaResult.schemaFetchedAt
    $state.modeType = $ModeType
    $state.parameters = $parameters
    $state.assetRefs = $assetRefs
    Set-FlowStage $state 'profile_selected' | Out-Null
    Set-FlowStage $state 'project_selected' | Out-Null
    Set-FlowStage $state 'assets_resolved' | Out-Null
    Set-FlowStage $state 'model_selected' | Out-Null
    Set-FlowStage $state 'parameters_selected' | Out-Null
    Set-FlowStage $state 'plan_validated' | Out-Null
    Set-FlowStage $state 'awaiting_final_confirmation' | Out-Null
    Save-RunState $state | Out-Null
    return [pscustomobject]@{ status = 'valid'; runId = $state.runId; stage = $state.stage; validation = $result; state = $state }
}

function Get-NodeLifecycleStage($Node) {
    if ($null -eq $Node) { return $null }
    if ($Node -is [System.Array] -and $Node.Count -eq 1) { $Node = $Node[0] }
    if ($Node.PSObject.Properties.Name -contains 'nodes' -and $Node.nodes) {
        $nested = @($Node.nodes)
        if ($nested.Count -gt 0) { return Get-NodeLifecycleStage $nested[0] }
    }
    $values = @()
    foreach ($name in @('status','state','taskStatus','generationStatus','jobStatus')) {
        if ($Node.PSObject.Properties.Name -contains $name -and $Node.$name) { $values += [string]$Node.$name }
    }
    foreach ($containerName in @('task','job','generation')) {
        if ($Node.PSObject.Properties.Name -contains $containerName -and $Node.$containerName) {
            $container = $Node.$containerName
            foreach ($name in @('status','state','taskStatus','generationStatus','jobStatus')) {
                if ($container.PSObject.Properties.Name -contains $name -and $container.$name) { $values += [string]$container.$name }
            }
        }
    }
    foreach ($raw in $values) {
        $value = $raw.ToLowerInvariant()
        if ($value -match '^(completed|complete|success|succeeded|done|finished)$') { return 'completed' }
        if ($value -match '^(failed|failure|error|cancelled|canceled|aborted)$') { return 'failed' }
        if ($value -match '^(queued|pending|processing|running|generating|submitted|in_progress|in-progress)$') { return 'running' }
    }
    return $null
}

function Get-ResumeDecision($State) {
    switch ([string]$State.stage) {
        'prompt_ready' { return 'restart_selection' }
        'profile_selected' { return 'restart_selection' }
        'project_selected' { return 'restart_selection' }
        'assets_resolved' { return 'restart_selection' }
        'model_selected' { return 'restart_selection' }
        'parameters_selected' { return 'restart_selection' }
        'plan_validated' { return 'show_confirmation' }
        'awaiting_final_confirmation' { return 'show_confirmation' }
        'submitted' { return 'query_original' }
        'running' { return 'query_original' }
        'recoverable' { return 'query_original' }
        'completed' { return 'show_result' }
        'failed' { return 'show_error' }
        default { return 'restart_selection' }
    }
}

function Invoke-SubmitRun {
    $state = Get-RunState $RunId
    if (-not (Test-CanSubmit $state)) { return [pscustomobject]@{ status = 'not_submittable'; state = $state } }
    if (-not $Confirmed) { return [pscustomobject]@{ status = 'confirmation_required'; state = $state } }
    $schemaRefresh = Refresh-RunSchema $state
    if ($schemaRefresh.status -eq 'changed') { return [pscustomobject]@{ status = 'schema_changed'; state = $state; oldSchemaHash = $schemaRefresh.oldSchemaHash; newSchemaHash = $schemaRefresh.newSchemaHash } }
    if ($schemaRefresh.status -eq 'unavailable') { return [pscustomobject]@{ status = 'recoverable'; state = $state; error = $schemaRefresh.error } }
    $nodeName = "Seedance-$($state.runId)"
    $args = @('node','create',$nodeName,'-p',$state.projectUuid,'-t','video','--prompt',$state.prompt,'-s',"model=$($state.model)",' -s'.Trim(),"modeType=$($state.modeType)")
    foreach ($property in $state.parameters.PSObject.Properties) { $args += @('-s', "$($property.Name)=$($property.Value)") }
    foreach ($ref in @($state.assetRefs)) { if ($ref.nodeKey) { $args += @('--left', [string]$ref.nodeKey) } }
    $args += '-r'
    $result = Invoke-LibTv $state.profile $args
    if ($result.exitCode -ne 0) {
        $state.error = $result.stderr
        Set-FlowStage $state 'recoverable' | Out-Null
        Save-RunState $state | Out-Null
        return [pscustomobject]@{ status = 'recoverable'; state = $state; error = $result.stderr }
    }
    $payload = $result.json
    $nodeKey = [string](Select-FirstValue @($payload.newNodeKey, $payload.nodeKey, $payload.id, $payload.key))
    $state.nodeKey = $nodeKey
    $state.taskId = [string](Select-FirstValue @($payload.taskId, $payload.task_id, $nodeKey))
    $state.lastNode = $payload
    Set-FlowStage $state 'submitted' | Out-Null
    Save-RunState $state | Out-Null
    Save-LastVideoConfig $state
    return [pscustomobject]@{ status = 'submitted'; state = $state; result = $payload }
}

function Invoke-StatusRun {
    $state = Get-RunState $RunId
    $decision = Get-ResumeDecision $state
    if ($decision -in @('restart_selection','show_confirmation','show_result','show_error')) {
        return [pscustomobject]@{ status = $decision; state = $state; node = $state.lastNode }
    }
    $target = if ($state.nodeKey) { $state.nodeKey } else { "Seedance-$($state.runId)" }
    $result = Invoke-LibTv $state.profile @('node',$target,'-p',$state.projectUuid)
    if ($result.exitCode -ne 0) {
        $state.error = $result.stderr
        Set-FlowStage $state 'recoverable' | Out-Null
        Save-RunState $state | Out-Null
        return [pscustomobject]@{ status = 'recoverable'; state = $state; error = $result.stderr }
    }
    $state.lastNode = $result.json
    $nextStage = Get-NodeLifecycleStage $result.json
    if ($nextStage -and $nextStage -ne $state.stage -and (Test-FlowTransition $state.stage $nextStage)) {
        Set-FlowStage $state $nextStage | Out-Null
    }
    $state.updatedAt = [DateTimeOffset]::UtcNow.ToString('o')
    Save-RunState $state | Out-Null
    return [pscustomobject]@{ status = 'queried'; stage = $state.stage; state = $state; node = $result.json }
}

function Invoke-ResumeRun {
    $state = Get-RunState $RunId
    $decision = Get-ResumeDecision $state
    if ($decision -eq 'show_confirmation') {
        $schemaRefresh = Refresh-RunSchema $state
        if ($schemaRefresh.status -eq 'changed') { return [pscustomobject]@{ status = 'schema_changed'; state = $state; oldSchemaHash = $schemaRefresh.oldSchemaHash; newSchemaHash = $schemaRefresh.newSchemaHash } }
        if ($schemaRefresh.status -eq 'unavailable') { return [pscustomobject]@{ status = 'recoverable'; state = $state; error = $schemaRefresh.error } }
    }
    if ($decision -eq 'query_original') { return Invoke-StatusRun }
    return [pscustomobject]@{ status = $decision; state = $state; node = $state.lastNode }
}

function Show-Help {
    [pscustomobject]@{
        name = 'seedance-video-flow'
        actions = @('discover-profiles','list-projects','resolve-assets','list-models','get-model-schema','validate-plan','prepare-video-config','submit','status','resume')
        stateRoot = $script:FlowRoot
        runRoot = $script:RunRoot
        note = 'Uses local LIBTV_CONFIG_DIR profiles and libtv CLI. Does not read or emit credentials.'
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    try {
        Ensure-FlowDirectories
        $output = switch ($Action) {
            'discover-profiles' { [pscustomobject]@{ status = 'ready'; profiles = @(Get-ProfileStatuses -Force:$Fresh) } }
            'list-projects' { Get-ProjectList $Profile $FolderId -Force:$Fresh }
            'resolve-assets' { Resolve-AssetRefs $Profile $Project $Prompt (ConvertFrom-FlowJson $AssetManifestJson) }
            'list-models' { Get-ModelCatalog $Profile $Model -Force:$Fresh }
            'get-model-schema' { Get-ModelSchema $Profile $Model -Force:$Fresh }
            'validate-plan' { Invoke-ValidatePlan }
            'prepare-video-config' { Prepare-VideoConfig }
            'submit' { Invoke-SubmitRun }
            'status' { Invoke-StatusRun }
            'resume' { Invoke-ResumeRun }
            default { Show-Help }
        }
        Write-FlowJson $output
    } catch {
        Write-FlowJson ([pscustomobject]@{ status = 'error'; errorType = $_.Exception.GetType().Name; message = $_.Exception.Message })
        exit 1
    }
}
