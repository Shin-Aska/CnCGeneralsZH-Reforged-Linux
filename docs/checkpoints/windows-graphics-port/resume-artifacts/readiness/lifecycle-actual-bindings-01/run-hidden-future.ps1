param([switch]$Go)
$ErrorActionPreference = 'Stop'
if (-not $Go) { throw 'GAME held: requires separate root review, sole GPU lease and explicit GO' }
if ($PSVersionTable.PSVersion.ToString() -cne '7.5.2') { throw 'qualified host must be PS7.5.2' }
$root = '<REPO>'
$binding = '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/lifecycle-actual-bindings-01'
$receipt = '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/native/lifecycle-hidden-820dedb5-cfcb-4949-85f5-c44310c3e532-execution'
$python = '<USERPROFILE>/AppData/Local/uv/cache/environments-v2/control-monitor-task8-0044c22f2c592d49/Scripts/python.exe'
$body = Join-Path $binding 'run_hidden.py'
$runner = '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-6/scene-input-repair/native-process-runner.ps1'
$manifest = Get-Content -Raw -LiteralPath (Join-Path $binding 'manifest.json') | ConvertFrom-Json
function Require-Pin([string]$Path,[long]$Bytes,[string]$Digest) {
 if ((Get-Item -LiteralPath $Path).Length -ne $Bytes -or (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant() -cne $Digest.ToLowerInvariant()) { throw "stale bound input: $Path" }
}
Require-Pin (Join-Path $binding 'body-freeze.json') 35927 '17c9d7db0c49ba75ba658f14c69d2a5a9b9c86485efe0a80ec1dbdb0efd0bcc3'
$inputs = Get-Content -Raw -LiteralPath (Join-Path $binding 'body-freeze.json') | ConvertFrom-Json
Require-Pin $runner 2048 'c78a26c2b58081cd9810ced579c6d1350faa528a164fbfb8c8aec45c08877790'
Require-Pin $python 255200 '553a3ab1745f76010992beb8be8e37252e064324fdd878a11bb04d34f861dac5'
foreach ($pin in $inputs) { Require-Pin $pin.path $pin.bytes $pin.sha256 }
if (Test-Path -LiteralPath $receipt) { throw 'receipt namespace collision' }
if (Test-Path -LiteralPath '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/native/lifecycle-hidden-820dedb5-cfcb-4949-85f5-c44310c3e532') { throw 'GAME output namespace collision' }
$previousLocation = (Get-Location).Path
$previousCwd = [Environment]::CurrentDirectory
$environmentBefore = [ordered]@{}
foreach ($entry in Get-ChildItem Env:) { $environmentBefore[$entry.Name] = $entry.Value }
$self = Get-Process -Id $PID
New-Item -ItemType Directory -Path $receipt | Out-Null
$registration = [ordered]@{supervisor=[ordered]@{pid=$PID;birthUtc=$self.StartTime.ToUniversalTime().ToString('o');path=$self.Path};child=[ordered]@{path=$python;argv=@('-B',$body,'--go');timeoutMilliseconds=2100000;windowStyle='Hidden'};game=[ordered]@{timeoutSeconds=600;selectedSource=$manifest.runtime.selectedSource;stage=$manifest.runtime.stage};cwd=$root;environmentMutation=$false;bytecode='-B';teardown='C78 direct child normal reap; isolated game600s finally; root must witness exact child births/port/cache/environment closure; direct launcher exact birth UNKNOWN until actual registration';fixedCase='hidden';gameAuthorizedByExplicitGo=$true}
$registration | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $receipt 'preregister.json') -Encoding utf8
try {
 Set-Location -LiteralPath $root
 [Environment]::CurrentDirectory = $root
 . $runner
 $result = Invoke-NativeProcess -FilePath $python -ArgumentList @('-B',('"'+$body+'"'),'--go') -StandardOutput (Join-Path $receipt 'stdout.log') -StandardError (Join-Path $receipt 'stderr.log') -TimeoutMilliseconds 2100000
 $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $receipt 'process.json') -Encoding utf8
 if ($result.timedOut -or $result.exitCode -ne 0 -or -not $result.reaped) { throw 'future capture process failed or timed out' }
} finally {
 Set-Location -LiteralPath $previousLocation
 [Environment]::CurrentDirectory = $previousCwd
 $environmentAfter = [ordered]@{}
 foreach ($entry in Get-ChildItem Env:) { $environmentAfter[$entry.Name] = $entry.Value }
 $sameEnvironment = ($environmentBefore | ConvertTo-Json -Compress) -ceq ($environmentAfter | ConvertTo-Json -Compress)
 foreach ($pin in $inputs) { Require-Pin $pin.path $pin.bytes $pin.sha256 }
 [ordered]@{environmentUnchanged=$sameEnvironment;cwdRestored=([Environment]::CurrentDirectory -ceq $previousCwd);runtimeClosure='REQUIRES actual game/Python birth/port/cache receipt review; wrapper result alone is not closure';retained='original3BMP/options/logs/reports';oldCleanupRetry=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $receipt 'wrapper-cleanup.json') -Encoding utf8
}
