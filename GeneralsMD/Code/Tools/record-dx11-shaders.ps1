#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
<#
.SYNOPSIS
  Records the Direct3D 11 programs the game compiles into Data\dx11shaders.shipped.

.DESCRIPTION
  The Direct3D 11 backend compiles each generated program with d3dcompiler_47 the first time a pipeline
  needs it, and keeps the bytecode in the player's dx11shaders.cache. Under Wine, d3dcompiler_47 is
  vkd3d-shader, and one of those compiles took minutes (X2, on a Steam Deck): the game looked hung on its
  first frame. The backend reads dx11shaders.shipped, next to the exe, before it compiles anything, so the
  programs recorded here are never compiled on a player's machine.

  This plays AI-against-AI skirmishes with the picture drawn by Direct3D 11, on a Windows machine whose
  d3dcompiler_47 is Microsoft's. It does so at the default post chain and with it off, on a few seeds,
  plus the shell map. Every child uses a caller-selected, new private user-data directory. The farm's
  shipped file is unlinked unless -KeepShipped is requested. The recorder never moves or removes the
  real player's cache. What the runs compiled is copied to the requested private output only after the
  cache and production-variant manifest pass validation.

  Every run is unattended (ZH_UNATTENDED), in a farm made by windows-ci.ps1's own New-Farm, and killed at
  -TimeoutMinutes.

  -ExtraArgs goes to every run, for -noDynamicLOD: in a
  build that has that switch (when this was written, only Debug and Internal builds did), it keeps the effects a slow
  machine would drop, and so their programs, in the recording.

.EXAMPLE
  $root = Join-Path $env:TEMP ('zh-shader-record-' + [guid]::NewGuid().ToString('N'))
  $null = New-Item -ItemType Directory -Path $root
  .\record-dx11-shaders.ps1 -DataDir D:\ZeroHourData -UserData (Join-Path $root 'user') `
    -WorkDir (Join-Path $root 'work') -Out (Join-Path $root 'dx11shaders.shipped') `
    -Maps "Bitter Winter","Lights Out" -ExtraArgs -noDynamicLOD
#>
param(
	# a folder holding zerohour\ (a Zero Hour install, with ZH_Generals\ in it or the base game findable)
	[Parameter(Mandatory = $true)] [string] $DataDir,
	# the built game: generals.exe, its DLLs and the fork's data
	[string] $RunDir = (Join-Path $PSScriptRoot "..\..\Run"),
	# new private output path; an empty value resolves to the repository shipped path and is refused
	[string] $Out = "",
	# new private game user-data folder; the real Documents player folder is always refused
	[string] $UserData = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) "Command and Conquer Generals Zero Hour Data"),
	[string] $WorkDir = (Join-Path $env:TEMP "zh-record-dx11-shaders"),
	[int[]] $Seeds = @(0, 1, 2),
	# named skirmish maps played the same way, for the lighting the random maps do not have (night, snow)
	[string[]] $Maps = @(),
	# logic frames, 60 a second
	[int] $Frames = 4800,
	[int] $TimeoutMinutes = 30,
	# focused harness seam; production callers leave this at zero and use TimeoutMinutes
	[int] $TimeoutSeconds = 0,
	# leave the shipped file in the farm: a check that a run with it compiles nothing it holds
	[switch] $KeepShipped,
	# given to every run, after its own switches (for example -noDynamicLOD)
	[string[]] $ExtraArgs = @(),
	# additional exact userdata roots to protect in focused tests; the real Documents root is always protected
	[string[]] $AdditionalProtectedUserData = @()
)

$ErrorActionPreference = "Stop"

function Assert-NoReparseAncestor([string] $Path, [string] $Label) {
	$cursor = [IO.Path]::GetFullPath($Path)
	while (-not (Test-Path -LiteralPath $cursor)) {
		$parent = Split-Path -Parent $cursor
		if ([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor) { throw "$Label has no existing local ancestor: $Path" }
		$cursor = $parent
	}
	while (-not [string]::IsNullOrEmpty($cursor)) {
		$item = Get-Item -Force -LiteralPath $cursor
		if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
			throw "$Label must not traverse a junction, symbolic link, or other reparse point: $($item.FullName)"
		}
		$parent = Split-Path -Parent $cursor
		if ([string]::IsNullOrEmpty($parent) -or $parent -eq $cursor) { break }
		$cursor = $parent
	}
}

function Get-NewAbsolutePath([string] $Path, [string] $Label) {
	if ([string]::IsNullOrWhiteSpace($Path)) { throw "$Label must not be empty" }
	if ([Management.Automation.WildcardPattern]::ContainsWildcardCharacters($Path)) {
		throw "$Label must not contain wildcard characters: $Path"
	}
	if (-not [IO.Path]::IsPathRooted($Path)) { throw "$Label must be an absolute path: $Path" }
	try { $full = [IO.Path]::GetFullPath($Path) }
	catch { throw "$Label is not a valid path: $Path ($($_.Exception.Message))" }
	if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { throw "$Label must be on a local drive: $Path" }
	Assert-NoReparseAncestor $full $Label
	if (Test-Path -LiteralPath $full) { throw "$Label must not already exist: $full" }
	return $full.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-PathWithin([string] $Candidate, [string] $Root) {
	$candidateFull = [IO.Path]::GetFullPath($Candidate).TrimEnd('\', '/')
	$rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
	return $candidateFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
		$candidateFull.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-DisjointPaths([string] $Left, [string] $LeftLabel, [string] $Right, [string] $RightLabel) {
	if ((Test-PathWithin $Left $Right) -or (Test-PathWithin $Right $Left)) {
		throw "$LeftLabel and $RightLabel must be disjoint: $Left ; $Right"
	}
}

function ConvertTo-WindowsCommandLineArgument([string] $Value) {
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

function Get-CacheEntries([string] $Path) {
	$bytes = [IO.File]::ReadAllBytes($Path)
	if ($bytes.Length -lt 8) { throw "shader cache is shorter than its 8-byte header: $Path" }
	$at = 8; $entries = @()
	while ($at -lt $bytes.Length) {
		if ($at + 12 -gt $bytes.Length) { throw "shader cache has a truncated entry header at byte ${at}: $Path" }
		$hash = [BitConverter]::ToUInt64($bytes, $at).ToString('X16')
		$size = [BitConverter]::ToUInt32($bytes, $at + 8)
		if ($size -eq 0 -or $at + 12 + $size -gt $bytes.Length) {
			throw "shader cache has an invalid entry at byte $at (size $size): $Path"
		}
		$entries += $hash
		$at += 12 + $size
	}
	if ($entries.Count -eq 0) { throw "shader cache contains no entries: $Path" }
	return @($entries)
}

function Assert-ProductionManifest([string] $Path, [string[]] $CacheEntries) {
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "production variant manifest is missing: $Path" }
	try { $manifest = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json }
	catch { throw "production variant manifest is invalid JSON: $Path ($($_.Exception.Message))" }
	if ($null -eq $manifest.productionVariants -or @($manifest.productionVariants).Count -eq 0) {
		throw "production variant manifest has no productionVariants: $Path"
	}
	$names = @{}; $entryHashes = @{}
	foreach ($variant in @($manifest.productionVariants)) {
		foreach ($field in 'name', 'sourceHash', 'profileHash', 'profile', 'compilationResult', 'cacheEntry', 'expectedEntry') {
			if ($variant.PSObject.Properties.Name -notcontains $field) {
				throw "production variant is missing $field in $Path"
			}
		}
		if ([string]::IsNullOrWhiteSpace("$($variant.name)") -or $names.ContainsKey("$($variant.name)")) {
			throw "production variant names must be non-empty and unique in $Path"
		}
		$names["$($variant.name)"] = $true
		if ("$($variant.sourceHash)" -notmatch '^[0-9A-Fa-f]{64}$') { throw "invalid full sourceHash for $($variant.name)" }
		if ("$($variant.profileHash)" -notmatch '^[0-9A-Fa-f]{64}$') { throw "invalid full profileHash for $($variant.name)" }
		if ("$($variant.profile)" -notmatch '^[a-z]{2}_[0-9]+_[0-9]+$') { throw "invalid shader profile for $($variant.name)" }
		if ("$($variant.compilationResult)" -cne 'success') { throw "production compilation failed for $($variant.name)" }
		if ($variant.expectedEntry -ne $true) { throw "production variant is not marked expected: $($variant.name)" }
		$entry = "$($variant.cacheEntry)".ToUpperInvariant()
		if ($entry -notmatch '^[0-9A-F]{16}$') { throw "invalid cacheEntry for $($variant.name)" }
		if ($entryHashes.ContainsKey($entry)) { throw "duplicate production cacheEntry $entry in $Path" }
		$entryHashes[$entry] = $true
		if ($CacheEntries -notcontains $entry) { throw "missing production cache entry $entry for $($variant.name)" }
	}
	return $manifest
}

function Add-RunReceipt($Receipt) {
	$Receipt | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $WorkDir 'recorder-runs.jsonl') -Encoding UTF8
}

function Invoke-RecordingRun($Run, [string[]] $Arguments) {
	$farm = Join-Path $WorkDir "farm-$($Run.Name)"
	if (Test-Path -LiteralPath $farm) { throw "recording farm unexpectedly exists: $farm" }
	New-Farm $zh $farm
	$shipped = Join-Path $farm 'dx11shaders.shipped'
	if (-not $KeepShipped -and (Test-Path -LiteralPath $shipped)) { Remove-Item -LiteralPath $shipped -Force }

	$psi = New-Object Diagnostics.ProcessStartInfo
	$psi.FileName = Join-Path $farm 'generals.exe'
	$psi.WorkingDirectory = $farm
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.Arguments = (($Arguments | ForEach-Object { ConvertTo-WindowsCommandLineArgument "$_" }) -join ' ')
	$psi.EnvironmentVariables['ZH_UNATTENDED'] = '1'
	$psi.EnvironmentVariables['ZH_USER_DATA_DIR'] = $UserData

	$proc = New-Object Diagnostics.Process
	$proc.StartInfo = $psi
	$startedAt = [DateTime]::UtcNow.ToString('o')
	$classification = 'start-failed'; $pidValue = $null; $exitCode = $null
	try {
		if (-not $proc.Start()) { throw "failed to start child for $($Run.Name)" }
		$pidValue = $proc.Id
		$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
		$stderrTask = $proc.StandardError.ReadToEndAsync()
		$timeoutMilliseconds = if ($TimeoutSeconds -gt 0) { $TimeoutSeconds * 1000 } else { $Run.Minutes * 60 * 1000 }
		if (-not $proc.WaitForExit($timeoutMilliseconds)) {
			$classification = 'timeout'
			$proc.Kill(); $proc.WaitForExit()
			$exitCode = $proc.ExitCode
		} else {
			$proc.WaitForExit()
			$exitCode = $proc.ExitCode
			$classification = if ($exitCode -eq 0) { 'success' } else { 'child-failure' }
		}
		$stdoutTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $farm 'recorder-child.stdout.log') -Encoding UTF8
		$stderrTask.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $farm 'recorder-child.stderr.log') -Encoding UTF8
		Add-RunReceipt ([ordered]@{ name = $Run.Name; pid = $pidValue; startedUtc = $startedAt; endedUtc = [DateTime]::UtcNow.ToString('o'); classification = $classification; exitCode = $exitCode; userData = $UserData; farm = $farm })
		if ($classification -eq 'timeout') { throw "$($Run.Name) timed out and PID $pidValue was killed and reaped" }
		if ($exitCode -ne 0) { throw "$($Run.Name) failed with exit code $exitCode" }
		Write-Host ("{0}: exit 0" -f $Run.Name)
	} catch {
		if ($pidValue -and -not $proc.HasExited) { $proc.Kill(); $proc.WaitForExit() }
		if ($pidValue -and $classification -eq 'start-failed') {
			Add-RunReceipt ([ordered]@{ name = $Run.Name; pid = $pidValue; startedUtc = $startedAt; endedUtc = [DateTime]::UtcNow.ToString('o'); classification = 'exception-cleaned'; userData = $UserData; farm = $farm; error = $_.Exception.Message })
		}
		throw
	} finally {
		$proc.Dispose()
	}
}

if ($TimeoutMinutes -le 0) { throw "TimeoutMinutes must be positive" }
if ($TimeoutSeconds -lt 0) { throw "TimeoutSeconds must not be negative" }
if ($Frames -le 0) { throw "Frames must be positive" }

$RunDir = (Resolve-Path -LiteralPath $RunDir).Path
$DataDir = (Resolve-Path -LiteralPath $DataDir).Path
Assert-NoReparseAncestor $RunDir 'RunDir'
Assert-NoReparseAncestor $DataDir 'DataDir'
$zh = Join-Path $DataDir 'zerohour'
if (-not (Test-Path -LiteralPath $zh -PathType Container)) { throw "Zero Hour data directory is missing: $zh" }
if (-not (Test-Path -LiteralPath (Join-Path $RunDir 'generals.exe') -PathType Leaf)) { throw "generals.exe is missing from RunDir: $RunDir" }

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$defaultShipped = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\Data\dx11shaders.shipped"))
if ($Out -eq "") { $Out = $defaultShipped }
$UserData = Get-NewAbsolutePath $UserData 'UserData'
$WorkDir = Get-NewAbsolutePath $WorkDir 'WorkDir'
$Out = Get-NewAbsolutePath $Out 'Out'
$outManifest = "$Out.manifest.json"
if (Test-Path -LiteralPath $outManifest) { throw "output manifest must not already exist: $outManifest" }

if ($Out.Equals($defaultShipped, [StringComparison]::OrdinalIgnoreCase)) {
	throw "refusing to write the repository shipped cache; pass a new private -Out path"
}
$documents = [Environment]::GetFolderPath('MyDocuments')
$protectedUserData = @()
if (-not [string]::IsNullOrWhiteSpace($documents)) {
	$protectedUserData += [IO.Path]::GetFullPath((Join-Path $documents 'Command and Conquer Generals Zero Hour Data'))
}
foreach ($protected in $AdditionalProtectedUserData) {
	if ([string]::IsNullOrWhiteSpace($protected) -or -not [IO.Path]::IsPathRooted($protected)) {
		throw "AdditionalProtectedUserData entries must be non-empty absolute paths"
	}
	$protectedUserData += [IO.Path]::GetFullPath($protected)
}
foreach ($protected in $protectedUserData) {
	if ($UserData.Equals($protected.TrimEnd('\', '/'), [StringComparison]::OrdinalIgnoreCase)) {
		throw "refusing the real or protected player userdata directory: $UserData"
	}
}

Assert-DisjointPaths $UserData 'UserData' $WorkDir 'WorkDir'
Assert-DisjointPaths $UserData 'UserData' $Out 'Out'
Assert-DisjointPaths $WorkDir 'WorkDir' $Out 'Out'
foreach ($privatePath in @($UserData, $WorkDir, $Out)) {
	if ((Test-PathWithin $privatePath $RunDir) -or (Test-PathWithin $privatePath $DataDir)) {
		throw "private recording paths must be outside RunDir and DataDir: $privatePath"
	}
}

# windows-ci.ps1's own farm functions, taken from its syntax tree: a farm is never hand-rolled.
$ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $repoRoot "windows-ci.ps1"), [ref]$null, [ref]$null)
$ast.FindAll({ $args[0] -is [Management.Automation.Language.FunctionDefinitionAst] -and
	$args[0].Name -in 'New-FarmLink', 'New-Farm' }, $true) | ForEach-Object { Invoke-Expression $_.Extent.Text }

New-Item -ItemType Directory -Path $UserData | Out-Null
New-Item -ItemType Directory -Path $WorkDir | Out-Null
$cache = Join-Path $UserData 'dx11shaders.cache'
$producerManifestPath = "$cache.manifest.json"

# The same things windows-ci and replay-check pass for an E1 match, drawn this time.
$runs = @()
foreach ($seed in $Seeds) {
	$match = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-randommap", $seed, 2,
		"-autoskirmish", 2, "-aidiff", "brutal", "-seed", $seed, "-observer")
	$runs += , @{ Name = "seed$seed"; Args = $match; Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "seed$seed-nopost"; Args = $match + @("-dx11post", "off"); Minutes = $TimeoutMinutes }
}
foreach ($map in $Maps) {
	# quoted by hand: Start-Process joins its arguments with spaces and the names have them
	$named = @("-quickstart", "-noshellmap", "-multiInstance", "-noFPSLimit", "-map", "Maps\$map\$map.map",
		"-autoskirmish", 2, "-aidiff", "brutal", "-seed", 0, "-observer")
	$tag = $map -replace '[^A-Za-z0-9]', ''
	$runs += , @{ Name = "map-$tag"; Args = $named; Minutes = $TimeoutMinutes }
	$runs += , @{ Name = "map-$tag-nopost"; Args = $named + @("-dx11post", "off"); Minutes = $TimeoutMinutes }
}
# The main menu's shell map, the first thing a player sees. It must exit at maxframes like every other run;
# a timeout is always a recording failure.
$runs += , @{ Name = "shellmap"; Args = @("-multiInstance", "-noFPSLimit"); Minutes = 5 }

foreach ($run in $runs) {
	$arguments = $run.Args + $ExtraArgs + @('-maxframes', $Frames, '-hiddenwindow', '-noaudio', '-logPrefix', "rec_$($run.Name)")
	Invoke-RecordingRun $run $arguments
}

if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) {
	throw "no private shader cache was written: $cache (KeepShipped never makes this a success)"
}
$cacheEntries = @(Get-CacheEntries $cache)
$producerManifest = Assert-ProductionManifest $producerManifestPath $cacheEntries
$outParent = Split-Path -Parent $Out
if (-not (Test-Path -LiteralPath $outParent)) { New-Item -ItemType Directory -Path $outParent | Out-Null }
$sourceStream = [IO.File]::Open($cache, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
$destinationStream = $null
try {
	$destinationStream = [IO.File]::Open($Out, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
	$sourceStream.CopyTo($destinationStream)
} finally {
	if ($destinationStream) { $destinationStream.Dispose() }
	$sourceStream.Dispose()
}
$runReceipts = @(Get-Content -LiteralPath (Join-Path $WorkDir 'recorder-runs.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
$recordingManifest = [ordered]@{
	schemaVersion = 1
	output = $Out
	cacheSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Out).Hash
	producerManifestSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $producerManifestPath).Hash
	cacheEntries = $cacheEntries
	productionVariants = @($producerManifest.productionVariants)
	runs = $runReceipts
}
$manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes(($recordingManifest | ConvertTo-Json -Depth 12))
$manifestStream = [IO.File]::Open($outManifest, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try { $manifestStream.Write($manifestBytes, 0, $manifestBytes.Length) }
finally { $manifestStream.Dispose() }
$compilerPath = Join-Path $env:WINDIR 'System32\d3dcompiler_47.dll'
$compiler = if (Test-Path -LiteralPath $compilerPath) { (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion } else { 'not present (controlled harness)' }
Write-Host ("{0}: {1} programs, {2} bytes, production manifest validated, compiler {3}" -f $Out, $cacheEntries.Count, (Get-Item -LiteralPath $Out).Length, $compiler)
$global:LASTEXITCODE = 0
