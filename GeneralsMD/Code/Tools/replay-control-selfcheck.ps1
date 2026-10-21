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

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "replay-control.ps1")

$failed = 0
$passed = 0
function Check([bool] $Condition, [string] $Name)
{
	if ($Condition) { Write-Host "ok: $Name"; $script:passed++ }
	else { Write-Host "FAIL: $Name"; $script:failed++ }
}
function Run([string] $CRC = "0x11111111", [int] $Frame = 2400, [string] $Result = "frame limit",
	[int] $Built = 1, [int] $ExitCode = 0, [bool] $TimedOut = $false)
{
	return [pscustomobject]@{ CRC = $CRC; Frame = $Frame; Result = $Result; Built = $Built; ExitCode = $ExitCode; TimedOut = $TimedOut }
}

$temp = Join-Path ([IO.Path]::GetTempPath()) ("replay-control-selfcheck-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
	$source = Join-Path $temp "source.rep"
	$untouched = Join-Path $temp "untouched.rep"
	$mutated = Join-Path $temp "mutated.rep"
	[IO.File]::WriteAllBytes($source, [Text.Encoding]::ASCII.GetBytes("GENREP`0US=1;SD=19;C=100;`0payload"))
	$mutation = New-ReplayControlMutation $source $untouched $mutated
	Check ($mutation.Valid -and $mutation.HeaderFieldCount -eq 1 -and $mutation.ChangedByteCount -eq 1) "one valid SD field changes exactly one byte"
	Check ($mutation.OldDigit -eq '9' -and $mutation.NewDigit -eq '0' -and $mutation.LengthBefore -eq $mutation.LengthAfter) "final digit changes modulo ten without changing length"
	Check ($mutation.BeforeSha256 -eq $mutation.UntouchedSha256 -and $mutation.BeforeSha256 -ne $mutation.AfterSha256) "before, untouched and after hashes prove identity"
	$staleRejected = $false
	try { $null = New-ReplayControlMutation $source $untouched $mutated } catch { $staleRejected = $true }
	Check $staleRejected "existing outputs are rejected instead of overwritten"

	$live = Run
	$back = Run
	$again = Run
	$control = Run -CRC "0x22222222"
	$valid = Get-ReplayControlClassification $live $back $again $control $mutation 2400
	Check ($valid.Code -eq "control_detected" -and $valid.ControlDetected) "completed expected-frame CRC divergence detects the control"

	$cases = @(
		@("positive_live_invalid", (Get-ReplayControlClassification (Run -ExitCode 2) $back $again $control $mutation 2400)),
		@("positive_idle", (Get-ReplayControlClassification (Run -Built 0) $back $again $control $mutation 2400)),
		@("positive_untouched_invalid", (Get-ReplayControlClassification $live (Run -Result "") $again $control $mutation 2400)),
		@("positive_untouched_mismatch", (Get-ReplayControlClassification $live (Run -CRC "0x33333333") $again $control $mutation 2400)),
		@("positive_repeat_invalid", (Get-ReplayControlClassification $live $back (Run -TimedOut $true) $control $mutation 2400)),
		@("positive_repeat_mismatch", (Get-ReplayControlClassification $live $back (Run -CRC "0x33333333") $control $mutation 2400)),
		@("mutation_invalid", (Get-ReplayControlClassification $live $back $again $control ([pscustomobject]@{ Valid=$false }) 2400)),
		@("mutated_timeout", (Get-ReplayControlClassification $live $back $again (Run -TimedOut $true) $mutation 2400)),
		@("mutated_exit", (Get-ReplayControlClassification $live $back $again $null $mutation 2400), "loader_rejection"),
		@("mutated_exit", (Get-ReplayControlClassification $live $back $again (Run -CRC "0x22222222" -ExitCode -1073741819) $mutation 2400), "crash"),
		@("mutated_exit", (Get-ReplayControlClassification $live $back $again (Run -CRC "0x22222222" -ExitCode 7) $mutation 2400), "misleading_nonzero_exit"),
		@("mutated_missing_result", (Get-ReplayControlClassification $live $back $again (Run -Result "") $mutation 2400)),
		@("mutated_wrong_frame", (Get-ReplayControlClassification $live $back $again (Run -CRC "0x22222222" -Frame 2399) $mutation 2400)),
		@("mutated_same_crc", (Get-ReplayControlClassification $live $back $again (Run) $mutation 2400))
	)
	foreach ($case in $cases) {
		$label = if ($case.Count -gt 2) { $case[2] } else { $case[0] }
		Check ($case[1].Code -eq $case[0] -and -not $case[1].ControlDetected) ("classification rejects " + $label)
	}
	$timeoutAgain = Get-ReplayControlClassification $live $back $again (Run -TimedOut $true) $mutation 2400
	Check ($timeoutAgain.Code -eq "mutated_timeout" -and -not $timeoutAgain.ControlDetected) "repeated interrupted controls remain failures"

	foreach ($bad in @(
		@("malformed", "NOTREP`0US=1;SD=1;"),
		@("missing", "GENREP`0US=1;C=100;"),
		@("ambiguous", "GENREP`0US=1;SD=1;C=100;SD=2;")
	)) {
		$badSource = Join-Path $temp ($bad[0] + ".rep")
		[IO.File]::WriteAllBytes($badSource, [Text.Encoding]::ASCII.GetBytes($bad[1]))
		$threw = $false
		try { $null = New-ReplayControlMutation $badSource (Join-Path $temp ($bad[0] + ".u")) (Join-Path $temp ($bad[0] + ".m")) }
		catch { $threw = $true }
		Check $threw ("mutation rejects " + $bad[0] + " header")
	}
} finally {
	if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Recurse -Force }
}

Write-Host "$passed passed, $failed failed"
exit $(if ($failed -eq 0) { 0 } else { 1 })

