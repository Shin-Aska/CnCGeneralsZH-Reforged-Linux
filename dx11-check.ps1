# Photograph the same frame of the same match twice, once through Direct3D 9 and once through the
# Direct3D 11 backend, and count the pixels between them.
#
# This is the Direct3D 11 backend's exit measurement.  Everything else about the backend is a
# count - buffers mirrored, pipelines built, draws made and refused - and a run can have all of
# those right and still draw the wrong picture: the white terrain had a correct atlas, a correct
# program and a correct draw count for two sessions.  Only two pictures of one frame settle it.
#
# All three shots come from one exe.  -d3d9 is the only difference between them, so a view that
# comes back apart is the backend and not a build, a map or a seed.
#
# Three shots, not two.  The instrument has noise of its own: particles advance per rendered frame,
# so each view is photographed twice through Direct3D 9 as well and the pair's own disagreement is
# printed beside the signal.  That floor is 0.0% to 0.8% by count and 0.03 to 0.18 levels by mean.
#
# Two numbers a pair, and the pass rule is the mean.  This changed once the error got small, and the
# reason is in the numbers rather than in anybody's preference.  The count is pixels more than 40
# apart in summed channels: a coverage measure.  A boundary pixel that one rasteriser fills and the
# other does not flips between a leaf and the sky and clears 40 without difficulty, so the count
# tracks how much foliage is in the view - 0.18% on Golden Oasis against 1.44% on Flash Effect while
# the mean over the same eight views moves only from 0.56 to 0.91 levels a channel.  The two
# rasterisers have been shown to agree at every sub-pixel position across a whole pixel, ties
# included (test_pixelcentre), so a boundary pixel can only land differently because the vertex
# reaching it differs in its last bits - which is the transform, and no backend makes two of those
# bit-identical.  Below about 2% the count is measuring content and the mean is measuring error.
#
# So a view passes when the mean is under MeanMargin levels a channel.  -CountRule restores the old
# rule (count no further from Direct3D 9 than Direct3D 9 is from itself, plus Margin) for anyone who
# wants the number this phase was originally written against.  Both are always printed.
#
# The views are tree-check.ps1's, for the same reason it has them: they are the eight positions in
# UI-MAP.md that between them frame terrain, trees, water, roads, shadows and the command bar.

# -Map narrows the run to the views whose map name contains it, for the middle of a hunt where one
# view answers the question and eight of them is twenty-four launches.  The table that goes into a
# pull request is only ever written from a full run.
#
# -BackendNoise takes the repeated pair through the Direct3D 11 backend instead, which answers a
# different question: the printed floor is Direct3D 9 against itself, and a backend with a
# repeatability of its own would be measured against a floor that cannot see it.
#
# -Extra passes further switches to all three shots, which is how a pass is taken out of both sides
# at once: -Extra -nofx, -Extra -noshadowvolumes, -Extra -noshroud. A difference that collapses when
# one pass is gone belongs to that pass, and one that does not is somewhere else.
#
# -MeanMargin is the pass threshold in levels a channel, and 1.0 is the frame buffer's own step: two
# frames that agree to within the smallest value the buffer can hold are the same frame.
#
# -dx11post off is on every launch and must stay there.  The post chain runs by default since
# v1.0.0, and it changes the finished frame on purpose, so a run with it on disagrees with Direct3D 9
# by design and this script would be measuring the effect rather than the backend.  Its own before
# and after is a pair of pictures.
[CmdletBinding()]
param([double]$Margin = 1.0, [double]$MeanMargin = 1.0, [string]$Map = '',
  [switch]$BackendNoise, [switch]$CountRule, [string[]]$Extra = @(), [string]$Exe = 'generals.exe',
  [string]$RunDir = '', [Alias('Output', 'Out')][string]$OutDir = '',
  [ValidateRange(1, 86400)][int]$TimeoutSeconds = 900)
$ErrorActionPreference = 'Stop'
$scriptFileHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $PSCommandPath).Hash

# These switches define the instrument. Content-pass switches such as -nofx and -noaudio
# still pass through unchanged, including their quoted values.
$ownedSwitches = @('-headless', '-d3d9', '-dx11', '-dx11present', '-dx11post', '-msaa', '-root', '-overlay',
  '-video', '-directorrecord', '-screenshot', '-xres', '-yres', '-map', '-randommap', '-seed', '-maxframes',
  '-camera', '-autocamera', '-logprefix', '-win', '-fullscreen', '-window', '-borderless', '-hiddenwindow',
  '-autoskirmish', '-aidiff', '-mod', '-modpatch', '-file', '-nologorcrash', '-nodraw', '-shellmap',
  '-recordfps', '-directorscout', '-directortimeline', '-jumptoframe')
foreach ($argument in $Extra) {
  if (($argument -split '[=:]', 2)[0].ToLowerInvariant() -in $ownedSwitches) {
    throw "Extra cannot override comparison control: $argument"
  }
}
$contaminatingEnvironment = @(Get-ChildItem Env: | Where-Object {
  $_.Name -match '^(ZH_(DX11|D3D9|GPU|WATER|TEST|SUN_SHADOW|FFREF)_|ZH_(OFFSCREEN|HIDDEN_WINDOW)$|DXVK_|VK_|D3D_)' })
if ($contaminatingEnvironment.Count) {
  throw ('comparison refuses inherited renderer/test overrides: ' + (($contaminatingEnvironment.Name | Sort-Object) -join ', '))
}

# One new output root owns every writable path. No player directory or input file is changed.
function Test-Within([string]$Path, [string]$Root) {
  $p = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  $r = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
  return $p.Equals($r, [StringComparison]::OrdinalIgnoreCase) -or
    $p.StartsWith($r + '\', [StringComparison]::OrdinalIgnoreCase)
}
function Assert-NoReparse([string]$Path) {
  $cursor = [IO.Path]::GetFullPath($Path)
  while ($cursor) {
    if (Test-Path -LiteralPath $cursor) {
      if (((Get-Item -Force -LiteralPath $cursor).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "path must not traverse a reparse point: $cursor"
      }
    }
    $parent = Split-Path -Parent $cursor
    if ($parent -eq $cursor) { break }; $cursor = $parent
  }
}
function Get-LocalPath([string]$Path, [string]$Label) {
  if ($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Substring(2).Contains(':') -or
      [Management.Automation.WildcardPattern]::ContainsWildcardCharacters($Path)) {
    throw "$Label must be an absolute local path without wildcards or alternate streams: $Path"
  }
  $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
  Assert-NoReparse $full
  return $full
}
function Native-Quote([string]$Value) {
  if ($Value -eq '') { return '""' }
  if ($Value -notmatch '[\s"]') { return $Value }
  return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}
function Save-Json($Value, [string]$Path) {
  [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}
function Get-InputManifest([string]$Root) {
  foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -Force -File) {
    [pscustomobject]@{ path = $file.FullName.Substring($Root.Length + 1); bytes = $file.Length;
      sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash }
  }
}
function Save-ShotLogs([string]$Shot, [datetime]$StartedUtc) {
  $logs = @()
  foreach ($file in Get-ChildItem -LiteralPath $run -Recurse -File | Where-Object {
    $_.Extension -in '.log', '.txt' -and $_.LastWriteTimeUtc -ge $StartedUtc }) {
    Assert-NoReparse $file.FullName
    $destination = Join-Path (Join-Path $Shot 'logs') $file.FullName.Substring($run.Length + 1)
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination)
    Copy-Item -LiteralPath $file.FullName -Destination $destination
    $logs += $destination
  }
  return $logs
}
function Remove-OwnedTree([string]$Path) {
  foreach ($entry in Get-ChildItem -LiteralPath $Path -Force) {
    if ($entry.PSIsContainer -and ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
      Remove-OwnedTree $entry.FullName
    } elseif ($entry.PSIsContainer) { [IO.Directory]::Delete($entry.FullName) }
    else { Remove-Item -LiteralPath $entry.FullName -Force }
  }
  [IO.Directory]::Delete($Path)
}
function Stop-OwnedProcessTree([Diagnostics.Process]$Process) {
  if ($Process.HasExited) { return }
  $killer = New-Object Diagnostics.Process
  $killer.StartInfo.FileName = Join-Path $env:WINDIR 'System32\taskkill.exe'
  $killer.StartInfo.Arguments = "/PID $($Process.Id) /T /F"
  $killer.StartInfo.UseShellExecute = $false; $killer.StartInfo.CreateNoWindow = $true
  $killer.StartInfo.RedirectStandardOutput = $true; $killer.StartInfo.RedirectStandardError = $true
  try {
    $null = $killer.Start()
    if (-not $killer.WaitForExit(5000)) { $killer.Kill(); $null = $killer.WaitForExit(1000) }
    if (-not $Process.WaitForExit(5000)) {
      $Process.Kill()
      if (-not $Process.WaitForExit(5000)) { throw "owned PID $($Process.Id) could not be reaped" }
    }
  } finally { $killer.Dispose() }
}

if ($RunDir -eq '') { $RunDir = Join-Path $PSScriptRoot 'GeneralsMD\Run' }
if ($OutDir -eq '') { $OutDir = Join-Path $env:TEMP ('dx11check-' + [guid]::NewGuid().ToString('N')) }
$RunDir = Get-LocalPath $RunDir 'RunDir'; $OutDir = Get-LocalPath $OutDir 'OutDir'
if (-not (Test-Path -LiteralPath $RunDir -PathType Container)) { throw "RunDir is missing: $RunDir" }
if ((Test-Within $OutDir $RunDir) -or (Test-Within $RunDir $OutDir)) { throw 'OutDir and RunDir must be disjoint' }
$documentsRoots = @([Environment]::GetFolderPath('MyDocuments'), (Join-Path $env:USERPROFILE 'Documents'))
foreach ($documents in $documentsRoots | Where-Object { $_ }) {
  if ((Test-Within $OutDir $documents) -or (Test-Within $documents $OutDir)) { throw "OutDir overlaps protected Documents: $OutDir" }
}
if (Test-Path -LiteralPath $OutDir) { throw "OutDir must not already exist: $OutDir" }
if ([IO.Path]::GetFileName($Exe) -cne $Exe -or $Exe -in '.', '..' -or $Exe.Contains(':')) { throw 'Exe must be a file name inside RunDir' }
if (-not (Test-Path -LiteralPath (Join-Path $RunDir $Exe) -PathType Leaf)) { throw "Exe is missing from RunDir: $Exe" }
# Reject links anywhere in the staged input before recursively copying or hashing it.
$pending = New-Object 'Collections.Generic.Stack[string]'; $pending.Push($RunDir)
while ($pending.Count -gt 0) {
  foreach ($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
    if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "RunDir contains a reparse point: $($entry.FullName)" }
    if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
  }
}
Add-Type -AssemblyName System.Drawing
$tmp = $OutDir
$run = Join-Path $OutDir 'work'

# f is a logic frame at 60 a second
$cases = @(
  @{map='Flash Effect';       x='1200'; y='945';  f=800},
  @{map='Flash Effect';       x='1816'; y='1861'; f=2400},
  @{map='Flash Effect';       x='1378'; y='1384'; f=4800},
  @{map='ForgottenForestZH';  x='1620'; y='1470'; f=1200},
  @{map='ForgottenForestZH';  x='1543'; y='1636'; f=3600},
  @{map='Golden Oasis';       x='2643'; y='3580'; f=1800},
  @{map='Alpine Assault';     x='760';  y='920';  f=1400},
  @{map='Killing Fields';     x='1024'; y='1024'; f=3000}
)

# -msaa 0 avoids a multisampled readback. Every shot has a new private userdata directory;
# old screenshots are never selected or removed. A desktop-fallback log makes the shot fail.
# The run's own switches are $switches: PowerShell's names ignore case, so a parameter called $extra was the
# script's -Extra too, and -Extra (-noDynamicLOD, -noaudio ...) never reached the game while the renderer
# switch went in twice.
function Shoot($c, $tag, $switches) {
  $shot = Join-Path $OutDir $tag
  $null = New-Item -ItemType Directory -Path $shot
  $shots = Join-Path $shot 'user'
  $null = New-Item -ItemType Directory -Path $shots
  $options = Join-Path $shots 'Options.ini'
  [IO.File]::WriteAllText($options, "WaterStyle = 0`r`n", [Text.Encoding]::ASCII)
  $arguments = @('-win','-xres','1280','-yres','720','-quickstart','-noshellmap','-multiInstance',
    '-msaa','0','-dx11post','off','-map',"Maps\$($c.map)\$($c.map).map",'-autoskirmish','4','-aidiff','easy',
    '-seed','5','-maxframes',($c.f+160),'-screenshot',$c.f,'-camera',$c.x,$c.y,
    '-logPrefix',"dx11chk_$tag`_",'-turbo') + $switches + $Extra
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = Join-Path $run $Exe
  $psi.Arguments = (($arguments | ForEach-Object { Native-Quote "$_" }) -join ' ')
  $psi.WorkingDirectory = $run
  $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  $psi.EnvironmentVariables['ZH_UNATTENDED'] = '1'
  $psi.EnvironmentVariables['ZH_USER_DATA_DIR'] = $shots
  $receipt = [ordered]@{ tag = $tag; executable = $psi.FileName; executableSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $psi.FileName).Hash;
    arguments = $arguments; commandLine = (Native-Quote $psi.FileName) + ' ' + $psi.Arguments;
    workingDirectory = $run; userData = $shots; unattended = '1'; waterStyle = 0;
    optionsSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $options).Hash;
    inputsManifest = 'copied-inputs.json'; shaders = @($script:copiedInputs | Where-Object { $_.path -match 'shader|\.fx$|\.hlsl$' });
    pid = $null; exitCode = $null; processTree = 'timeout cleanup: taskkill /PID actual-child /T /F, bounded waits and stream drains';
    classification = 'start-failed'; startedUtc = [DateTime]::UtcNow.ToString('o');
    screenshot = $null; desktopFallback = 'not checked'; error = $null }
  $process = New-Object Diagnostics.Process
  $process.StartInfo = $psi
  $stdoutTask = $null; $stderrTask = $null
  try {
    if (-not $process.Start()) { throw "failed to start $tag" }
    $receipt.pid = $process.Id
    $stdoutTask = $process.StandardOutput.ReadToEndAsync(); $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
      $receipt.classification = 'timeout'
      Stop-OwnedProcessTree $process
      $receipt.exitCode = $process.ExitCode
      throw "$tag timed out; owned PID $($receipt.pid) was killed and reaped"
    }
    $receipt.exitCode = $process.ExitCode
    if ($process.ExitCode -ne 0) { $receipt.classification = 'child-failure'; throw "$tag failed with exit code $($process.ExitCode)" }
    $receipt.classification = 'capture-failure'
    $files = @(Get-ChildItem -LiteralPath $shots -Filter 'sshot*.bmp' -File)
    if ($files.Count -ne 1) { throw "expected one fresh private screenshot for $tag; found $($files.Count)" }
    Assert-NoReparse $files[0].FullName
    $image = New-Object System.Drawing.Bitmap($files[0].FullName)
    try {
      if ($image.Width -ne 1280 -or $image.Height -ne 720) { throw "wrong screenshot dimensions for ${tag}: $($image.Width)x$($image.Height)" }
      $first = $image.GetPixel(0, 0).ToArgb(); $varies = $false
      for ($y = 0; $y -lt $image.Height -and -not $varies; $y += 2) {
        for ($x = 0; $x -lt $image.Width; $x += 2) {
          if ($image.GetPixel($x, $y).ToArgb() -ne $first) { $varies = $true; break }
        }
      }
      if (-not $varies) { throw "blank or uniform screenshot for $tag" }
      $out = Join-Path $shot 'frame.png'
      $image.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
      $receipt.screenshot = [ordered]@{ source = $files[0].FullName; png = $out; width = $image.Width; height = $image.Height;
        bmpSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $files[0].FullName).Hash;
        pngSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $out).Hash }
    } finally { $image.Dispose() }
    $receipt.classification = 'success'
  } catch {
    $receipt.error = $_.Exception.Message
    throw
  } finally {
    if ($receipt.pid -and -not $process.HasExited) { Stop-OwnedProcessTree $process }
    if ($receipt.pid) { $receipt.exitCode = $process.ExitCode }
    $logs = @(Save-ShotLogs $shot ([DateTime]::Parse($receipt.startedUtc).ToUniversalTime()))
    if ($stdoutTask) {
      $stdoutFile = Join-Path $shot 'child.stdout.log'; $stderrFile = Join-Path $shot 'child.stderr.log'
      $drained = [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdoutTask, $stderrTask), 5000)
      [IO.File]::WriteAllText($stdoutFile, $(if ($stdoutTask.IsCompleted) { $stdoutTask.GetAwaiter().GetResult() } else { 'stdout did not close within 5 seconds' }))
      [IO.File]::WriteAllText($stderrFile, $(if ($stderrTask.IsCompleted) { $stderrTask.GetAwaiter().GetResult() } else { 'stderr did not close within 5 seconds' }))
      if (-not $drained) { $receipt.classification = 'cleanup-failure'; $receipt.error = 'child streams did not close within 5 seconds' }
      $logs += $stdoutFile; $logs += $stderrFile
    }
    $fallback = @()
    if ($logs.Count) { $fallback = @(Select-String -LiteralPath $logs -Pattern 'falling back to a desktop capture|desktop[ -]capture fallback') }
    $receipt.desktopFallback = if ($fallback.Count) { 'detected' } else { 'no fallback marker in retained fresh logs; native positive readback receipt unavailable' }
    if ($fallback.Count -gt 0) { $receipt.classification = 'desktop-fallback'; $receipt.error = "desktop capture fallback detected for $tag" }
    $receipt.generatedShaders = @(Get-ChildItem -LiteralPath $shots -File -Filter '*shader*' | ForEach-Object {
      Assert-NoReparse $_.FullName
      [ordered]@{ path = $_.FullName; bytes = $_.Length; sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash }
    })
    $receipt.optionsAfterSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $options).Hash
    $receipt.endedUtc = [DateTime]::UtcNow.ToString('o')
    Save-Json $receipt (Join-Path $shot 'receipt.json')
    $process.Dispose()
  }
  if ($receipt.classification -in 'desktop-fallback', 'cleanup-failure') { throw $receipt.error }
  return $out
}

# Every other pixel of every other row, counting the ones more than 40 apart in summed channels.
#
# The whole frame, where tree-check.ps1 stops at row 520 to keep the command bar out of a comparison
# about trees.  Carrying that limit over here hid a bug for the length of the phase: the 2D layer was
# drawn half a pixel off under Direct3D 11, 45% of the command bar was past this threshold, and every
# number this script printed was blind to it.  An instrument that cannot see part of the picture
# cannot be the exit condition for drawing the picture.
#
# It returns the mean too, and the two say different things. The count is a coverage measure: a
# boundary pixel that one rasteriser fills and the other does not flips between a leaf and the sky
# and lands well past the threshold, so a frame that is aligned to the pixel and correct in every
# other way still counts a percent of its foliage edges. The mean is a fidelity measure: it is what
# the frame is wrong by, and a mean of a level a channel is the same picture whatever the count says.
# Quote both or neither.
function DiffPct($a, $b) {
  $ia = New-Object System.Drawing.Bitmap($a)
  $ib = New-Object System.Drawing.Bitmap($b)
  $w = $ia.Width; $h = $ia.Height
  $apart = 0; $total = 0; $sum = 0.0
  for ($y = 0; $y -lt $h; $y += 2) {
    for ($x = 0; $x -lt $w; $x += 2) {
      $pa = $ia.GetPixel($x, $y); $pb = $ib.GetPixel($x, $y)
      $d = [Math]::Abs($pa.R - $pb.R) + [Math]::Abs($pa.G - $pb.G) + [Math]::Abs($pa.B - $pb.B)
      $total++
      $sum += $d
      if ($d -gt 40) { $apart++ }
    }
  }
  $ia.Dispose(); $ib.Dispose()
  return @([Math]::Round(100.0 * $apart / $total, 2), [Math]::Round($sum / $total / 3.0, 2))
}

if ($Map -ne '') { $cases = @($cases | Where-Object { $_.map -like "*$Map*" }) }
if ($cases.Count -eq 0) { throw "no view matches -Map $Map" }

$fail = 0
$completed = $false
$null = New-Item -ItemType Directory -Path $OutDir
try {
  $sourceInputs = @(Get-InputManifest $RunDir)
  Save-Json $sourceInputs (Join-Path $OutDir 'source-inputs.json')
  $null = New-Item -ItemType Directory -Path $run
  foreach ($inputFile in $sourceInputs) {
    $destination = Join-Path $run $inputFile.path
    $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination)
    Copy-Item -LiteralPath (Join-Path $RunDir $inputFile.path) -Destination $destination
  }
  $script:copiedInputs = @(Get-InputManifest $run)
  Save-Json $script:copiedInputs (Join-Path $OutDir 'copied-inputs.json')
  $sourceHashes = @($sourceInputs | ForEach-Object { "$($_.path)|$($_.sha256)" })
  $copiedHashes = @($script:copiedInputs | ForEach-Object { "$($_.path)|$($_.sha256)" })
  if (@(Compare-Object -ReferenceObject $sourceHashes -DifferenceObject $copiedHashes).Count -ne 0) {
    throw 'private input copy hashes differ from source'
  }
foreach ($c in $cases) {
  $tag = ($c.map -replace '[^A-Za-z]','') + "_$($c.x)_$($c.f)"
  $nine = Shoot $c "d3d9a_$tag" @('-d3d9')
  $eleven = Shoot $c "dx11_$tag" @()
  if ($BackendNoise) {
    $again = Shoot $c "dx11b_$tag" @()
    $noise = DiffPct $eleven $again
  } else {
    $again = Shoot $c "d3d9b_$tag" @('-d3d9')
    $noise = DiffPct $nine $again
  }
  $signal = DiffPct $nine $eleven
  $verdict = if ($CountRule) {
    if ($signal[0] -le ($noise[0] + $Margin)) { 'ok' } else { 'DIFFERENT' }
  } else {
    if ($signal[1] -le $MeanMargin) { 'ok' } else { 'DIFFERENT' }
  }
  if ($verdict -ne 'ok') { $fail++ }
  "{0,-20} cam {1,5},{2,-5} frame {3,-5} noise {4,5}% {5,5}  dx11 {6,5}% {7,5}  {8}" -f `
    $c.map, $c.x, $c.y, $c.f, $noise[0], $noise[1], $signal[0], $signal[1], $verdict
}
"---"
"pictures in $tmp"
$rule = if ($CountRule) { "$Margin% of their own noise" } else { "$MeanMargin levels a channel" }
if ($fail -eq 0) { "all $($cases.Count) views are within $rule" }
else { "$fail of $($cases.Count) views are further than $rule" }
  $completed = $true
} finally {
  # Only this invocation's freshly created work copy is removed, after preserving shot diagnostics.
  if (Test-Path -LiteralPath $run) {
    if ($run -cne (Join-Path $OutDir 'work') -or -not (Test-Within $run $OutDir)) { throw 'owned work cleanup escaped OutDir' }
    Assert-NoReparse $run
    Remove-OwnedTree $run
  }
  Save-Json ([ordered]@{ completed = $completed; failedViews = $fail; selectedViews = $cases.Count;
    countRule = [bool]$CountRule; margin = $Margin; meanMargin = $MeanMargin; backendNoise = [bool]$BackendNoise;
    sourceRunDir = $RunDir; workRemoved = -not (Test-Path -LiteralPath $run);
    scriptSha256 = $scriptFileHash;
    readbackLimit = 'fresh BMP and fallback-log rejection; engine has no positive native capture receipt' }) (Join-Path $OutDir 'run-summary.json')
}
exit $fail
