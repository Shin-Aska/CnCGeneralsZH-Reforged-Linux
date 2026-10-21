#!/usr/bin/env bash
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
set -u
. "$(cd "$(dirname "$0")" && pwd)/replay-control.sh"

failed=0
passed=0
check() {
	if eval "$1"; then echo "ok: $2"; passed=$((passed + 1))
	else echo "FAIL: $2"; failed=$((failed + 1))
	fi
}
set_run() {
	local prefix="$1" crc="${2:-0x11111111}" frame="${3:-2400}" result="${4:-frame limit}" built="${5:-1}" status="${6:-0}" timeout="${7:-0}"
	eval "${prefix}_CRC=\$crc; ${prefix}_FRAME=\$frame; ${prefix}_RESULT=\$result; ${prefix}_BUILT=\$built; ${prefix}_STATUS=\$status; ${prefix}_TIMEOUT=\$timeout"
}

work="$(mktemp -d "${TMPDIR:-/tmp}/replay-control-selfcheck.XXXXXX")" || exit 2
trap 'rm -rf -- "${work:?}"' EXIT
printf 'GENREP\000US=1;SD=19;C=100;\000payload' > "$work/source.rep"
replay_control_mutate "$work/source.rep" "$work/untouched.rep" "$work/mutated.rep"
check '[ "$RC_MUTATION_VALID" -eq 1 ] && [ "$RC_MUTATION_FIELDS" -eq 1 ] && [ "$RC_MUTATION_CHANGED" -eq 1 ]' "one valid SD field changes exactly one byte"
check '[ "$RC_MUTATION_OLD" = 9 ] && [ "$RC_MUTATION_NEW" = 0 ] && [ "$RC_MUTATION_LENGTH_BEFORE" -eq "$RC_MUTATION_LENGTH_AFTER" ]' "final digit changes modulo ten without changing length"
check '[ "$RC_MUTATION_BEFORE_SHA" = "$RC_MUTATION_UNTOUCHED_SHA" ] && [ "$RC_MUTATION_BEFORE_SHA" != "$RC_MUTATION_AFTER_SHA" ]' "before, untouched and after hashes prove identity"
if replay_control_mutate "$work/source.rep" "$work/untouched.rep" "$work/mutated.rep" 2>/dev/null; then stale_rejected=0; else stale_rejected=1; fi
check '[ "$stale_rejected" -eq 1 ]' "existing outputs are rejected instead of overwritten"

set_run RC_LIVE
set_run RC_BACK
set_run RC_AGAIN
set_run RC_CONTROL 0x22222222
replay_control_classify 2400
check '[ "$RC_CLASSIFICATION" = control_detected ] && [ "$RC_CONTROL_DETECTED" -eq 1 ]' "completed expected-frame CRC divergence detects the control"

run_case() {
	local expected="$1" setup="$2" label="${3:-$1}"
	set_run RC_LIVE; set_run RC_BACK; set_run RC_AGAIN; set_run RC_CONTROL 0x22222222
	RC_MUTATION_VALID=1; RC_MUTATION_FIELDS=1; RC_MUTATION_CHANGED=1
	RC_MUTATION_LENGTH_BEFORE=20; RC_MUTATION_LENGTH_AFTER=20
	RC_MUTATION_BEFORE_SHA=a; RC_MUTATION_UNTOUCHED_SHA=a; RC_MUTATION_AFTER_SHA=b
	eval "$setup"
	replay_control_classify 2400 >/dev/null 2>&1 || true
	check '[ "$RC_CLASSIFICATION" = "$expected" ] && [ "$RC_CONTROL_DETECTED" -eq 0 ]' "classification rejects $label"
}
run_case positive_live_invalid 'RC_LIVE_STATUS=2'
run_case positive_idle 'RC_LIVE_BUILT=0'
run_case positive_untouched_invalid 'RC_BACK_RESULT='
run_case positive_untouched_mismatch 'RC_BACK_CRC=0x33333333'
run_case positive_repeat_invalid 'RC_AGAIN_TIMEOUT=1'
run_case positive_repeat_mismatch 'RC_AGAIN_CRC=0x33333333'
run_case mutation_invalid 'RC_MUTATION_VALID=0'
run_case mutated_timeout 'RC_CONTROL_TIMEOUT=1'
run_case mutated_exit 'RC_CONTROL_STATUS=1; RC_CONTROL_CRC=; RC_CONTROL_RESULT=' loader_rejection
run_case mutated_exit 'RC_CONTROL_STATUS=139; RC_CONTROL_CRC=0x22222222' crash
run_case mutated_exit 'RC_CONTROL_STATUS=7; RC_CONTROL_CRC=0x22222222' misleading_nonzero_exit
run_case mutated_missing_result 'RC_CONTROL_RESULT='
run_case mutated_wrong_frame 'RC_CONTROL_FRAME=2399'
run_case mutated_same_crc 'RC_CONTROL_CRC=0x11111111'
run_case mutated_timeout 'RC_CONTROL_TIMEOUT=1' repeated_interrupted_control

for kind in malformed missing ambiguous; do
	case "$kind" in
		malformed) bytes='NOTREP\000US=1;SD=1;' ;;
		missing) bytes='GENREP\000US=1;C=100;' ;;
		ambiguous) bytes='GENREP\000US=1;SD=1;C=100;SD=2;' ;;
	esac
	printf '%b' "$bytes" > "$work/$kind.rep"
	if replay_control_mutate "$work/$kind.rep" "$work/$kind.u" "$work/$kind.m" 2>/dev/null; then rejected=0; else rejected=1; fi
	check '[ "$rejected" -eq 1 ]' "mutation rejects $kind header"
done

echo "$passed passed, $failed failed"
exit "$failed"

