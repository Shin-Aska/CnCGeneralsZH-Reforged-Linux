$ErrorActionPreference='Stop'
$d='<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/pause-reset-private-successor-01/green'
$freeze='<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/pause-reset-private-successor-01/green-freeze.json'
if((Get-FileHash -LiteralPath $freeze).Hash -ne '427AE50736962C8BE11C7E9894B80124AC1BE58E477F648103A878A26AFD45B4'){throw 'body_freeze_drift'}
foreach($p in @(Get-Content -Raw -LiteralPath $freeze|ConvertFrom-Json)){if((Get-Item -LiteralPath $p.path).Length -ne $p.bytes -or (Get-FileHash -LiteralPath $p.path).Hash -ne $p.sha256){throw 'body_drift'}}
$python='<USERPROFILE>/AppData/Local/uv/cache/environments-v2/control-monitor-task8-0044c22f2c592d49/Scripts/python.exe'
if((Get-FileHash -LiteralPath $python).Hash -ne '553A3AB1745F76010992BEB8BE8E37252E064324FDD878A11BB04D34F861DAC5'){throw 'python_drift'}
$self=Get-Process -Id $PID
$envBefore=@(Get-ChildItem Env:|Sort-Object Name|ForEach-Object{@{name=$_.Name;value=$_.Value}})|ConvertTo-Json -Compress
$cwdBefore=(Get-Location).Path
$scratch=[IO.Path]::GetFullPath((Join-Path $d 'scratch'))
if(!$scratch.StartsWith([IO.Path]::GetFullPath($d)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'scratch_escape'}
if(Test-Path -LiteralPath $scratch){throw 'scratch_collision'}
@{registered=(Get-Date -Format o);supervisor=@{pid=$PID;birth=$self.StartTime.ToUniversalTime().ToString('o');path=$self.Path};program=$python;arguments=@('-B','<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/pause-reset-private-successor-01/qualify_green.py');cap=120000;scratch=$scratch;cleanup='own fixture scratch; eleven registered private P/R helperwire children/ports; screenshot producer workers; one actual interruptwatcher; exact identity/reap/env/CWD';gameAuthorized=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $d 'cpu-registration.json')
New-Item -ItemType Directory -Path $scratch|Out-Null
. '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-6/scene-input-repair/native-process-runner.ps1'
$r=$null
try{
 Set-Location -LiteralPath $d
 $r=Invoke-NativeProcess -FilePath $python -ArgumentList @('-B','<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/pause-reset-private-successor-01/qualify_green.py') -StandardOutput (Join-Path $d 'stdout.json') -StandardError (Join-Path $d 'stderr.txt') -TimeoutMilliseconds 120000
 $r|ConvertTo-Json -Depth 8|Set-Content (Join-Path $d 'process.json')
}finally{
 Set-Location -LiteralPath $cwdBefore
 $owned=@(Get-ChildItem -LiteralPath $scratch -Recurse -File|ForEach-Object{@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 ConvertTo-Json -InputObject @($owned) -Depth 5|Set-Content (Join-Path $d 'scratch-cleanup-intent.json')
 foreach($f in $owned){if(!$f.path.StartsWith($scratch+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'cleanup_escape'};if((Get-Item -LiteralPath $f.path).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'cleanup_reparse'};if((Get-FileHash -LiteralPath $f.path).Hash -ne $f.sha256){throw 'cleanup_drift'};Remove-Item -LiteralPath $f.path}
 foreach($sub in @(Get-ChildItem -LiteralPath $scratch -Recurse -Directory|Sort-Object {$_.FullName.Length} -Descending)){if($sub.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'cleanup_dir_reparse'};Remove-Item -LiteralPath $sub.FullName}
 Remove-Item -LiteralPath $scratch
 $fixtureIdentities=@(Get-ChildItem -LiteralPath $d -Filter child-identity.json -Recurse|ForEach-Object{Get-Content -Raw -LiteralPath $_.FullName|ConvertFrom-Json})
 $originals=@()
 foreach($id in $fixtureIdentities){foreach($known in @($id,$id.parent)){$p=Get-CimInstance Win32_Process -Filter "ProcessId=$($known.pid)";if($p){$expected=[datetime]::FromFileTimeUtc([long]$known.creationFiletime);if([math]::Floor($p.CreationDate.ToUniversalTime().Ticks/10) -eq [math]::Floor($expected.Ticks/10)){$originals+=@{pid=$known.pid;birth=$known.creationFiletime;path=$p.ExecutablePath}}}}}
 $envAfter=@(Get-ChildItem Env:|Sort-Object Name|ForEach-Object{@{name=$_.Name;value=$_.Value}})|ConvertTo-Json -Compress
 $idPath=Join-Path $d 'python-identity.json';$stillOwned=$null
 if(Test-Path -LiteralPath $idPath){$id=Get-Content -Raw -LiteralPath $idPath|ConvertFrom-Json;$p=Get-CimInstance Win32_Process -Filter "ProcessId=$($id.pid)";if($p){$expected=[datetime]::FromFileTimeUtc([long]$id.creationFiletime);if([math]::Floor($p.CreationDate.ToUniversalTime().Ticks/10) -eq [math]::Floor($expected.Ticks/10)){$stillOwned=$p|Select-Object ProcessId,ParentProcessId,CreationDate,ExecutablePath}}}
 @{sourceBodies=@(Get-Content -Raw -LiteralPath $freeze|ConvertFrom-Json|ForEach-Object{@{path=$_.path;sha256After=(Get-FileHash -LiteralPath $_.path).Hash;unchanged=((Get-FileHash -LiteralPath $_.path).Hash -eq $_.sha256)}});environmentUnchanged=($envBefore -ceq $envAfter);cwdRestored=((Get-Location).Path -ceq $cwdBefore);scratchAbsent=!(Test-Path -LiteralPath $scratch);ownedFiles=$owned;pythonStillOriginal=$stillOwned;bytecodeAbsent=!(Test-Path -LiteralPath (Join-Path $d '__pycache__'));gameLaunched=$false;fixtureIdentities=$fixtureIdentities;originalFixtureProcessesRemaining=$originals}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $d 'cpu-cleanup.json')
 if($stillOwned -or $originals.Count -gt 0){throw 'original_python_or_fixture_remaining'}
}
if($r.timedOut -or $r.exitCode -ne 0){throw 'unexpected_green_exit'}
$proof=Get-Content -Raw -LiteralPath (Join-Path $d 'proof.json')|ConvertFrom-Json
if($proof.classification -ne 'QUALIFIED_FOCUSED_GREEN' -or $proof.matchedCases -ne 11 -or $proof.unexpectedFailures -ne 0){throw 'unqualified_green'}
