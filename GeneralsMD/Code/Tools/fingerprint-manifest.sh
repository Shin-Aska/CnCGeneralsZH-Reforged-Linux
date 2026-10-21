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
#
# Writes GeneralsMD/Code/BuildFingerprint.manifest: the tracked files the build fingerprint hashes (N1,
# decision 5), from `git ls-files`, relative to GeneralsMD/Code, '/' separators, sorted bytewise.
#
#   fingerprint-manifest.sh            rewrite the manifest (run after adding, removing or renaming a file)
#   fingerprint-manifest.sh --check    exit 1 if the committed manifest is out of date, 77 without git
#   fingerprint-manifest.sh --include-untracked [--check]
#     list the current working tree, including nonignored new files and omitting deleted files.
#     --check validates paths only; build_fingerprint detects changed file contents.
#
# Left out, because a vendoring or build run rewrites their bytes and two checkouts of one commit would
# then disagree: Libraries/Source/FFmpeg/dist/ and dist-arm64/ (Windows' FFmpeg, x64 and ARM64, rebuilt by
# Tools/ffmpeg-build.sh).
# The manifest itself is left out too (it would hash its own list).
#
# Refused while the index has unmerged entries (a merge or rebase stopped on a conflict): `git ls-files`
# lists an unmerged path once per stage, so a manifest written then names it up to three times, and a
# check run then compares against a list that is not a commit's.  The list is deduplicated anyway, and
# --check names any line the committed manifest holds more than once.

set -euo pipefail

CODE="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$CODE/BuildFingerprint.manifest"
CHECK=0
INCLUDE_UNTRACKED=0
for argument in "$@"; do
	case "$argument" in
		--check) [ "$CHECK" -eq 0 ] || { echo 'FAIL: duplicate --check' >&2; exit 2; }; CHECK=1 ;;
		--include-untracked) [ "$INCLUDE_UNTRACKED" -eq 0 ] || { echo 'FAIL: duplicate --include-untracked' >&2; exit 2; }; INCLUDE_UNTRACKED=1 ;;
		--help) echo 'usage: fingerprint-manifest.sh [--include-untracked] [--check]'; exit 0 ;;
		*) echo "FAIL: unknown option: $argument" >&2; exit 2 ;;
	esac
done

if ! command -v git >/dev/null 2>&1 || ! git -C "$CODE" rev-parse --git-dir >/dev/null 2>&1; then
	echo "skip: no git here, so the manifest cannot be checked (it is used as committed)"
	exit 77
fi

if [ -n "$(git -C "$CODE" ls-files -u . 2>/dev/null | head -1)" ]; then
	echo "FAIL: the index has unmerged entries (a merge or rebase stopped on a conflict); resolve and add them, then run this again:"
	git -C "$CODE" ls-files -u . | awk '{print $4}' | LC_ALL=C sort -u | head -10
	exit 1
fi

list() {
	git -C "$CODE" ls-files -z . | tr '\0' '\n' \
		| grep -v '^Libraries/Source/FFmpeg/dist/' \
		| grep -v '^Libraries/Source/FFmpeg/dist-arm64/' \
		| grep -v '^BuildFingerprint\.manifest$' \
		| LC_ALL=C sort -u
}

valid_path() {
	local path="$1" component remaining="$1" prefix="$CODE"
	case "$path" in
		''|/*|*/|*//*|*\\*|*:*|*';'*|*$'\n'*|*$'\r'*) echo "FAIL: invalid manifest path: $path" >&2; return 1 ;;
	esac
	case "/$path/" in
		*/./*|*/../*) echo "FAIL: invalid manifest path: $path" >&2; return 1 ;;
	esac
	while [ -n "$remaining" ]; do
		component="${remaining%%/*}"
		prefix="$prefix/$component"
		if [ -L "$prefix" ]; then echo "FAIL: symbolic link in manifest path: $path" >&2; return 1; fi
		if [ "$remaining" = "$component" ]; then break; fi
		remaining="${remaining#*/}"
	done
}

if [ "$INCLUDE_UNTRACKED" -eq 1 ]; then
	WORK_PARENT="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
	WORK="$(mktemp -d "$WORK_PARENT/fingerprint-manifest.XXXXXX")"
	OUTPUT_TEMP=''
	cleanup() {
		if [ -n "$OUTPUT_TEMP" ]; then rm -f -- "$OUTPUT_TEMP"; fi
		local target
		target="$(cd "$WORK" && pwd -P)" || return
		case "$target" in "$WORK_PARENT"/fingerprint-manifest.??????) rm -rf -- "$target" ;; esac
	}
	trap cleanup EXIT
	git -C "$CODE" ls-files --cached --others --exclude-standard -z -- . > "$WORK/candidates"
	while IFS= read -r -d '' path; do
		case "$path" in
			Libraries/Source/FFmpeg/dist/*|Libraries/Source/FFmpeg/dist-arm64/*|BuildFingerprint.manifest) continue ;;
		esac
		valid_path "$path" || exit 1
		if [ ! -e "$CODE/$path" ]; then continue; fi
		if [ ! -f "$CODE/$path" ]; then echo "FAIL: not a regular source file: $path" >&2; exit 1; fi
		printf '%s\n' "$path"
	done < "$WORK/candidates" > "$WORK/list"
	LC_ALL=C sort -u "$WORK/list" > "$WORK/expected"
	if [ -L "$MANIFEST" ] || { [ -e "$MANIFEST" ] && [ ! -f "$MANIFEST" ]; }; then
		echo 'FAIL: BuildFingerprint.manifest must be a regular file' >&2; exit 1
	fi
	if [ "$CHECK" -eq 1 ]; then
		if [ ! -f "$MANIFEST" ]; then echo 'FAIL: BuildFingerprint.manifest is missing' >&2; exit 1; fi
		while IFS= read -r path || [ -n "$path" ]; do
			path="${path%$'\r'}"
			valid_path "$path" || exit 1
			if [ ! -f "$CODE/$path" ]; then echo "FAIL: listed source file is missing: $path" >&2; exit 1; fi
			printf '%s\n' "$path"
		done < "$MANIFEST" > "$WORK/actual"
		twice="$(LC_ALL=C sort "$WORK/actual" | uniq -d)"
		if [ -n "$twice" ]; then
			echo 'FAIL: BuildFingerprint.manifest lists these more than once:'
			printf '%s\n' "$twice" | head -10
			exit 1
		fi
		if cmp -s -- "$WORK/expected" "$WORK/actual"; then
			echo "ok: BuildFingerprint.manifest lists exactly the working-tree files, including untracked ($(wc -l < "$WORK/expected" | tr -d ' '))"
			exit 0
		fi
		echo 'FAIL: BuildFingerprint.manifest is out of date; run Tools/fingerprint-manifest.sh --include-untracked:'
		diff "$WORK/expected" "$WORK/actual" | head -10 || true
		exit 1
	fi
	OUTPUT_TEMP="$(mktemp "$CODE/.BuildFingerprint.manifest.XXXXXX")"
	cat "$WORK/expected" > "$OUTPUT_TEMP"
	mv -f -- "$OUTPUT_TEMP" "$MANIFEST"
	OUTPUT_TEMP=''
	echo "wrote $MANIFEST ($(wc -l < "$MANIFEST" | tr -d ' ') working-tree files, including untracked)"
	exit 0
fi

if [ "$CHECK" -eq 1 ]; then
	twice="$(LC_ALL=C sort "$MANIFEST" | uniq -d)"
	if [ -n "$twice" ]; then
		echo "FAIL: BuildFingerprint.manifest lists these more than once; run Tools/fingerprint-manifest.sh and commit it:"
		printf '%s\n' "$twice" | head -10
		exit 1
	fi
	if list | cmp -s - "$MANIFEST"; then
		echo "ok: BuildFingerprint.manifest lists exactly the tracked files ($(wc -l < "$MANIFEST" | tr -d ' '))"
		exit 0
	fi
	echo "FAIL: BuildFingerprint.manifest is out of date; run Tools/fingerprint-manifest.sh and commit it:"
	diff <(list) "$MANIFEST" | head -10
	exit 1
fi
list > "$MANIFEST"
echo "wrote $MANIFEST ($(wc -l < "$MANIFEST" | tr -d ' ') files)"
