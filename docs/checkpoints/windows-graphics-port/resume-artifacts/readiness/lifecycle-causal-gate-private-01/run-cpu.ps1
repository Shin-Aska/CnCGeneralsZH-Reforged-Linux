param([ValidateSet('red','red-r1','red-system','green','green-r1','red-stream','green-r2')][string]$Phase)
$ErrorActionPreference='Stop'
$d='<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-8/readiness/lifecycle-causal-gate-private-01'
$freeze=Join-Path $d ($Phase+'-freeze.json')
foreach($p in @(Get-Content -Raw -LiteralPath $freeze|ConvertFrom-Json)){if((Get-Item -LiteralPath $p.path).Length -ne $p.bytes -or (Get-FileHash -LiteralPath $p.path).Hash -ne $p.sha256){throw 'body_drift'}}
$python='<USERPROFILE>/AppData/Local/uv/cache/environments-v2/control-monitor-task8-0044c22f2c592d49/Scripts/python.exe'
if((Get-FileHash -LiteralPath $python).Hash -ne '553A3AB1745F76010992BEB8BE8E37252E064324FDD878A11BB04D34F861DAC5'){throw 'python_drift'}
$self=Get-Process -Id $PID
$envBefore=@(Get-ChildItem Env:|Sort-Object Name|ForEach-Object{@{name=$_.Name;value=$_.Value}})|ConvertTo-Json -Compress
$cwdBefore=(Get-Location).Path
@{phase=$Phase;supervisor=@{pid=$PID;birth=$self.StartTime.ToUniversalTime().ToString('o');path=$self.Path};program=$python;arguments=@('-B',(Join-Path $d 'qualify.py'),$Phase);cap=120000;teardown='8e-lifecycle-causal-gate-private-owned-teardown: original Popen finally close/wait/reap; each loopback port; stream workers; exact registered births; no broad kill';gameAuthorized=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $d ($Phase+'-cpu-registration.json'))
. '<REPO>/.omo/evidence/windows-modern-water-d3d11/20261016152026/task-6/scene-input-repair/native-process-runner.ps1'
$r=$null
try{
 Set-Location -LiteralPath $d
 $r=Invoke-NativeProcess -FilePath $python -ArgumentList @('-B',(Join-Path $d 'qualify.py'),$Phase) -StandardOutput (Join-Path $d ($Phase+'-stdout.txt')) -StandardError (Join-Path $d ($Phase+'-stderr.txt')) -TimeoutMilliseconds 120000
 $r|ConvertTo-Json -Depth 8|Set-Content (Join-Path $d ($Phase+'-process.json'))
}finally{
 Set-Location -LiteralPath $cwdBefore
 $originals=@();$identities=@()
 foreach($path in @(Get-ChildItem -LiteralPath (Join-Path $d $Phase) -Filter '*identity.json' -Recurse -ErrorAction SilentlyContinue)){
  $id=Get-Content -Raw -LiteralPath $path.FullName|ConvertFrom-Json;$identities+=$id
  foreach($known in @($id,$id.parent)){
   $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($known.pid)"
   if($p){$expected=[datetime]::FromFileTimeUtc([long]$known.creationFiletime);if([math]::Floor($p.CreationDate.ToUniversalTime().Ticks/10) -eq [math]::Floor($expected.Ticks/10)){$originals+=@{pid=$known.pid;birth=$known.creationFiletime;path=$p.ExecutablePath}}}
  }
 }
 $envAfter=@(Get-ChildItem Env:|Sort-Object Name|ForEach-Object{@{name=$_.Name;value=$_.Value}})|ConvertTo-Json -Compress
 @{environmentUnchanged=($envBefore -ceq $envAfter);cwdRestored=((Get-Location).Path -ceq $cwdBefore);identities=$identities;originalsRemaining=$originals;bytecodeAbsent=!(Test-Path -LiteralPath (Join-Path $d '__pycache__'));gameLaunched=$false}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $d ($Phase+'-cpu-cleanup.json'))
 if($originals.Count){throw 'original_process_remaining'}
}
if($r.timedOut){exit 2}
exit $r.exitCode
