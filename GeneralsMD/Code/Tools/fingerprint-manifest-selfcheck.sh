#!/usr/bin/env bash
# Exercise the real manifest CLI only in a fresh private Git repository.
# Optional second argument: an already-built build_fingerprint executable, for content/CRLF checks.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="${1:-$HERE/fingerprint-manifest.sh}"
TOOL="$(cd "$(dirname "$TOOL")" && pwd)/$(basename "$TOOL")"
FINGERPRINT="${2:-}"
if [ -n "$FINGERPRINT" ]; then
	FINGERPRINT="$(cd "$(dirname "$FINGERPRINT")" && pwd)/$(basename "$FINGERPRINT")"
fi
WORK_PARENT="$(cd "${TMPDIR:-/tmp}" && pwd -P)" || exit 2
WORK="$(mktemp -d "$WORK_PARENT/fingerprint-manifest-selfcheck.XXXXXX")" || exit 2
cleanup() {
	local target
	target="$(cd "$WORK" && pwd -P)" || exit 2
	case "$target" in
		"$WORK_PARENT"/fingerprint-manifest-selfcheck.??????) rm -rf -- "$target" ;;
		*) echo 'FAIL: refusing cleanup outside the private fixture' >&2; exit 2 ;;
	esac
	if [ -e "$target" ]; then echo 'FAIL: private fixture cleanup failed' >&2; exit 2; fi
	printf 'CLEANUP: removed private fixture %s\n' "$target"
}
trap cleanup EXIT
unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES
REPO="$WORK/repo with spaces"
CODE="$REPO/GeneralsMD/Code"
mkdir -p "$CODE/Tools" "$CODE/Source" "$CODE/Libraries/Source/FFmpeg/dist" "$CODE/Libraries/Source/FFmpeg/dist-arm64"
cp "$TOOL" "$CODE/Tools/fingerprint-manifest.sh"
printf 'first\n' > "$CODE/Source/tracked.cpp"
printf 'second\n' > "$CODE/Source/with spaces.h"
printf 'generated\n' > "$CODE/Libraries/Source/FFmpeg/dist/generated.h"
printf 'generated\n' > "$CODE/Libraries/Source/FFmpeg/dist-arm64/generated.h"
printf 'ignored/\n' > "$CODE/.gitignore"
git init -q "$REPO" || exit 2
git -C "$REPO" -c core.autocrlf=false add -- GeneralsMD/Code || exit 2
MANIFEST="$CODE/BuildFingerprint.manifest"
PRIVATE_TOOL="$CODE/Tools/fingerprint-manifest.sh"
PASS=0; FAIL=0
check() {
	local label="$1"; shift
	if "$@"; then printf 'PASS: %s\n' "$label"; PASS=$((PASS + 1))
	else printf 'FAIL: %s\n' "$label"; FAIL=$((FAIL + 1)); fi
}
run() {
	local expected="$1"; shift
	"$@" > "$WORK/last-output.txt" 2>&1
	local actual=$?
	if [ "$actual" -ne "$expected" ]; then
		printf 'expected exit %s, actual %s: ' "$expected" "$actual"; printf '%q ' "$@"; printf '\n'
		cat "$WORK/last-output.txt"
		return 1
	fi
}
has() { grep -Fqx -- "$1" "$MANIFEST"; }
missing() { ! has "$1"; }
unchanged() { cmp -s -- "$1" "$2"; }
value() { "$FINGERPRINT" --print "$CODE" "$MANIFEST" | awk '{print $1}'; }

git -C "$CODE" ls-files -z . | tr '\0' '\n' | grep -v '^Libraries/Source/FFmpeg/dist/' \
	| grep -v '^Libraries/Source/FFmpeg/dist-arm64/' | grep -v '^BuildFingerprint\.manifest$' | LC_ALL=C sort -u > "$WORK/legacy-list"
check 'default generation retains the legacy tracked list' run 0 bash "$PRIVATE_TOOL"
check 'default bytes equal the original tracked-only contract' unchanged "$WORK/legacy-list" "$MANIFEST"
check 'default check succeeds in a current tracked fixture' run 0 bash "$PRIVATE_TOOL" --check
cp "$MANIFEST" "$WORK/tracked-list"
printf 'untracked\n' > "$CODE/Source/new water.cpp"
mkdir -p "$CODE/ignored"
printf 'ignore me\n' > "$CODE/ignored/hidden.h"
printf 'exclude me\n' > "$CODE/Libraries/Source/FFmpeg/dist/new.h"
check 'default check ignores nonignored untracked source' run 0 bash "$PRIVATE_TOOL" --check
check 'explicit working-tree check detects an added untracked path' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
check 'working-tree check does not rewrite a stale manifest' unchanged "$WORK/tracked-list" "$MANIFEST"
check 'explicit working-tree generation succeeds' run 0 bash "$PRIVATE_TOOL" --include-untracked
check 'working-tree generation includes an untracked path with spaces' has 'Source/new water.cpp'
check 'vendor generated files stay excluded' missing 'Libraries/Source/FFmpeg/dist/new.h'
check 'ignored files stay excluded' missing 'ignored/hidden.h'
check 'working-tree check succeeds with reversed option order' run 0 bash "$PRIVATE_TOOL" --check --include-untracked
cp "$MANIFEST" "$WORK/working-list"
check 'working-tree generation is deterministic' run 0 bash "$PRIVATE_TOOL" --include-untracked
check 'a second generation is byte-identical' unchanged "$WORK/working-list" "$MANIFEST"
rm -- "$CODE/Source/new water.cpp"
check 'working-tree check detects removal of an untracked path' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
printf 'untracked\n' > "$CODE/Source/new water.cpp"
mv -- "$CODE/Source/tracked.cpp" "$CODE/Source/renamed.cpp"
check 'working-tree check detects a removed tracked path and untracked rename' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
check 'working-tree generation accepts a working-tree deletion' run 0 bash "$PRIVATE_TOOL" --include-untracked
check 'deleted tracked path is absent from the working-tree list' missing 'Source/tracked.cpp'
check 'untracked renamed source is present' has 'Source/renamed.cpp'
check 'renamed working-tree list checks successfully' run 0 bash "$PRIVATE_TOOL" --include-untracked --check

if [ -n "$FINGERPRINT" ]; then
	before="$(value)"
	printf 'changed byte\n' >> "$CODE/Source/renamed.cpp"
	after="$(value)"
	check 'content fingerprint detects modified untracked source bytes' test -n "$before"
	check 'modified bytes change the existing content fingerprint' test "$before" != "$after"
	check 'a byte edit keeps the path-only roster current' run 0 bash "$PRIVATE_TOOL" --include-untracked --check
	while IFS= read -r line; do printf '%s\r\n' "$line"; done < "$CODE/Source/renamed.cpp" > "$WORK/crlf-source"
	cp "$WORK/crlf-source" "$CODE/Source/renamed.cpp"
	crlf="$(value)"
	check 'existing content fingerprint normalizes source CRLF' test "$after" = "$crlf"
fi

cp "$MANIFEST" "$WORK/good-list"
head -1 "$WORK/good-list" >> "$MANIFEST"
check 'working-tree check rejects duplicate manifest entries' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
cp "$WORK/good-list" "$MANIFEST"
printf '../outside.cpp\n' >> "$MANIFEST"
check 'working-tree check rejects traversal manifest entries' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
cp "$WORK/good-list" "$MANIFEST"
printf '/absolute.cpp\n' >> "$MANIFEST"
check 'working-tree check rejects absolute manifest entries' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
cp "$WORK/good-list" "$MANIFEST"
printf 'Source\\windows.cpp\n' >> "$MANIFEST"
check 'working-tree check rejects backslash manifest paths' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
cp "$WORK/good-list" "$MANIFEST"
printf 'Source/bad;list.cpp\n' >> "$MANIFEST"
check 'working-tree check rejects CMake list separators' run 1 bash "$PRIVATE_TOOL" --include-untracked --check
cp "$WORK/good-list" "$MANIFEST"
while IFS= read -r line; do printf '%s\r\n' "$line"; done < "$WORK/good-list" > "$WORK/crlf-list"
cp "$WORK/crlf-list" "$MANIFEST"
check 'working-tree check accepts CRLF manifest text' run 0 bash "$PRIVATE_TOOL" --include-untracked --check
check 'working-tree check does not normalize the checked file in place' unchanged "$WORK/crlf-list" "$MANIFEST"
cp "$WORK/good-list" "$MANIFEST"
check 'unknown options fail without rewriting the manifest' run 2 bash "$PRIVATE_TOOL" --bogus
check 'invalid option preserves prior manifest bytes' unchanged "$WORK/good-list" "$MANIFEST"
check 'duplicate mode switches are rejected' run 2 bash "$PRIVATE_TOOL" --include-untracked --include-untracked
check 'duplicate check switches are rejected' run 2 bash "$PRIVATE_TOOL" --check --check
check 'help is a read-only successful CLI route' run 0 bash "$PRIVATE_TOOL" --help
check 'help preserves manifest bytes' unchanged "$WORK/good-list" "$MANIFEST"

mv -- "$CODE/Source/with spaces.h" "$WORK/with spaces.h"
mkdir -- "$CODE/Source/with spaces.h"
check 'working-tree generation rejects a tracked nonregular replacement' run 1 bash "$PRIVATE_TOOL" --include-untracked
check 'failed generation preserves the old manifest' unchanged "$WORK/good-list" "$MANIFEST"
rmdir -- "$CODE/Source/with spaces.h"
mv -- "$WORK/with spaces.h" "$CODE/Source/with spaces.h"
printf 'newline\n' > "$CODE/Source/unsupported"$'\n'"name.cpp"
literal_newline=0
while IFS= read -r -d '' candidate; do
	case "$candidate" in *$'\n'*) literal_newline=1 ;; esac
done < <(git -C "$CODE" ls-files --others --exclude-standard -z -- Source)
if [ "$literal_newline" -eq 1 ]; then
	check 'working-tree generation rejects a native newline filename' run 1 bash "$PRIVATE_TOOL" --include-untracked
else
	printf 'UNRUN: native newline filename is unavailable; Windows maps it to U+F00A\n'
fi
check 'newline rejection preserves the old manifest' unchanged "$WORK/good-list" "$MANIFEST"
rm -- "$CODE/Source/unsupported"$'\n'"name.cpp"
ln -s 'with spaces.h' "$CODE/Source/link.h" > "$WORK/symlink-output" 2>&1
if [ -L "$CODE/Source/link.h" ]; then
	check 'working-tree generation rejects a native symbolic link' run 1 bash "$PRIVATE_TOOL" --include-untracked
	check 'symbolic-link rejection preserves the manifest' unchanged "$WORK/good-list" "$MANIFEST"
else
	printf 'UNRUN: native symbolic links are unavailable in this shell\n'
fi
rm -f -- "$CODE/Source/link.h"
printf 'cmake\n' > "$CODE/Source/bad;list.cpp"
check 'working-tree generation rejects a CMake separator filename' run 1 bash "$PRIVATE_TOOL" --include-untracked
rm -- "$CODE/Source/bad;list.cpp"
blob="$(printf 'conflict\n' | git -C "$REPO" hash-object -w --stdin)"
git -C "$REPO" update-index --force-remove 'GeneralsMD/Code/Source/with spaces.h'
printf '100644 %s 1\tGeneralsMD/Code/Source/with spaces.h\n100644 %s 2\tGeneralsMD/Code/Source/with spaces.h\n100644 %s 3\tGeneralsMD/Code/Source/with spaces.h\n' "$blob" "$blob" "$blob" | git -C "$REPO" update-index --index-info
cp "$MANIFEST" "$WORK/pre-unmerged-list"
check 'default check retains unmerged-index refusal' run 1 bash "$PRIVATE_TOOL" --check
check 'working-tree generation refuses an unmerged index' run 1 bash "$PRIVATE_TOOL" --include-untracked
check 'unmerged-index refusal preserves manifest bytes' unchanged "$WORK/pre-unmerged-list" "$MANIFEST"

mv -- "$REPO/.git" "$WORK/private-git"
cp "$MANIFEST" "$WORK/pre-archive-list"
check 'archive default check remains explicit unavailable 77' run 77 bash "$PRIVATE_TOOL" --check
check 'archive working-tree check is explicit unavailable 77' run 77 bash "$PRIVATE_TOOL" --include-untracked --check
check 'archive generation leaves shipped manifest unchanged' run 77 bash "$PRIVATE_TOOL" --include-untracked
check 'archive manifest bytes remain unchanged' unchanged "$WORK/pre-archive-list" "$MANIFEST"
printf 'RESULT: %s passed, %s failed; cleanup follows\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
