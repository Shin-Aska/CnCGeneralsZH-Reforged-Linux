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
replay_control_sha256() {
	if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
	else shasum -a 256 "$1" | awk '{print $1}'
	fi
}

replay_control_mutate() {
	local source="$1" untouched="$2" mutated="$3" details before after unchanged changed
	[ "$source" != "$untouched" ] && [ "$source" != "$mutated" ] && [ "$untouched" != "$mutated" ] || return 1
	[ -f "$source" ] && [ ! -e "$untouched" ] && [ ! -e "$mutated" ] || return 1
	cp "$source" "$untouched" || return 1
	details="$(perl - "$source" "$mutated" <<'PERL'
use strict;
use warnings;
my ($source, $mutated) = @ARGV;
open my $in, '<:raw', $source or die "open source: $!";
local $/;
my $data = <$in>;
close $in or die "close source: $!";
die "invalid GENREP header magic\n" unless length($data) >= 6 && substr($data, 0, 6) eq 'GENREP';
my $header = substr($data, 0, 4096);
my @offsets;
while ($header =~ /(?:^|;)SD=(\d+);/g) { push @offsets, $-[1] + length($1) - 1; }
die "expected exactly one valid SD header field, found " . scalar(@offsets) . "\n" unless @offsets == 1;
my $offset = $offsets[0];
my $old = substr($data, $offset, 1);
my $new = (int($old) + 1) % 10;
substr($data, $offset, 1, $new);
open my $out, '>:raw', $mutated or die "open mutated: $!";
print {$out} $data or die "write mutated: $!";
close $out or die "close mutated: $!";
print "$offset $old $new " . scalar(@offsets) . "\n";
PERL
)" || return 1
	read -r RC_MUTATION_OFFSET RC_MUTATION_OLD RC_MUTATION_NEW RC_MUTATION_FIELDS <<< "$details"
	before="$(replay_control_sha256 "$source")" || return 1
	unchanged="$(replay_control_sha256 "$untouched")" || return 1
	after="$(replay_control_sha256 "$mutated")" || return 1
	RC_MUTATION_LENGTH_BEFORE="$(wc -c < "$source" | tr -d ' ')"
	RC_MUTATION_LENGTH_AFTER="$(wc -c < "$mutated" | tr -d ' ')"
	changed="$(cmp -l "$source" "$mutated" | wc -l | tr -d ' ')"
	RC_MUTATION_CHANGED="$changed"
	RC_MUTATION_BEFORE_SHA="$before"
	RC_MUTATION_UNTOUCHED_SHA="$unchanged"
	RC_MUTATION_AFTER_SHA="$after"
	RC_MUTATION_VALID=0
	if [ "$RC_MUTATION_FIELDS" -eq 1 ] && [ "$changed" -eq 1 ] &&
		[ "$RC_MUTATION_LENGTH_BEFORE" -eq "$RC_MUTATION_LENGTH_AFTER" ] &&
		[ "$before" = "$unchanged" ] && [ "$before" != "$after" ]; then RC_MUTATION_VALID=1; fi
	[ "$RC_MUTATION_VALID" -eq 1 ]
}

replay_control_valid_crc() { printf '%s\n' "$1" | grep -Eq '^0x[0-9A-Fa-f]+$'; }
replay_control_valid_run() {
	local prefix="$1" expected="$2" crc frame result status timeout
	eval "crc=\${${prefix}_CRC:-}; frame=\${${prefix}_FRAME:--1}; result=\${${prefix}_RESULT:-}; status=\${${prefix}_STATUS:-1}; timeout=\${${prefix}_TIMEOUT:-0}"
	[ "$timeout" -eq 0 ] && [ "$status" -eq 0 ] && replay_control_valid_crc "$crc" &&
		[ "$frame" -eq "$expected" ] && [ -n "$result" ]
}

replay_control_classify() {
	local expected="$1"
	RC_CLASSIFICATION=positive_live_invalid; RC_CONTROL_DETECTED=0
	replay_control_valid_run RC_LIVE "$expected" || return 1
	[ "${RC_LIVE_BUILT:-0}" -gt 0 ] || { RC_CLASSIFICATION=positive_idle; return 1; }
	RC_CLASSIFICATION=positive_untouched_invalid; replay_control_valid_run RC_BACK "$expected" || return 1
	[ "$RC_BACK_CRC" = "$RC_LIVE_CRC" ] && [ "$RC_BACK_FRAME" -eq "$RC_LIVE_FRAME" ] || { RC_CLASSIFICATION=positive_untouched_mismatch; return 1; }
	RC_CLASSIFICATION=positive_repeat_invalid; replay_control_valid_run RC_AGAIN "$expected" || return 1
	[ "$RC_AGAIN_CRC" = "$RC_LIVE_CRC" ] && [ "$RC_AGAIN_FRAME" -eq "$RC_LIVE_FRAME" ] || { RC_CLASSIFICATION=positive_repeat_mismatch; return 1; }
	RC_CLASSIFICATION=mutation_invalid
	[ "${RC_MUTATION_VALID:-0}" -eq 1 ] && [ "${RC_MUTATION_FIELDS:-0}" -eq 1 ] &&
		[ "${RC_MUTATION_CHANGED:-0}" -eq 1 ] && [ "$RC_MUTATION_LENGTH_BEFORE" -eq "$RC_MUTATION_LENGTH_AFTER" ] &&
		[ "$RC_MUTATION_BEFORE_SHA" = "$RC_MUTATION_UNTOUCHED_SHA" ] && [ "$RC_MUTATION_BEFORE_SHA" != "$RC_MUTATION_AFTER_SHA" ] || return 1
	[ "${RC_CONTROL_TIMEOUT:-0}" -eq 0 ] || { RC_CLASSIFICATION=mutated_timeout; return 1; }
	[ "${RC_CONTROL_STATUS:-1}" -eq 0 ] || { RC_CLASSIFICATION=mutated_exit; return 1; }
	replay_control_valid_crc "${RC_CONTROL_CRC:-}" && [ -n "${RC_CONTROL_RESULT:-}" ] || { RC_CLASSIFICATION=mutated_missing_result; return 1; }
	[ "${RC_CONTROL_FRAME:--1}" -eq "$expected" ] || { RC_CLASSIFICATION=mutated_wrong_frame; return 1; }
	[ "$RC_CONTROL_CRC" != "$RC_LIVE_CRC" ] || { RC_CLASSIFICATION=mutated_same_crc; return 1; }
	RC_CLASSIFICATION=control_detected; RC_CONTROL_DETECTED=1
	return 0
}

