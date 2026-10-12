#!/usr/bin/env bash
# Copyright 2026. Additional terms under GNU GPL section 7 apply: see LICENSE.md.
# Publish only a stripped copy of a built Linux executable; keep its debug source and overlay intact.
# Usage: bash publish-linux-executable.sh <built-generals> <destination-generals>

set -euo pipefail

fail() { echo "[publish] ERROR: $*" >&2; exit 1; }
[ "$#" -eq 2 ] || fail "usage: $0 <built-generals> <destination-generals>"
[ "$(uname -s)" = Linux ] || fail "this script publishes Linux executables"
command -v strip >/dev/null || fail "strip is required"
source_exe="$1"
destination_exe="$2"
[ -f "$source_exe" ] && [ -x "$source_exe" ] || fail "no built executable at $source_exe"
mkdir -p -- "$(dirname "$destination_exe")"
[ "$(readlink -f -- "$source_exe")" != "$(readlink -f -- "$destination_exe")" ] \
  || fail "the destination must differ from the build executable"

# Strip into the destination filesystem, then rename so a failed strip leaves the installed game intact.
temporary_exe=$(mktemp "$(dirname "$destination_exe")/.generals-publish.XXXXXX")
trap 'rm -f -- "$temporary_exe"' EXIT
strip --strip-all -o "$temporary_exe" -- "$source_exe"
chmod 755 "$temporary_exe"
mv -fT -- "$temporary_exe" "$destination_exe"
echo "[publish] done: $destination_exe ($(stat -c %s -- "$destination_exe") bytes)"
