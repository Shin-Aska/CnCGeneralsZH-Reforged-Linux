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

function Get-ReplayControlSha256([byte[]] $Bytes)
{
	$sha = [Security.Cryptography.SHA256]::Create()
	try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant() }
	finally { $sha.Dispose() }
}

function New-ReplayControlMutation(
	[string] $SourcePath,
	[string] $UntouchedPath,
	[string] $MutatedPath)
{
	$source = [IO.Path]::GetFullPath($SourcePath)
	$untouched = [IO.Path]::GetFullPath($UntouchedPath)
	$mutated = [IO.Path]::GetFullPath($MutatedPath)
	if ($source -eq $untouched -or $source -eq $mutated -or $untouched -eq $mutated) {
		throw "replay control paths must be distinct"
	}
	if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "replay control source is missing: $source" }
	if ((Test-Path -LiteralPath $untouched) -or (Test-Path -LiteralPath $mutated)) {
		throw "replay control outputs must not already exist"
	}

	$before = [IO.File]::ReadAllBytes($source)
	if ($before.Length -lt 6 -or [Text.Encoding]::ASCII.GetString($before, 0, 6) -ne "GENREP") {
		throw "replay control source has invalid GENREP header magic"
	}
	$headerLength = [Math]::Min($before.Length, 4096)
	$header = [Text.Encoding]::ASCII.GetString($before, 0, $headerLength)
	$matches = [regex]::Matches($header, "(?:^|;)SD=(\d+);")
	if ($matches.Count -ne 1) { throw "replay control needs exactly one valid SD= decimal header field; found $($matches.Count)" }
	$match = $matches[0]
	$digits = $match.Groups[1]
	$offset = $digits.Index + $digits.Length - 1
	$oldByte = $before[$offset]
	$newByte = [byte](([int] $oldByte - [int] [char] '0' + 1) % 10 + [int] [char] '0')

	$after = [byte[]] $before.Clone()
	$after[$offset] = $newByte
	[IO.File]::WriteAllBytes($untouched, $before)
	[IO.File]::WriteAllBytes($mutated, $after)

	$changed = 0
	for ($i = 0; $i -lt $before.Length; $i++) { if ($before[$i] -ne $after[$i]) { $changed++ } }
	$untouchedBytes = [IO.File]::ReadAllBytes($untouched)
	$mutatedBytes = [IO.File]::ReadAllBytes($mutated)
	$valid = $before.Length -eq $after.Length -and $changed -eq 1 -and
		(Get-ReplayControlSha256 $before) -eq (Get-ReplayControlSha256 $untouchedBytes) -and
		$mutatedBytes.Length -eq $before.Length -and $mutatedBytes[$offset] -eq $newByte

	return [pscustomobject]@{
		Valid = $valid
		Source = $source
		Untouched = $untouched
		Mutated = $mutated
		LengthBefore = $before.Length
		LengthAfter = $after.Length
		ChangedByteCount = $changed
		ChangedOffset = $offset
		OldDigit = [char] $oldByte
		NewDigit = [char] $newByte
		BeforeSha256 = Get-ReplayControlSha256 $before
		UntouchedSha256 = Get-ReplayControlSha256 $untouchedBytes
		AfterSha256 = Get-ReplayControlSha256 $mutatedBytes
		HeaderFieldCount = $matches.Count
	}
}

function Test-ReplayControlCompletedRun($Run, [int] $ExpectedFrame)
{
	return $null -ne $Run -and -not $Run.TimedOut -and $Run.ExitCode -eq 0 -and
		$Run.CRC -match '^0x[0-9A-Fa-f]+$' -and $Run.Frame -eq $ExpectedFrame -and
		-not [string]::IsNullOrWhiteSpace([string] $Run.Result)
}

function Get-ReplayControlClassification(
	$Live,
	$Untouched,
	$Repeat,
	$Mutated,
	$Mutation,
	[int] $ExpectedFrame)
{
	if (-not (Test-ReplayControlCompletedRun $Live $ExpectedFrame)) {
		return [pscustomobject]@{ Code = "positive_live_invalid"; ControlDetected = $false }
	}
	if ($Live.Built -le 0) { return [pscustomobject]@{ Code = "positive_idle"; ControlDetected = $false } }
	if (-not (Test-ReplayControlCompletedRun $Untouched $ExpectedFrame)) {
		return [pscustomobject]@{ Code = "positive_untouched_invalid"; ControlDetected = $false }
	}
	if ($Untouched.CRC -ne $Live.CRC -or $Untouched.Frame -ne $Live.Frame) {
		return [pscustomobject]@{ Code = "positive_untouched_mismatch"; ControlDetected = $false }
	}
	if (-not (Test-ReplayControlCompletedRun $Repeat $ExpectedFrame)) {
		return [pscustomobject]@{ Code = "positive_repeat_invalid"; ControlDetected = $false }
	}
	if ($Repeat.CRC -ne $Live.CRC -or $Repeat.Frame -ne $Live.Frame) {
		return [pscustomobject]@{ Code = "positive_repeat_mismatch"; ControlDetected = $false }
	}
	if ($null -eq $Mutation -or -not $Mutation.Valid -or $Mutation.HeaderFieldCount -ne 1 -or
		$Mutation.ChangedByteCount -ne 1 -or $Mutation.LengthBefore -ne $Mutation.LengthAfter -or
		$Mutation.BeforeSha256 -ne $Mutation.UntouchedSha256 -or $Mutation.BeforeSha256 -eq $Mutation.AfterSha256) {
		return [pscustomobject]@{ Code = "mutation_invalid"; ControlDetected = $false }
	}
	if ($null -ne $Mutated -and $Mutated.TimedOut) {
		return [pscustomobject]@{ Code = "mutated_timeout"; ControlDetected = $false }
	}
	if ($null -eq $Mutated -or $Mutated.ExitCode -ne 0) {
		return [pscustomobject]@{ Code = "mutated_exit"; ControlDetected = $false }
	}
	if ([string]::IsNullOrWhiteSpace([string] $Mutated.Result) -or $Mutated.CRC -notmatch '^0x[0-9A-Fa-f]+$') {
		return [pscustomobject]@{ Code = "mutated_missing_result"; ControlDetected = $false }
	}
	if ($Mutated.Frame -ne $ExpectedFrame) {
		return [pscustomobject]@{ Code = "mutated_wrong_frame"; ControlDetected = $false }
	}
	if ($Mutated.CRC -eq $Live.CRC) {
		return [pscustomobject]@{ Code = "mutated_same_crc"; ControlDetected = $false }
	}
	return [pscustomobject]@{ Code = "control_detected"; ControlDetected = $true }
}

