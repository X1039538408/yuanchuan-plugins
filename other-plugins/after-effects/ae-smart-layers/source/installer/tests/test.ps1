[CmdletBinding()]
param([string]$BinaryDirectory = '', [string]$ReportPath = '', [string]$JsxPath = '')
$ErrorActionPreference = 'Stop'
if (-not $BinaryDirectory) { $BinaryDirectory = Join-Path $PSScriptRoot '..\dist' }
if (-not $ReportPath) { $ReportPath = Join-Path $PSScriptRoot 'test-report.json' }
if (-not $JsxPath) {
    $packaged = Join-Path $PSScriptRoot '..\..\AE 智能分层.jsx'
    $JsxPath = if (Test-Path -LiteralPath $packaged -PathType Leaf) { $packaged } else { Join-Path $PSScriptRoot '..\..\..\..\AE 智能分层.jsx' }
}
$JsxPath = (Resolve-Path -LiteralPath $JsxPath).Path
$BinaryDirectory = (Resolve-Path -LiteralPath $BinaryDirectory).Path
$setup = Join-Path $BinaryDirectory 'AE 智能分层.exe'
$remove = Join-Path $BinaryDirectory 'uninstaller.exe'
$root = Join-Path $PSScriptRoot ('temp-fixture-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$expected = '2EB5FA596DFF774F50AA4029AAAAE8B3907F540475F6642E9825B52BBAAE279D'
$results = New-Object System.Collections.Generic.List[object]
$script:counter = 0
function Fixture([string]$name) {
    $ae = Join-Path $root ($name + '\Adobe After Effects Fixture')
    New-Item -ItemType Directory -Path (Join-Path $ae 'Support Files\Scripts') -Force | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $ae 'Support Files\AfterFX.exe'), [byte[]]@())
    return $ae
}
function Plugin([string]$ae) { return Join-Path $ae 'Support Files\Scripts\AE 智能分层.jsx' }
function State([string]$name) { return Join-Path $root ($name + '\state') }
function Assert($condition, [string]$message) { if (-not $condition) { throw $message } }
function Call([string]$exe, [string[]]$arguments) {
    $script:counter++
    $quoted = ($arguments | ForEach-Object { '"' + $_.Replace('"', '\"') + '"' }) -join ' '
    $out = Join-Path $root ('cli-' + $script:counter + '.stdout.txt')
    $err = Join-Path $root ('cli-' + $script:counter + '.stderr.txt')
    $p = Start-Process -FilePath $exe -ArgumentList $quoted -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    return $p.ExitCode
}
function Install([string]$ae, [string]$state) { return Call $setup @('--install','--target',$ae,'--quiet','--test-mode','--state-dir',$state) }
function Uninstall([string]$ae, [string]$state) { return Call $remove @('--uninstall','--target',$ae,'--quiet','--test-mode','--state-dir',$state) }
function Case([string]$name, [scriptblock]$test) {
    try { & $test; $results.Add([ordered]@{ name = $name; passed = $true }); Write-Output "PASS $name" }
    catch { $results.Add([ordered]@{ name = $name; passed = $false; error = $_.Exception.Message }); Write-Output "FAIL $name : $($_.Exception.Message)" }
}
$a = Fixture 'normal'; $stateA = State 'normal'; $pluginA = Plugin $a
Case 'normal install copies exact payload only to Scripts' {
    Assert ((Install $a $stateA) -eq 0) 'install failed'
    Assert ((Get-FileHash -LiteralPath $pluginA -Algorithm SHA256).Hash -eq $expected) 'payload mismatch'
    Assert (-not (Test-Path -LiteralPath (Join-Path $a 'Support Files\Scripts\ScriptUI Panels'))) 'unexpected panel directory'
    Assert (Test-Path -LiteralPath (Join-Path $stateA 'uninstaller.exe')) 'uninstaller absent'
    Assert (Test-Path -LiteralPath (Join-Path $stateA 'manifest.xml')) 'manifest absent'
}
Case 'reinstall idempotent and manifest no duplicates' {
    Assert ((Install $a $stateA) -eq 0) 'reinstall failed'
    $doc = New-Object Xml.XmlDocument; $doc.Load((Join-Path $stateA 'manifest.xml'))
    Assert ($doc.SelectNodes('/aeSmartLayerSplit/target').Count -eq 1) 'duplicate record'
}
Case 'other files preserved at install and uninstall' {
    $other = Join-Path $a 'Support Files\Scripts\unrelated.jsx'
    [IO.File]::WriteAllText($other, 'keep me')
    Assert ((Install $a $stateA) -eq 0) 'install failed'
    Assert ((Uninstall $a $stateA) -eq 0) 'uninstall failed'
    Assert ([IO.File]::ReadAllText($other) -eq 'keep me') 'other file changed'
    Assert (-not (Test-Path -LiteralPath $pluginA)) 'plugin still exists'
    Assert (Test-Path -LiteralPath (Join-Path $a 'Support Files\Scripts')) 'Scripts removed'
}
Case 'uninstall idempotent' { Assert ((Uninstall $a $stateA) -eq 0) 'second uninstall failed' }
Case 'Support Files accepted directly' {
    Assert ((Install (Join-Path $a 'Support Files') $stateA) -eq 0) 'Support Files install failed'
    Assert ((Uninstall (Join-Path $a 'Support Files') $stateA) -eq 0) 'Support Files uninstall failed'
}
Case 'modified plugin retained and record retained' {
    Assert ((Install $a $stateA) -eq 0) 'setup install failed'
    $manifestBefore = (Get-FileHash -LiteralPath (Join-Path $stateA 'manifest.xml')).Hash
    [IO.File]::WriteAllText($pluginA, 'user edited plugin')
    Assert ((Uninstall $a $stateA) -ne 0) 'modified file reported success'
    Assert ([IO.File]::ReadAllText($pluginA) -eq 'user edited plugin') 'modified content lost'
    Assert ((Get-FileHash -LiteralPath (Join-Path $stateA 'manifest.xml')).Hash -eq $manifestBefore) 'record changed'
}
Case 'same-name conflict never overwritten' {
    Assert ((Install $a $stateA) -ne 0) 'conflict reported success'
    Assert ([IO.File]::ReadAllText($pluginA) -eq 'user edited plugin') 'conflict overwritten'
}
$b = Fixture 'multi'; $stateB = State 'multi'; $c = Fixture 'multi-second'
Case 'multiple targets recorded independently' {
    Assert ((Install $b $stateB) -eq 0) 'first install failed'
    Assert ((Install $c $stateB) -eq 0) 'second install failed'
    Assert ((Uninstall $b $stateB) -eq 0) 'first uninstall failed'
    Assert (Test-Path -LiteralPath (Plugin $c)) 'second plugin removed'
    $doc = New-Object Xml.XmlDocument; $doc.Load((Join-Path $stateB 'manifest.xml'))
    Assert ($doc.SelectNodes('/aeSmartLayerSplit/target').Count -eq 1) 'wrong target count'
}
Case 'standalone installer can also uninstall' { Assert ((Call $setup @('--uninstall','--target',$c,'--quiet','--test-mode','--state-dir',$stateB)) -eq 0) 'installer uninstall failed' }
Case 'embedded uninstaller identical to standalone binary' {
    Assert ((Get-FileHash -LiteralPath (Join-Path $stateB 'uninstaller.exe')).Hash -eq (Get-FileHash -LiteralPath $remove).Hash) 'embedded executable differs'
}
Case 'invalid target never creates Scripts' {
    $bad = Join-Path $root 'invalid'; New-Item -ItemType Directory -Path $bad | Out-Null
    Assert ((Install $bad (State 'invalid')) -ne 0) 'invalid accepted'
    Assert (-not (Test-Path -LiteralPath (Join-Path $bad 'Support Files\Scripts'))) 'invalid path changed'
}
Case 'AfterFX must still exist at uninstall' {
    $ae = Fixture 'missing-afterfx'; $state = State 'missing-afterfx'
    Assert ((Install $ae $state) -eq 0) 'setup failed'
    $after = Join-Path $ae 'Support Files\AfterFX.exe'
    # Exact checked fixture path, not a production executable.
    Assert ($after.StartsWith($root + '\')) 'fixture path escape'
    Remove-Item -LiteralPath $after
    Assert ((Uninstall $ae $state) -ne 0) 'removed despite missing AfterFX'
    Assert (Test-Path -LiteralPath (Plugin $ae)) 'plugin removed'
}
Case 'missing manifest explicit uninstall uses built-in hash' {
    $ae = Fixture 'missing-record'; $state = State 'missing-record'
    Assert ((Install $ae $state) -eq 0) 'setup failed'
    $record = Join-Path $state 'manifest.xml'; Assert ($record.StartsWith($root + '\')) 'path escape'
    Remove-Item -LiteralPath $record
    Assert ((Uninstall $ae $state) -eq 0) 'explicit recovery failed'
    Assert (-not (Test-Path -LiteralPath (Plugin $ae))) 'plugin remains'
}
Case 'corrupt manifest blocks install and uninstall without deleting' {
    $ae = Fixture 'corrupt'; $state = State 'corrupt'; New-Item -ItemType Directory -Path $state | Out-Null
    [IO.File]::WriteAllText((Join-Path $state 'manifest.xml'), '<broken')
    Assert ((Install $ae $state) -ne 0) 'corrupt install accepted'
    Assert ((Uninstall $ae $state) -ne 0) 'corrupt uninstall accepted'
    Assert (-not (Test-Path -LiteralPath (Plugin $ae))) 'corrupt install wrote plugin'
}
Case 'DTD and external entity manifest rejected' {
    $ae = Fixture 'dtd'; $state = State 'dtd'; New-Item -ItemType Directory -Path $state | Out-Null
    [IO.File]::WriteAllText((Join-Path $state 'manifest.xml'), '<!DOCTYPE x [<!ENTITY x SYSTEM "file:///C:/Windows/win.ini">]><aeSmartLayerSplit schema="1" payloadSha256="'+$expected+'">&x;</aeSmartLayerSplit>')
    Assert ((Install $ae $state) -ne 0) 'DTD accepted'
}
Case 'manifest arbitrary file/path fields cannot authorize deletion' {
    $ae = Fixture 'tampered'; $state = State 'tampered'; New-Item -ItemType Directory -Path $state | Out-Null
    $victim = Join-Path $root 'must-keep.txt'; [IO.File]::WriteAllText($victim, 'untouched')
    [IO.File]::WriteAllText((Join-Path $state 'manifest.xml'), '<aeSmartLayerSplit schema="1" payloadSha256="'+$expected+'"><target support="'+[Security.SecurityElement]::Escape((Join-Path $ae 'Support Files'))+'" path="'+[Security.SecurityElement]::Escape($victim)+'" /></aeSmartLayerSplit>')
    Assert ((Uninstall $ae $state) -ne 0) 'unknown manifest field accepted'
    Assert ([IO.File]::ReadAllText($victim) -eq 'untouched') 'victim changed'
}
Case 'manifest cannot substitute payload hash' {
    $ae = Fixture 'forged-hash'; $state = State 'forged-hash'; New-Item -ItemType Directory -Path $state | Out-Null
    [IO.File]::WriteAllText((Plugin $ae), 'changed')
    $forged = (Get-FileHash -LiteralPath (Plugin $ae)).Hash
    [IO.File]::WriteAllText((Join-Path $state 'manifest.xml'), '<aeSmartLayerSplit schema="1" payloadSha256="'+$forged+'" />')
    Assert ((Uninstall $ae $state) -ne 0) 'forged hash accepted'
    Assert ([IO.File]::ReadAllText((Plugin $ae)) -eq 'changed') 'changed plugin removed'
}
Case 'install failure rolls back newly copied plugin' {
    $ae = Fixture 'rollback-new'; $state = State 'rollback-new'; New-Item -ItemType Directory -Path (Join-Path $state 'uninstaller.exe') -Force | Out-Null
    Assert ((Install $ae $state) -ne 0) 'uninstaller path conflict reported success'
    Assert (-not (Test-Path -LiteralPath (Plugin $ae))) 'new plugin not rolled back'
    Assert (-not (Test-Path -LiteralPath (Join-Path $state 'manifest.xml'))) 'manifest unexpectedly committed'
}
Case 'manifest commit failure restores old uninstaller and old manifest' {
    $ae = Fixture 'rollback-record-old'; $next = Fixture 'rollback-record-new'; $state = State 'rollback-record'
    Assert ((Install $ae $state) -eq 0) 'setup failed'
    $record = Join-Path $state 'manifest.xml'; $before = (Get-FileHash -LiteralPath $record).Hash
    $uninstaller = Join-Path $state 'uninstaller.exe'; [IO.File]::WriteAllText($uninstaller, 'previous backup fixture')
    $handle = [IO.File]::Open($record, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Assert ((Install $next $state) -ne 0) 'locked manifest reported success' } finally { $handle.Dispose() }
    Assert ((Get-FileHash -LiteralPath $record).Hash -eq $before) 'manifest not restored'
    Assert ([IO.File]::ReadAllText($uninstaller) -eq 'previous backup fixture') 'uninstaller not restored'
    Assert (-not (Test-Path -LiteralPath (Plugin $next))) 'new plugin not rolled back'
    Assert (Test-Path -LiteralPath (Plugin $ae)) 'previous target lost'
}
Case 'uninstall delete failure rolls back manifest and preserves plugin' {
    $ae = Fixture 'rollback-uninstall'; $state = State 'rollback-uninstall'
    Assert ((Install $ae $state) -eq 0) 'setup failed'
    $record = Join-Path $state 'manifest.xml'; $before = (Get-FileHash -LiteralPath $record).Hash
    $handle = [IO.File]::Open((Plugin $ae), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Assert ((Uninstall $ae $state) -ne 0) 'locked plugin reported success' } finally { $handle.Dispose() }
    Assert ((Get-FileHash -LiteralPath $record).Hash -eq $before) 'manifest not rolled back'
    Assert ((Get-FileHash -LiteralPath (Plugin $ae)).Hash -eq $expected) 'plugin changed'
}
Case 'junction Scripts and ancestor paths rejected' {
    $ae = Fixture 'junction'; $outside = Join-Path $root 'junction-destination'; New-Item -ItemType Directory -Path $outside | Out-Null
    $scripts = Join-Path $ae 'Support Files\Scripts'; Assert ($scripts.StartsWith($root + '\')) 'path escape'
    Remove-Item -LiteralPath $scripts # empty fixture directory only, never recursive
    New-Item -ItemType Junction -Path $scripts -Target $outside | Out-Null
    Assert ((Install $ae (State 'junction')) -ne 0) 'junction accepted'
    Assert (-not (Test-Path -LiteralPath (Join-Path $outside 'AE 智能分层.jsx'))) 'junction target written'
}
Case 'state directory junction rejected' {
    $ae = Fixture 'state-junction'; $outside = Join-Path $root 'outside-state'; New-Item -ItemType Directory -Path $outside | Out-Null
    $state = Join-Path $root 'state-link'; New-Item -ItemType Junction -Path $state -Target $outside | Out-Null
    Assert ((Install $ae $state) -ne 0) 'state junction accepted'
    Assert (-not (Test-Path -LiteralPath (Plugin $ae))) 'plugin written'
}
Case 'test state override requires explicit test-mode' {
    Assert ((Call $setup @('--install','--target',$b,'--quiet','--state-dir',(State 'flag'))) -ne 0) 'state override allowed in production mode'
}
Case 'test mode requires isolated state directory' {
    Assert ((Call $setup @('--install','--target',$b,'--quiet','--test-mode')) -ne 0) 'test mode could write real state'
}
Case 'quiet mode never auto-selects a target' {
    Assert ((Call $setup @('--install','--quiet','--test-mode','--state-dir',(State 'no-target'))) -ne 0) 'quiet guessed target'
}
Case 'quiet mode requires explicit operation' {
    Assert ((Call $setup @('--target',$b,'--quiet','--test-mode','--state-dir',(State 'no-op'))) -ne 0) 'quiet guessed operation'
}
Case 'standalone uninstaller rejects install operation' {
    Assert ((Call $remove @('--install','--target',$b,'--quiet','--test-mode','--state-dir',(State 'bad-operation'))) -ne 0) 'uninstaller allowed install'
}
Case 'unknown and duplicate options rejected' {
    Assert ((Call $setup @('--install','--target',$b,'--quiet','--unexpected')) -ne 0) 'unknown flag accepted'
    Assert ((Call $setup @('--install','--install','--target',$b,'--quiet')) -ne 0) 'duplicate accepted'
}
Case 'no recursive deletion implementation' {
    $text = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '..\Core.cs'))
    Assert (-not $text.Contains('Directory.Delete(')) 'directory deletion API present'
    Assert (-not $text.Contains('RegistryKey.Create')) 'registry write present'
}
Case 'native WinForms minimum layout and actual install/error/uninstall buttons' {
    $base = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $compiler = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $harness = Join-Path $root 'UiSmoke.exe'
    $jsx = $JsxPath
    & $compiler /nologo /target:exe /main:AESmartLayerSplit.UiSmoke /codepage:65001 /reference:System.dll /reference:System.Core.dll /reference:System.Xml.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll "/out:$harness" "/resource:$jsx,payload.jsx" "/resource:$remove,uninstaller.exe" (Join-Path $base 'Core.cs') (Join-Path $base 'Program.cs') (Join-Path $PSScriptRoot 'UiSmoke.cs')
    Assert ($LASTEXITCODE -eq 0) 'UI harness compile failed'
    & $harness (Join-Path $root 'native-ui')
    Assert ($LASTEXITCODE -eq 0) 'native UI harness failed'
}
$failed = @($results | Where-Object { -not $_.passed }).Count
$report = [ordered]@{
    schema = 1
    generatedAt = [DateTimeOffset]::Now.ToString('o')
    environment = '.NET Framework 4 WinForms / empty AfterFX.exe fixtures only'
    fixtureRoot = $root
    realAfterEffectsTouched = $false
    registryMutated = $false
    passed = $results.Count - $failed
    failed = $failed
    tests = @($results.ToArray())
    limitations = @('No real After Effects invocation or JSX execution', 'No Program Files ACL mutation', 'Native UI is tested off-screen with fixture targets; no real AE UI/menu validation', 'Registry discovery is read-only and not invoked by test-mode CLI')
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Output "RESULT $($report.passed)/$($results.Count) passed; report: $ReportPath"
if ($failed -gt 0) { exit 1 }
