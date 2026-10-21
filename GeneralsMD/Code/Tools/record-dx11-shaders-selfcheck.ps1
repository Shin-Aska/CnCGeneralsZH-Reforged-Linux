param(
	[Parameter(Mandatory = $true)] [string] $EvidenceDir,
	[string] $Recorder = ""
)

$ErrorActionPreference = "Stop"

function Assert-True([bool] $Condition, [string] $Message) {
	if (-not $Condition) { throw "SELF-CHECK FAILED: $Message" }
}

function Get-EnvState([string] $Name) {
	$value = [Environment]::GetEnvironmentVariable($Name, 'Process')
	return [pscustomobject]@{ Exists = $null -ne $value; Value = $value }
}

function Assert-EnvState([string] $Name, $Expected) {
	$actual = Get-EnvState $Name
	Assert-True ($actual.Exists -eq $Expected.Exists) "$Name existence changed"
	Assert-True ($actual.Value -ceq $Expected.Value) "$Name value changed"
}

function Get-FileState([string] $Path) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		return [pscustomobject]@{ Exists = $false; Hash = $null; Length = 0 }
	}
	$item = Get-Item -LiteralPath $Path
	return [pscustomobject]@{
		Exists = $true
		Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
		Length = $item.Length
	}
}

function Assert-FileState([string] $Path, $Expected, [string] $Label) {
	$actual = Get-FileState $Path
	Assert-True ($actual.Exists -eq $Expected.Exists) "$Label existence changed"
	Assert-True ($actual.Hash -ceq $Expected.Hash) "$Label bytes changed"
	Assert-True ($actual.Length -eq $Expected.Length) "$Label length changed"
}

function ConvertTo-NativeArgument([string] $Value) {
	if ($null -eq $Value -or $Value.Length -eq 0) { return '""' }
	if ($Value -notmatch '[\s"]') { return $Value }
	$result = '"'; $slashes = 0
	foreach ($character in $Value.ToCharArray()) {
		if ($character -eq '\') { $slashes++; continue }
		if ($character -eq '"') {
			$result += ('\' * (2 * $slashes + 1)) + '"'; $slashes = 0; continue
		}
		if ($slashes -gt 0) { $result += '\' * $slashes; $slashes = 0 }
		$result += $character
	}
	if ($slashes -gt 0) { $result += '\' * (2 * $slashes) }
	return $result + '"'
}

function Invoke-RecorderScenario {
	param(
		[Parameter(Mandatory = $true)] [string] $Name,
		[Parameter(Mandatory = $true)] [string] $Mode,
		[bool] $ExpectSuccess,
		[switch] $KeepShipped,
		[switch] $ExistingUser,
		[switch] $ExistingWork,
		[switch] $ExistingOut,
		[switch] $UseProtectedUser,
		[switch] $RelativeUser,
		[switch] $WildcardUser,
		[switch] $OverlapRoots,
		[switch] $ExistingManifest,
		[switch] $JunctionEscape,
		[string] $UserOverride = '',
		[int] $TimeoutSeconds = 15,
		[ValidateSet('absent', 'existing')] [string] $CallerEnvironment = 'absent'
	)

	$scenario = Join-Path $EvidenceDir $Name
	New-Item -ItemType Directory -Path $scenario | Out-Null
	$user = Join-Path $scenario 'user'
	$work = Join-Path $scenario 'work'
	$out = Join-Path $scenario 'output\dx11shaders.shipped'
	$protected = Join-Path $scenario 'known-documents\Command and Conquer Generals Zero Hour Data'
	if ($JunctionEscape) {
		$junctionTarget = Join-Path $scenario 'junction-target'
		$junctionPath = Join-Path $scenario 'junction'
		New-Item -ItemType Directory -Path $junctionTarget | Out-Null
		New-Item -ItemType Junction -Path $junctionPath -Target $junctionTarget | Out-Null
		$protected = Join-Path $junctionTarget 'Command and Conquer Generals Zero Hour Data'
		$user = Join-Path $junctionPath 'Command and Conquer Generals Zero Hour Data'
	}
	if ($UseProtectedUser) { $user = $protected }
	if ($RelativeUser) { $user = ".\relative-user-$Name" }
	if ($WildcardUser) { $user = Join-Path $scenario 'wild*user' }
	if ($UserOverride -ne '') { $user = $UserOverride }
	if ($OverlapRoots) { $work = Join-Path $user 'work' }
	if ($ExistingUser) {
		New-Item -ItemType Directory -Path $user -Force | Out-Null
		[IO.File]::WriteAllBytes((Join-Path $user 'dx11shaders.cache'), [byte[]](1, 3, 3, 7))
	}
	if ($ExistingWork) { New-Item -ItemType Directory -Path $work -Force | Out-Null }
	if ($ExistingOut) {
		New-Item -ItemType Directory -Path (Split-Path -Parent $out) -Force | Out-Null
		[IO.File]::WriteAllBytes($out, [byte[]](9, 8, 7))
	}
	if ($ExistingManifest) {
		New-Item -ItemType Directory -Path (Split-Path -Parent $out) -Force | Out-Null
		[IO.File]::WriteAllBytes("$out.manifest.json", [byte[]](5, 4, 3))
	}
	$existingUserCache = Get-FileState (Join-Path $user 'dx11shaders.cache')
	$existingOutState = Get-FileState $out

	if ($CallerEnvironment -eq 'existing') {
		[Environment]::SetEnvironmentVariable('ZH_UNATTENDED', "caller-unattended-$Name", 'Process')
		[Environment]::SetEnvironmentVariable('ZH_USER_DATA_DIR', "caller-user-$Name", 'Process')
	} else {
		[Environment]::SetEnvironmentVariable('ZH_UNATTENDED', $null, 'Process')
		[Environment]::SetEnvironmentVariable('ZH_USER_DATA_DIR', $null, 'Process')
	}
	$beforeUnattended = Get-EnvState 'ZH_UNATTENDED'
	$beforeUserData = Get-EnvState 'ZH_USER_DATA_DIR'

	$args = @{
		DataDir = $script:DataDir
		RunDir = $script:RunDir
		UserData = $user
		WorkDir = $work
		Out = $out
		Seeds = if ($ExpectSuccess) { @(0) } else { @() }
		Maps = if ($ExpectSuccess) { @('Map With Spaces') } else { @() }
		Frames = 5
		TimeoutMinutes = 1
		TimeoutSeconds = $TimeoutSeconds
		AdditionalProtectedUserData = @($protected)
		ExtraArgs = @('-fakeMode', $Mode, '-fakeQuoted', 'value with spaces', 'quote"value', 'trailing\')
	}
	if ($KeepShipped) { $args.KeepShipped = $true }

	$success = $true; $failure = $null; $output = @()
	try { $output = @(& $Recorder @args *>&1 | ForEach-Object { "$_" }) }
	catch { $success = $false; $failure = $_.Exception.Message; $output += $failure }
	$output | Set-Content -LiteralPath (Join-Path $scenario 'recorder-output.txt') -Encoding UTF8

	Assert-EnvState 'ZH_UNATTENDED' $beforeUnattended
	Assert-EnvState 'ZH_USER_DATA_DIR' $beforeUserData
	Assert-True ($success -eq $ExpectSuccess) "$Name expected success=$ExpectSuccess, actual success=$success; $failure"
	if ($ExistingUser) { Assert-FileState (Join-Path $user 'dx11shaders.cache') $existingUserCache "$Name existing user cache" }
	if ($ExistingOut) { Assert-FileState $out $existingOutState "$Name existing output" }
	if ($JunctionEscape) {
		Assert-True (-not (Test-Path -LiteralPath $work)) "$Name reached WorkDir through an unsafe junction path"
		Assert-True (-not (Test-Path -LiteralPath $protected)) "$Name created the protected junction target"
	}

	if ($ExpectSuccess) {
		Assert-True (Test-Path -LiteralPath $out -PathType Leaf) "$Name output missing"
		Assert-True (Test-Path -LiteralPath "$out.manifest.json" -PathType Leaf) "$Name output manifest missing"
		$observation = Join-Path $user 'child-observations.txt'
		Assert-True (Test-Path -LiteralPath $observation -PathType Leaf) "$Name child observations missing"
		$lines = @(Get-Content -LiteralPath $observation)
		Assert-True ($lines.Count -eq 5) "$Name expected 5 child runs, observed $($lines.Count)"
		foreach ($line in $lines) {
			$fields = @{}; $line.Split('|') | ForEach-Object { $pair = $_.Split('=', 2); $fields[$pair[0]] = $pair[1] }
			$observedUser = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($fields.ZH))
			Assert-True ($observedUser -ceq ([IO.Path]::GetFullPath($user))) "$Name child userdata mismatch: $observedUser"
			Assert-True ($fields.UNATTENDED -ceq '1') "$Name child unattended mismatch"
			$observedArgs = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($fields.ARGS)).Split([char]0)
			foreach ($expectedArgument in @('-fakeMode', 'success', '-fakeQuoted', 'value with spaces', 'quote"value', 'trailing\')) {
				Assert-True ($observedArgs -ccontains $expectedArgument) "$Name child argument was not preserved exactly: $expectedArgument"
			}
			$mapIndex = [Array]::IndexOf($observedArgs, '-map')
			if ($mapIndex -ge 0) {
				Assert-True ($observedArgs[$mapIndex + 1] -ceq 'Maps\Map With Spaces\Map With Spaces.map') "$Name map argument quoting changed"
			}
		}
	}

	return [pscustomobject]@{ Name = $Name; Success = $success; ExpectedSuccess = $ExpectSuccess; Failure = $failure }
}

function Invoke-ExternalRecorderScenario {
	param(
		[Parameter(Mandatory = $true)] [string] $Name,
		[Parameter(Mandatory = $true)] [string] $Mode,
		[int] $ExpectedExit,
		[switch] $KeepShipped,
		[int] $TimeoutSeconds = 15
	)
	$scenario = Join-Path $EvidenceDir "external-exits\$Name"
	New-Item -ItemType Directory -Path $scenario -Force | Out-Null
	$user = Join-Path $scenario 'user'; $work = Join-Path $scenario 'work'; $out = Join-Path $scenario 'output\dx11shaders.shipped'
	$arguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Recorder,
		'-DataDir', $script:DataDir, '-RunDir', $script:RunDir, '-UserData', $user, '-WorkDir', $work,
		'-Out', $out, '-Seeds', '0', '-Frames', '5', '-TimeoutMinutes', '1', '-TimeoutSeconds', "$TimeoutSeconds")
	if ($KeepShipped) { $arguments += '-KeepShipped' }
	$psi = New-Object Diagnostics.ProcessStartInfo
	$psi.FileName = 'powershell.exe'; $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true
	$psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
	$psi.Arguments = (($arguments | ForEach-Object { ConvertTo-NativeArgument "$_" }) -join ' ')
	$psi.EnvironmentVariables['RECORDER_FAKE_MODE'] = $Mode
	$proc = New-Object Diagnostics.Process; $proc.StartInfo = $psi
	$pidValue = $null
	try {
		Assert-True ($proc.Start()) "$Name external recorder did not start"
		$pidValue = $proc.Id; $stdoutTask = $proc.StandardOutput.ReadToEndAsync(); $stderrTask = $proc.StandardError.ReadToEndAsync()
		if (-not $proc.WaitForExit(120000)) { $proc.Kill(); $proc.WaitForExit(); throw "$Name external recorder wrapper timed out" }
		$proc.WaitForExit(); $stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $scenario 'stdout.txt') -Encoding UTF8
		$stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $scenario 'stderr.txt') -Encoding UTF8
		Assert-True ($proc.ExitCode -eq $ExpectedExit) "$Name expected process exit $ExpectedExit, got $($proc.ExitCode)"
		return [pscustomobject]@{ Name = $Name; Pid = $pidValue; ExitCode = $proc.ExitCode; ExpectedExit = $ExpectedExit }
	} finally {
		if ($pidValue -and -not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
		$proc.Dispose()
	}
}

$EvidenceDir = [IO.Path]::GetFullPath($EvidenceDir)
Assert-True (-not (Test-Path -LiteralPath $EvidenceDir)) "EvidenceDir must be new: $EvidenceDir"
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
$Recorder = if ($Recorder -eq "") { Join-Path $PSScriptRoot 'record-dx11-shaders.ps1' } else { $Recorder }
$Recorder = (Resolve-Path -LiteralPath $Recorder).Path
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

$script:DataDir = Join-Path $EvidenceDir 'fake-data'
$script:RunDir = Join-Path $EvidenceDir 'fake-run'
New-Item -ItemType Directory -Path (Join-Path $script:DataDir 'zerohour') -Force | Out-Null
New-Item -ItemType Directory -Path $script:RunDir -Force | Out-Null

$fakeSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;

public static class FakeGenerals {
    static string B64(string value) { return Convert.ToBase64String(Encoding.UTF8.GetBytes(value ?? "")); }
    public static int Main(string[] args) {
        string user = Environment.GetEnvironmentVariable("ZH_USER_DATA_DIR");
        string unattended = Environment.GetEnvironmentVariable("ZH_UNATTENDED");
        if (String.IsNullOrEmpty(user)) return 41;
        Directory.CreateDirectory(user);
        string observation = String.Format("PID={0}|ZH={1}|UNATTENDED={2}|ARGS={3}",
            Process.GetCurrentProcess().Id, B64(user), unattended ?? "<null>", B64(String.Join("\0", args)));
        File.AppendAllText(Path.Combine(user, "child-observations.txt"), observation + Environment.NewLine);
        string mode = Environment.GetEnvironmentVariable("RECORDER_FAKE_MODE") ?? "success";
        for (int i = 0; i + 1 < args.Length; ++i) if (args[i] == "-fakeMode") mode = args[i + 1];
        if (mode == "timeout") { Thread.Sleep(30000); return 0; }
        if (mode == "fail") return 7;
        if (mode == "missing-cache") return 0;
        string cache = Path.Combine(user, "dx11shaders.cache");
        using (BinaryWriter writer = new BinaryWriter(File.Open(cache, FileMode.Create, FileAccess.Write, FileShare.Read))) {
            writer.Write(new byte[] { 68, 88, 49, 49, 67, 65, 67, 72 });
            writer.Write(0x0102030405060708UL);
            writer.Write((UInt32)4);
            writer.Write(new byte[] { 1, 2, 3, 4 });
        }
        string manifest = mode == "incomplete-manifest"
            ? "{\"schemaVersion\":1,\"productionVariants\":[]}"
            : "{\"schemaVersion\":1,\"productionVariants\":[{\"name\":\"fake-production\",\"sourceHash\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\",\"profileHash\":\"BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\",\"profile\":\"ps_5_0\",\"compilationResult\":\"success\",\"cacheEntry\":\"0102030405060708\",\"expectedEntry\":true}]}";
        File.WriteAllText(cache + ".manifest.json", manifest, new UTF8Encoding(false));
        return 0;
    }
}
'@
$fakeExe = Join-Path $script:RunDir 'generals.exe'
Add-Type -TypeDefinition $fakeSource -Language CSharp -OutputAssembly $fakeExe -OutputType ConsoleApplication
[IO.File]::WriteAllBytes((Join-Path $script:RunDir 'dx11shaders.shipped'), [byte[]](6, 6, 6))

$realUser = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Command and Conquer Generals Zero Hour Data'
$realCache = Join-Path $realUser 'dx11shaders.cache'
$realBefore = Get-FileState $realCache
$fakeShipped = Join-Path $script:RunDir 'dx11shaders.shipped'
$fakeShippedBefore = Get-FileState $fakeShipped
$originalUnattended = Get-EnvState 'ZH_UNATTENDED'
$originalUserData = Get-EnvState 'ZH_USER_DATA_DIR'
$results = @()
$externalResults = @()
try {
	$results += Invoke-RecorderScenario -Name 'success-env-absent' -Mode 'success' -ExpectSuccess $true -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'success-env-existing' -Mode 'success' -ExpectSuccess $true -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'child-failure' -Mode 'fail' -ExpectSuccess $false -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'child-timeout' -Mode 'timeout' -ExpectSuccess $false -CallerEnvironment existing -TimeoutSeconds 1
	$results += Invoke-RecorderScenario -Name 'missing-cache' -Mode 'missing-cache' -ExpectSuccess $false -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'incomplete-manifest' -Mode 'incomplete-manifest' -ExpectSuccess $false -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'keep-shipped-missing-cache' -Mode 'missing-cache' -ExpectSuccess $false -KeepShipped -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'existing-user' -Mode 'success' -ExpectSuccess $false -ExistingUser -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'existing-work' -Mode 'success' -ExpectSuccess $false -ExistingWork -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'existing-out' -Mode 'success' -ExpectSuccess $false -ExistingOut -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'existing-manifest' -Mode 'success' -ExpectSuccess $false -ExistingManifest -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'protected-user' -Mode 'success' -ExpectSuccess $false -UseProtectedUser -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'real-user' -Mode 'success' -ExpectSuccess $false -UserOverride $realUser -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'relative-user' -Mode 'success' -ExpectSuccess $false -RelativeUser -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'wildcard-user' -Mode 'success' -ExpectSuccess $false -WildcardUser -CallerEnvironment absent
	$results += Invoke-RecorderScenario -Name 'overlapping-roots' -Mode 'success' -ExpectSuccess $false -OverlapRoots -CallerEnvironment existing
	$results += Invoke-RecorderScenario -Name 'junction-escape' -Mode 'success' -ExpectSuccess $false -JunctionEscape -CallerEnvironment absent
	$externalResults += Invoke-ExternalRecorderScenario -Name 'success-zero' -Mode 'success' -ExpectedExit 0
	$externalResults += Invoke-ExternalRecorderScenario -Name 'failed-child-nonzero' -Mode 'fail' -ExpectedExit 1
	$externalResults += Invoke-ExternalRecorderScenario -Name 'timeout-nonzero' -Mode 'timeout' -ExpectedExit 1 -TimeoutSeconds 1
	$externalResults += Invoke-ExternalRecorderScenario -Name 'missing-cache-nonzero' -Mode 'missing-cache' -ExpectedExit 1
	$externalResults += Invoke-ExternalRecorderScenario -Name 'incomplete-manifest-nonzero' -Mode 'incomplete-manifest' -ExpectedExit 1
	$externalResults += Invoke-ExternalRecorderScenario -Name 'keep-shipped-missing-nonzero' -Mode 'missing-cache' -ExpectedExit 1 -KeepShipped

	Assert-FileState $realCache $realBefore 'real player cache'
	Assert-FileState $fakeShipped $fakeShippedBefore 'RunDir shipped cache fixture'
	$childPids = @(Get-ChildItem -LiteralPath $EvidenceDir -Filter 'recorder-runs.jsonl' -Recurse -File -ErrorAction SilentlyContinue |
		ForEach-Object { Get-Content -LiteralPath $_.FullName } |
		ForEach-Object { try { ($_ | ConvertFrom-Json).pid } catch { $null } } |
		Where-Object { $_ })
	$alive = @($childPids | ForEach-Object {
		$process = Get-Process -Id $_ -ErrorAction SilentlyContinue
		if ($process -and $process.ProcessName -ieq 'generals') {
			$processPath = try { $process.Path } catch { $null }
			if ($processPath -and $processPath.StartsWith($EvidenceDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
				$process.Id
			}
		}
	})
	Assert-True ($alive.Count -eq 0) "owned child PIDs still alive: $($alive -join ',')"

	$summary = [ordered]@{
		result = 'passed'
		scenarios = @($results)
		externalProcessScenarios = @($externalResults)
		nonzeroFailureCount = @($externalResults | Where-Object { $_.ExpectedExit -ne 0 -and $_.ExitCode -ne 0 }).Count
		zeroSuccessCount = @($externalResults | Where-Object { $_.ExpectedExit -eq 0 -and $_.ExitCode -eq 0 }).Count
		childPids = $childPids
		aliveChildPids = $alive
		realPlayerCacheBefore = $realBefore
		realPlayerCacheAfter = Get-FileState $realCache
	}
	$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'selfcheck-summary.json') -Encoding UTF8
	Write-Host "record-dx11-shaders self-check passed: $($results.Count) in-process scenarios; $($externalResults.Count) external exit scenarios; $($childPids.Count) child receipts; 0 live children"
} finally {
	if ($originalUnattended.Exists) { [Environment]::SetEnvironmentVariable('ZH_UNATTENDED', $originalUnattended.Value, 'Process') }
	else { [Environment]::SetEnvironmentVariable('ZH_UNATTENDED', $null, 'Process') }
	if ($originalUserData.Exists) { [Environment]::SetEnvironmentVariable('ZH_USER_DATA_DIR', $originalUserData.Value, 'Process') }
	else { [Environment]::SetEnvironmentVariable('ZH_USER_DATA_DIR', $null, 'Process') }
}
