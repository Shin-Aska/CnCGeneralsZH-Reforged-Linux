param(
  [Parameter(Mandatory = $true)] [string]$EvidenceDir,
  [string]$Check = '',
  [switch]$Baseline,
  [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
if ($Check -eq '') { $Check = Join-Path $PSScriptRoot '..\..\..\dx11-check.ps1' }
$scratch = Join-Path $env:TEMP ('dx11-cpu-selfcheck-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $scratch
$Check = (Resolve-Path -LiteralPath $Check).Path
$EvidenceDir = (Resolve-Path -LiteralPath $EvidenceDir).Path
$launcher = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$results = New-Object Collections.Generic.List[object]
function Assert-Check([bool]$Condition, [string]$Message) {
  if (-not $Condition) { throw "SELF-CHECK FAILED: $Message" }
}
function Native-Quote([string]$Value) {
  if ($Value -notmatch '[\s"]') { return $Value }
  return '"' + ($Value -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}
function Invoke-CLI([string]$Name, [string]$Body) {
  $wrapper = Join-Path $scratch "$Name.ps1"
  [IO.File]::WriteAllText($wrapper, $Body)
  $psi = New-Object Diagnostics.ProcessStartInfo
  $psi.FileName = $launcher
  $psi.Arguments = '-NoProfile -ExecutionPolicy Bypass -File ' + (Native-Quote $wrapper)
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.CreateNoWindow = $true
  $psi.EnvironmentVariables['TEMP'] = $scratch
  $psi.EnvironmentVariables['USERPROFILE'] = $scratch
  $p = New-Object Diagnostics.Process
  $p.StartInfo = $psi
  try {
    $null = $p.Start()
    $stdout = $p.StandardOutput.ReadToEndAsync(); $stderr = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(300000)) {
      & (Join-Path $env:WINDIR 'System32\taskkill.exe') /PID $p.Id /T /F | Out-Null
      $null = $p.WaitForExit(5000)
      throw "$Name CLI harness timeout"
    }
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    $code = $p.ExitCode
    $text | Set-Content -LiteralPath (Join-Path $EvidenceDir "$Name.txt") -Encoding UTF8
    return [pscustomobject]@{ Name = $Name; ExitCode = $code; Output = $text }
  } finally { $p.Dispose() }
}
try {
  if ($Baseline) {
    $quotedCheck = "'" + $Check.Replace("'", "''") + "'"
    $safeRoot = "'" + $scratch.Replace("'", "''") + "'"
    $cli = Invoke-CLI 'red-actual-cli' @"
`$ErrorActionPreference = 'Stop'
`$env:ZH_UNATTENDED = 'caller-sentinel'
try { & $quotedCheck -Map '__no_view_cpu_baseline__' -RunDir (Join-Path $safeRoot 'missing-run') -OutDir (Join-Path $safeRoot 'requested-private-output') }
catch { Write-Output ('ERROR=' + `$_.Exception.Message) }
Write-Output ('CALLER_UNATTENDED=' + `$env:ZH_UNATTENDED)
Write-Output ('REQUESTED_OUTPUT_EXISTS=' + (Test-Path -LiteralPath (Join-Path $safeRoot 'requested-private-output')))
Write-Output ('LEGACY_TEMP_EXISTS=' + (Test-Path -LiteralPath (Join-Path $safeRoot 'dx11check')))
"@
    Assert-Check ($cli.Output -match 'no view matches') 'unchanged CLI did not reach expected safe no-view branch'
    Assert-Check ($cli.Output -match 'CALLER_UNATTENDED=1') 'RED did not reproduce caller environment mutation'
    Assert-Check ($cli.Output -match 'REQUESTED_OUTPUT_EXISTS=False') 'RED output assumption changed'
    $results.Add([pscustomobject]@{ Name = 'RED isolated CLI'; ExpectedFailure = 'requested paths ignored and caller env changed'; Observed = $cli.Output })
    $binding = Invoke-CLI 'red-cli-binding' "`$ErrorActionPreference = 'Stop'; & $quotedCheck -MeanMargin 'not-a-number'"
    Assert-Check ($binding.ExitCode -ne 0 -and $binding.Output -match 'MeanMargin') 'actual CLI binding error did not fail'
    $results.Add([pscustomobject]@{ Name = 'RED actual CLI binding'; ExitCode = $binding.ExitCode })
    $shots = Join-Path $scratch 'safe-shots'; $null = New-Item -ItemType Directory -Path $shots
    $sentinel = Join-Path $shots 'sshot-sentinel.bmp'; [IO.File]::WriteAllText($sentinel, 'owned safe sentinel')
    $run = $scratch; $Exe = 'definitely-missing-cpu-fixture.exe'; $Extra = @(); $tmp = $scratch
    $ast = [Management.Automation.Language.Parser]::ParseFile($Check, [ref]$null, [ref]$null)
    $shootAst = $ast.Find({ $args[0] -is [Management.Automation.Language.FunctionDefinitionAst] -and $args[0].Name -eq 'Shoot' }, $true)
    Invoke-Expression $shootAst.Extent.Text
    try { Shoot @{ map = 'Synthetic'; x = '1'; y = '1'; f = 1 } 'red-safe-shoot' @() | Out-Null } catch { $shootError = $_.Exception.Message }
    Assert-Check (-not (Test-Path -LiteralPath $sentinel)) 'RED did not reproduce deletion in safe private shots harness'
    $results.Add([pscustomobject]@{ Name = 'RED safe unchanged Shoot'; SentinelDeleted = $true; Error = $shootError })
    $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'red-summary.json') -Encoding UTF8
    Write-Output 'RED CONFIRMED: actual CLI ignores isolation arguments, mutates caller env, and safe unchanged Shoot deletes existing screenshots.'
  } else {
    $fixtureRun = Join-Path $scratch 'fixture-run'
    $null = New-Item -ItemType Directory -Path $fixtureRun
    $fixture = Join-Path $fixtureRun 'dx11-cpu-fixture.exe'
    # Synthetic CPU-only child: never a game or GPU acceptance result, and never named generals.exe.
    Add-Type -ReferencedAssemblies System.Drawing -OutputAssembly $fixture -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Drawing;
using System.Drawing.Imaging;
using System.Diagnostics;
using System.Threading;
public static class Dx11CpuFixture {
  static string Arg(string[] args, string key, string fallback) {
    int i = Array.IndexOf(args, key); return i < 0 || i + 1 >= args.Length ? fallback : args[i + 1];
  }
  static string B64(string value) { return Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(value ?? "")); }
  public static int Main(string[] args) {
    if (Array.IndexOf(args, "-fixtureChild") >= 0) { Thread.Sleep(30000); return 0; }
    string user = Environment.GetEnvironmentVariable("ZH_USER_DATA_DIR");
    string unattended = Environment.GetEnvironmentVariable("ZH_UNATTENDED");
    string mode = Arg(args, "-fixtureMode", "success");
    Console.WriteLine("SYNTHETIC_CPU_ONLY|USER=" + B64(user) + "|UNATTENDED=" + B64(unattended) + "|ARGS=" + B64(String.Join("\0", args)) + "|CWD=" + B64(Environment.CurrentDirectory));
    if (String.IsNullOrEmpty(user) || unattended != "1" || File.ReadAllText(Path.Combine(user, "Options.ini")).Trim() != "WaterStyle = 0") return 21;
    File.WriteAllText(Path.Combine(user, "synthetic-shader-cache.txt"), "synthetic CPU fixture; no shader compilation");
    if (mode == "timeout") { Thread.Sleep(30000); return 0; }
    if (mode == "timeout-tree") {
      ProcessStartInfo info = new ProcessStartInfo(System.Reflection.Assembly.GetExecutingAssembly().Location, "-fixtureChild");
      info.UseShellExecute = false;
      using (Process child = Process.Start(info)) File.WriteAllText(Path.Combine(user, "synthetic-descendant.pid"), child.Id.ToString());
      Thread.Sleep(30000); return 0;
    }
    if (mode == "exit") return 7;
    if (mode == "missing") return 0;
    if (mode == "fallback") Console.WriteLine("takeScreenShot - the back buffer could not be read, falling back to a desktop capture");
    string prefix = Arg(args, "-logPrefix", "fixture_");
    File.WriteAllText(Path.Combine(Environment.CurrentDirectory, prefix + "fixture.log"), mode == "fallback-file" ? "SYNTHETIC_CPU_ONLY; falling back to a desktop capture" : "SYNTHETIC_CPU_ONLY; " + mode);
    int delta = Array.IndexOf(args, "-d3d9") >= 0 ? 0 : Int32.Parse(Arg(args, "-fixtureDelta", "0"));
    using (Bitmap image = new Bitmap(mode == "dimensions" ? 1279 : 1280, 720)) {
      using (Graphics g = Graphics.FromImage(image)) {
        g.Clear(Color.FromArgb(30 + delta, 60 + delta, 90 + delta));
        if (mode != "blank") using (Brush brush = new SolidBrush(Color.FromArgb(100 + delta, 120 + delta, 140 + delta))) g.FillRectangle(brush, 640, 0, 640, 720);
      }
      image.Save(Path.Combine(user, "sshot001.bmp"), ImageFormat.Bmp);
      if (mode == "duplicate") image.Save(Path.Combine(user, "sshot002.bmp"), ImageFormat.Bmp);
    }
    return 0;
  }
}
'@
    [IO.File]::WriteAllText((Join-Path $fixtureRun 'dx11shaders.shipped'), 'SYNTHETIC shader input sentinel')
    [IO.File]::WriteAllText((Join-Path $fixtureRun 'input-sentinel.txt'), 'input must remain unchanged')
    $inputHashes = @(Get-ChildItem -LiteralPath $fixtureRun -File | Get-FileHash -Algorithm SHA256)
    $fixtureInputs = $inputHashes | Select-Object Path,Hash
    $fixtureInputs | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDir 'synthetic-inputs.json') -Encoding UTF8
    $scenarioOuts = New-Object Collections.Generic.List[string]
    function Run-Scenario([string]$Name, [string]$Mode, [int]$ExpectedExit, [hashtable]$Overrides = @{}, [string]$ExpectedMessage = '') {
      if ($Only.Count -gt 0 -and $Name -notin $Only) { return }
      $out = Join-Path $scratch ('out-' + $Name)
      $parameters = @{ RunDir = $fixtureRun; Exe = 'dx11-cpu-fixture.exe'; OutDir = $out;
        Map = 'Golden'; TimeoutSeconds = 10; Extra = @('-fixtureMode', $Mode, '-noaudio', '-noDynamicLOD', '-fixtureQuoted', 'value with spaces', 'quote"value', 'space trailing\') }
      foreach ($key in $Overrides.Keys) { $parameters[$key] = $Overrides[$key] }
      $parameterFile = Join-Path $scratch ($Name + '.clixml'); $parameters | Export-Clixml -LiteralPath $parameterFile
      $escapedParameters = $parameterFile.Replace("'", "''"); $escapedCheck = $Check.Replace("'", "''")
      $envSetup = if ($Name -like '*existing-env*') {
        "`$env:ZH_UNATTENDED = 'caller-value'; `$env:ZH_USER_DATA_DIR = 'caller-user'"
      } else { "Remove-Item -LiteralPath Env:\ZH_UNATTENDED,Env:\ZH_USER_DATA_DIR -ErrorAction SilentlyContinue" }
      if ($Name -eq 'contaminated-backend-env') { $envSetup += "; `$env:ZH_DX11_DRIVER = 'warp'" }
      $body = @"
`$ErrorActionPreference = 'Stop'
$envSetup
`$beforeA = [Environment]::GetEnvironmentVariable('ZH_UNATTENDED', 'Process')
`$beforeU = [Environment]::GetEnvironmentVariable('ZH_USER_DATA_DIR', 'Process')
`$beforeDriver = [Environment]::GetEnvironmentVariable('ZH_DX11_DRIVER', 'Process')
`$beforePresence = @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_DX11_DRIVER') | ForEach-Object { [Environment]::GetEnvironmentVariables('Process').Contains(`$_) }
`$parameters = Import-Clixml -LiteralPath '$escapedParameters'
`$global:LASTEXITCODE = 0
try { & '$escapedCheck' @parameters; `$code = `$LASTEXITCODE }
catch { Write-Output ('ERROR=' + `$_.Exception.Message); `$code = 1 }
`$preserved = (`$beforeA -ceq [Environment]::GetEnvironmentVariable('ZH_UNATTENDED', 'Process')) -and (`$beforeU -ceq [Environment]::GetEnvironmentVariable('ZH_USER_DATA_DIR', 'Process')) -and (`$beforeDriver -ceq [Environment]::GetEnvironmentVariable('ZH_DX11_DRIVER', 'Process'))
`$afterPresence = @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_DX11_DRIVER') | ForEach-Object { [Environment]::GetEnvironmentVariables('Process').Contains(`$_) }
`$preserved = `$preserved -and ((`$beforePresence -join ',') -ceq (`$afterPresence -join ','))
Write-Output ('ENV_PRESERVED=' + `$preserved)
if (-not `$preserved) { exit 92 }
exit `$code
"@
      $observed = Invoke-CLI $Name $body
      Assert-Check ($observed.ExitCode -eq $ExpectedExit) "$Name expected exit $ExpectedExit, got $($observed.ExitCode): $($observed.Output)"
      Assert-Check ($observed.Output -match 'ENV_PRESERVED=True') "$Name caller environment was not preserved"
      if ($ExpectedMessage) { Assert-Check ($observed.Output -match $ExpectedMessage) "$Name missing expected failure: $($observed.Output)" }
      if (Test-Path -LiteralPath $out) {
        $scenarioOuts.Add($out)
        Assert-Check (-not (Test-Path -LiteralPath (Join-Path $out 'work'))) "$Name private work copy leaked"
        $save = Join-Path $EvidenceDir $Name; $null = New-Item -ItemType Directory -Path $save
        foreach ($file in Get-ChildItem -LiteralPath $out -Recurse -File | Where-Object { $_.Extension -in '.json', '.log', '.png' }) {
          $destination = Join-Path $save $file.FullName.Substring($out.Length + 1)
          $null = New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination)
          Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
        $receipts = @(Get-ChildItem -LiteralPath $out -Filter receipt.json -Recurse -File | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json })
        foreach ($receipt in $receipts) {
          Assert-Check ($receipt.pid -gt 0 -and $null -ne $receipt.exitCode) "$Name lost native PID/exit receipt"
          Assert-Check ($receipt.userData.StartsWith($out + '\', [StringComparison]::OrdinalIgnoreCase)) "$Name child user path escaped output"
          Assert-Check ($receipt.optionsSha256 -eq (Get-FileHash -LiteralPath (Join-Path $receipt.userData 'Options.ini')).Hash) "$Name Classic options hash mismatch"
          Assert-Check ($receipt.shaders.Count -eq 1 -and $receipt.shaders[0].sha256 -eq $inputHashes.Where({ $_.Path.EndsWith('dx11shaders.shipped') })[0].Hash) "$Name shader input hash missing"
          Assert-Check ($null -eq (Get-Process -Id $receipt.pid -ErrorAction SilentlyContinue)) "$Name child is still alive"
          $descendantFile = Join-Path $receipt.userData 'synthetic-descendant.pid'
          if (Test-Path -LiteralPath $descendantFile) {
            $descendantPid = [int](Get-Content -LiteralPath $descendantFile)
            Assert-Check ($null -eq (Get-Process -Id $descendantPid -ErrorAction SilentlyContinue)) "$Name owned descendant $descendantPid is still alive"
            [IO.File]::WriteAllText((Join-Path (Join-Path $EvidenceDir $Name) 'descendant-cleanup.txt'), "SYNTHETIC_CPU_ONLY; descendant PID $descendantPid no longer alive")
          }
          $childLog = Join-Path (Split-Path -Parent $receipt.userData) 'child.stdout.log'
          $line = (Get-Content -LiteralPath $childLog | Select-Object -First 1)
          $fields = @{}; foreach ($field in $line.Split('|') | Select-Object -Skip 1) { $pair = $field.Split('=', 2); $fields[$pair[0]] = $pair[1] }
          $user = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($fields.USER))
          $unattended = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($fields.UNATTENDED))
          $nativeArgs = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($fields.ARGS)).Split([char]0)
          Assert-Check ($user -ceq $receipt.userData -and $unattended -ceq '1') "$Name child environment mismatch"
          foreach ($arg in @('-dx11post', 'off', '-msaa', '0', '-noaudio', '-noDynamicLOD', '-fixtureQuoted', 'value with spaces', 'quote"value', 'space trailing\')) {
            if ($Overrides.ContainsKey('Extra')) { break }
            Assert-Check ($nativeArgs -ccontains $arg) "$Name native argument changed: $arg"
          }
          $mapAt = [Array]::IndexOf($nativeArgs, '-map')
          Assert-Check ($nativeArgs[$mapAt + 1] -like 'Maps\*\*.map') "$Name map argument changed"
        }
        if ($ExpectedExit -eq 0 -and $Mode -eq 'success') {
          $expectedCount = if ($parameters.Map -eq '') { 24 } else { 3 }
          Assert-Check ($receipts.Count -eq $expectedCount) "$Name expected $expectedCount shots, found $($receipts.Count)"
          Assert-Check (@($receipts.userData | Select-Object -Unique).Count -eq $receipts.Count) "$Name reused private shot userdata"
          if ($parameters.BackendNoise) { Assert-Check (@($receipts.tag | Where-Object { $_ -like 'dx11b_*' }).Count -gt 0) 'BackendNoise repetition changed' }
        }
      }
      foreach ($inputHash in $inputHashes) { Assert-Check ((Get-FileHash -Algorithm SHA256 -LiteralPath $inputHash.Path).Hash -ceq $inputHash.Hash) "$Name mutated input install" }
      $results.Add([pscustomobject]@{ Name = $Name; ExpectedExit = $ExpectedExit; ExitCode = $observed.ExitCode; Synthetic = $true; GpuAcceptance = 'UNRUN' })
      Write-Output "PASS $Name (synthetic CPU CLI; exit $($observed.ExitCode))"
    }
    Run-Scenario 'success-absent-env' 'success' 0
    Run-Scenario 'backend-noise-existing-env' 'success' 0 @{ BackendNoise = $true }
    Run-Scenario 'all-eight-views' 'success' 0 @{ Map = '' }
    Run-Scenario 'mean-rule-fails' 'success' 1 @{ Extra = @('-fixtureDelta', '2') } 'DIFFERENT'
    Run-Scenario 'mean-margin-passes' 'success' 0 @{ MeanMargin = 3; Extra = @('-fixtureDelta', '2') }
    Run-Scenario 'count-rule-passes' 'success' 0 @{ CountRule = $true; Extra = @('-fixtureDelta', '2') }
    Run-Scenario 'count-margin-fails' 'success' 1 @{ CountRule = $true; Extra = @('-fixtureDelta', '20') } 'DIFFERENT'
    Run-Scenario 'count-margin-passes' 'success' 0 @{ CountRule = $true; Margin = 100; Extra = @('-fixtureDelta', '20') }
    Run-Scenario 'nonzero-existing-env' 'exit' 1 @{} 'exit code 7'
    Run-Scenario 'missing' 'missing' 1 @{} 'expected one fresh private screenshot'
    Run-Scenario 'duplicate' 'duplicate' 1 @{} 'found 2'
    Run-Scenario 'dimensions' 'dimensions' 1 @{} 'wrong screenshot dimensions'
    Run-Scenario 'blank' 'blank' 1 @{} 'blank or uniform'
    Run-Scenario 'fallback' 'fallback' 1 @{} 'desktop capture fallback'
    Run-Scenario 'fallback-file' 'fallback-file' 1 @{} 'desktop capture fallback'
    Run-Scenario 'timeout-existing-env' 'timeout' 1 @{ TimeoutSeconds = 1 } 'timed out.*killed and reaped'
    Run-Scenario 'timeout-owned-descendant' 'timeout-tree' 1 @{ TimeoutSeconds = 1 } 'timed out.*killed and reaped'
    Run-Scenario 'contaminated-backend-env' 'success' 1 @{} 'refuses inherited renderer/test overrides'
    foreach ($conflict in @('-d3d9', '-dx11post', '-msaa', '-headless', '-root', '-screenshot', '-noLogOrCrash', '-mod', '-autocamera')) {
      Run-Scenario ('conflict-' + $conflict.TrimStart('-')) 'success' 1 @{ Extra = @($conflict, 'on') } 'cannot override comparison control'
    }
    $existing = Join-Path $scratch 'existing-out'; $null = New-Item -ItemType Directory -Path $existing
    $sentinel = Join-Path $existing 'sentinel.txt'; [IO.File]::WriteAllText($sentinel, 'existing output must survive')
    $sentinelHash = (Get-FileHash -LiteralPath $sentinel).Hash
    Run-Scenario 'existing-output' 'success' 1 @{ OutDir = $existing } 'must not already exist'
    Assert-Check ((Get-FileHash -LiteralPath $sentinel).Hash -eq $sentinelHash) 'existing output sentinel changed'
    Run-Scenario 'overlap-input' 'success' 1 @{ OutDir = (Join-Path $fixtureRun 'nested-out') } 'must be disjoint'
    Run-Scenario 'relative-output' 'success' 1 @{ OutDir = '.\relative-dx11-out' } 'absolute local path'
    Run-Scenario 'wildcard-output' 'success' 1 @{ OutDir = (Join-Path $scratch 'wild*out') } 'without wildcards'
    $documents = Join-Path $scratch 'Documents'
    Run-Scenario 'protected-documents' 'success' 1 @{ OutDir = (Join-Path $documents ('dx11-refused-' + [guid]::NewGuid().ToString('N'))) } 'protected Documents'
    $target = Join-Path $scratch 'junction-target'; $null = New-Item -ItemType Directory -Path $target
    $junction = Join-Path $scratch 'junction'; $null = New-Item -ItemType Junction -Path $junction -Target $target
    Run-Scenario 'junction-output' 'success' 1 @{ OutDir = (Join-Path $junction 'escape') } 'reparse point'
    [IO.Directory]::Delete($junction)
    Run-Scenario 'missing-executable' 'success' 1 @{ Exe = 'missing.exe' } 'Exe is missing'
    Run-Scenario 'no-view-existing-env' 'success' 1 @{ Map = '__no_view_cpu_baseline__' } 'no view matches'
    $pwsh = Get-Command pwsh.exe -ErrorAction SilentlyContinue
    if ($pwsh) {
      $launcher = $pwsh.Source
      Run-Scenario 'pwsh7-success-existing-env' 'success' 0
      Run-Scenario 'pwsh7-timeout-absent-env' 'timeout' 1 @{ TimeoutSeconds = 1 } 'timed out.*killed and reaped'
    }
    $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'green-summary.json') -Encoding UTF8
    Write-Output "PASS $($results.Count) CPU CLI scenarios; native GPU comparison UNRUN."
  }
} finally {
  $resolved = [IO.Path]::GetFullPath($scratch)
  $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
  Assert-Check ($resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) 'scratch cleanup escaped TEMP'
  Remove-Item -LiteralPath $resolved -Recurse -Force
  [pscustomobject]@{ Scratch = $resolved; Removed = -not (Test-Path -LiteralPath $resolved); Synthetic = $true; GpuAcceptance = 'UNRUN' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDir 'cleanup.json') -Encoding UTF8
}
