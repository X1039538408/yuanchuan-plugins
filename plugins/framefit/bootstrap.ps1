# Windows PowerShell 5.1 entry point. The consumer needs no Python or Node install.
param([string]$StateDir = (Join-Path $env:LOCALAPPDATA 'FrameFitCodex'))
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Net.Http
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ChannelUrl = 'https://raw.githubusercontent.com/X1039538408/yuanchuan-plugins/main/plugins/framefit/channels/stable.json'
$VersionPattern = '^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?$'
$StateDir = [IO.Path]::GetFullPath($StateDir)
[IO.Directory]::CreateDirectory($StateDir) | Out-Null

function Read-Json([string]$Path) { Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json }
function Get-Sha256([string]$Path) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hasher.Dispose() }
}
function Remove-StateDirectory([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $prefix = $StateDir.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Cleanup path is outside FrameFit state directory' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
function Get-SafeRelative([string]$Name) {
    $normalized = $Name.Replace('\', '/').TrimEnd('/')
    if ([string]::IsNullOrEmpty($normalized) -or $normalized.StartsWith('/') -or $normalized.Contains(':')) { throw 'Unsafe archive path' }
    foreach ($part in $normalized.Split('/')) {
        if ($part -in @('', '.', '..') -or $part -match '[. ]$' -or $part -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') { throw 'Unsafe Windows archive path' }
    }
    return $normalized
}
function Download-File([string]$Url, [string]$Path, [long]$Limit) {
    $handler = New-Object Net.Http.HttpClientHandler
    $handler.AllowAutoRedirect = $false
    $client = New-Object Net.Http.HttpClient($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(120)
    try {
        for ($hop = 0; $hop -lt 6; $hop++) {
            $uri = [Uri]$Url
            if ($uri.Scheme -ne 'https' -or $uri.Host -notin @('github.com','raw.githubusercontent.com','release-assets.githubusercontent.com','objects.githubusercontent.com')) { throw 'Untrusted download URL' }
            $response = $client.GetAsync($uri, [Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
            if ([int]$response.StatusCode -ge 300 -and [int]$response.StatusCode -lt 400) {
                $Url = ([Uri]::new($uri, $response.Headers.Location)).AbsoluteUri
                $response.Dispose()
                continue
            }
            $response.EnsureSuccessStatusCode() | Out-Null
            $inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
            $outputStream = [IO.File]::Create($Path)
            try {
                $buffer = New-Object byte[] (1024*1024)
                [long]$count = 0
                while (($read = $inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                    $count += $read
                    if ($count -gt $Limit) { throw 'Download exceeds size limit' }
                    $outputStream.Write($buffer,0,$read)
                }
            } finally { $outputStream.Dispose(); $inputStream.Dispose(); $response.Dispose() }
            return
        }
        throw 'Too many download redirects'
    } finally { $client.Dispose(); $handler.Dispose() }
}
function Verify-Runtime([string]$Root, [string]$Version) {
    $runtime = Read-Json (Join-Path $Root 'runtime.json')
    if ($runtime.version -ne $Version -or $runtime.apiVersion -ne 1 -or -not $runtime.files) { throw 'Runtime version or manifest mismatch' }
    $names = @($runtime.files.PSObject.Properties.Name)
    foreach ($required in @('host/FrameFitHost.exe','worker/FrameFitWorker.exe','bin/ffmpeg.exe','bin/ffprobe.exe','panel.html','shared-presets.json')) {
        if ($required -notin $names) { throw "Missing runtime file: $required" }
    }
    $actual = @(Get-ChildItem -LiteralPath $Root -Recurse -File | Where-Object { $_.FullName -ne (Join-Path $Root 'runtime.json') })
    if ($actual.Count -ne $names.Count) { throw 'Runtime file inventory mismatch' }
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Runtime reparse point rejected' }
    }
    foreach ($entry in $runtime.files.PSObject.Properties) {
        $relative = Get-SafeRelative $entry.Name
        if ($entry.Value -notmatch '^[a-f0-9]{64}$') { throw 'Invalid file checksum' }
        $hash = Get-Sha256 (Join-Path $Root $relative)
        if ($hash -ne $entry.Value) { throw "Runtime verification failed: $relative" }
    }
}
function Get-Cached {
    try {
        $active = Read-Json (Join-Path $StateDir 'active.json')
        if ($active.version -notmatch $VersionPattern) { return $null }
        $root = Join-Path (Join-Path $StateDir 'releases') $active.version
        Verify-Runtime $root $active.version
        return @{ root = $root; version = $active.version; sha256 = $active.sha256 }
    } catch { return $null }
}
function Expand-Safe([string]$Archive, [string]$Dest) {
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $seen = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        [long]$expanded = 0
        foreach ($entry in $zip.Entries) {
            $name = Get-SafeRelative $entry.FullName
            if (-not $seen.Add($name) -or (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Duplicate path or symlink in release' }
            $expanded += $entry.Length
            if ($expanded -gt 4GB) { throw 'Expanded release exceeds size limit' }
            $target = Join-Path $Dest $name
            if ($entry.FullName.EndsWith('/')) { [IO.Directory]::CreateDirectory($target) | Out-Null; continue }
            [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    } finally { $zip.Dispose() }
}
$lock = Join-Path $StateDir 'update.lock'
$deadline = [DateTime]::UtcNow.AddSeconds(120)
while ($true) {
    try { New-Item -ItemType Directory -Path $lock -ErrorAction Stop | Out-Null; break } catch {
        if ((Test-Path -LiteralPath $lock) -and ((Get-Item -LiteralPath $lock).LastWriteTimeUtc -lt [DateTime]::UtcNow.AddMinutes(-30))) { Remove-StateDirectory $lock; continue }
        if ([DateTime]::UtcNow -gt $deadline) { throw 'Another FrameFit update is still running' }
        Start-Sleep -Milliseconds 200
    }
}
$scratch = Join-Path $StateDir ('download-' + [Guid]::NewGuid().ToString('N'))
$selected = $null
try {
    $cached = Get-Cached
    try {
        [IO.Directory]::CreateDirectory($scratch) | Out-Null
        $channelFile = Join-Path $scratch 'channel.json'
        Download-File $ChannelUrl $channelFile 65536
        $release = Read-Json $channelFile
        if ($release.version -notmatch $VersionPattern -or $release.apiVersion -ne 1 -or $release.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Incompatible stable release' }
        if ($release.url -cnotmatch '^https://github\.com/X1039538408/yuanchuan-plugins/releases/download/framefit-v[0-9A-Za-z.-]+/framefit-[0-9A-Za-z.-]+-windows-x64\.zip$') { throw 'Untrusted release URL' }
        if (-not $release.url.EndsWith("/framefit-v$($release.version)/framefit-$($release.version)-windows-x64.zip", [StringComparison]::Ordinal)) { throw 'Release URL version mismatch' }
        if ($cached -and $cached.version -eq $release.version) {
            if ($cached.sha256 -ne $release.sha256) { throw 'Published versions are immutable; checksum changed' }
            $selected = $cached
        } else {
            $archive = Join-Path $scratch 'release.zip'
            Download-File $release.url $archive 2GB
            if ((Get-Sha256 $archive) -ne $release.sha256) { throw 'Release SHA-256 does not match' }
            $stage = Join-Path $scratch 'runtime'
            [IO.Directory]::CreateDirectory($stage) | Out-Null
            Expand-Safe $archive $stage
            Verify-Runtime $stage $release.version
            $releases = Join-Path $StateDir 'releases'
            [IO.Directory]::CreateDirectory($releases) | Out-Null
            $dest = Join-Path $releases $release.version
            if (Test-Path -LiteralPath $dest) {
                Verify-Runtime $dest $release.version
                $existingFiles = (Read-Json (Join-Path $dest 'runtime.json')).files | ConvertTo-Json -Compress
                $newFiles = (Read-Json (Join-Path $stage 'runtime.json')).files | ConvertTo-Json -Compress
                if ($existingFiles -ne $newFiles) { throw 'Published version differs from an existing pinned runtime' }
            } else { [IO.Directory]::Move($stage, $dest) }
            $pending = Join-Path $StateDir 'active.pending.json'
            @{ version=$release.version; apiVersion=1; sha256=$release.sha256 } | ConvertTo-Json | Set-Content -LiteralPath $pending -Encoding UTF8
            $activePath = Join-Path $StateDir 'active.json'
            if (Test-Path -LiteralPath $activePath) { [IO.File]::Replace($pending, $activePath, $null) } else { [IO.File]::Move($pending, $activePath) }
            $selected = @{root=$dest;version=$release.version}
        }
    } catch {
        if (-not $cached) { throw "FrameFit first download failed. Check network and retry: $($_.Exception.Message)" }
        [Console]::Error.WriteLine("FrameFit update unavailable; using verified $($cached.version): $($_.Exception.Message)")
        $selected = $cached
    }
} finally {
    # Scratch and lock are resolved beneath StateDir, never user media directories.
    Remove-StateDirectory $scratch
    Remove-StateDirectory $lock
}
& (Join-Path $selected.root 'host/FrameFitHost.exe') --runtime-root $selected.root --state-dir $StateDir
exit $LASTEXITCODE
