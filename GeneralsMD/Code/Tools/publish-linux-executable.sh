#!/usr/bin/env bash
# Copyright 2026. Additional terms under GNU GPL section 7 apply: see LICENSE.md.
# Publish a stripped Linux executable with its matching small runtime overlay.
# Existing Reforged*.big art and unrelated overlay entries stay in place.
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

code="$(cd "$(dirname "$0")/.." && pwd)"
bin="$(cd "$(dirname "$destination_exe")" && pwd -P)"
[ "$(basename "$bin")" = bin ] || fail "destination must be in a package's bin directory"
package="$(dirname "$bin")"
executable="bin/$(basename "$destination_exe")"
overlay="share/zero-hour-reforged/overlay"

# Do not follow an installed overlay or its parents back into another checkout.
for parent in share share/zero-hour-reforged "$overlay" "$overlay/Data" "$overlay/Art"; do
  [ ! -L "$package/$parent" ] || fail "cannot publish through symbolic link $package/$parent"
  [ ! -e "$package/$parent" ] || [ -d "$package/$parent" ] \
    || fail "not a directory: $package/$parent"
done

transaction=$(mktemp -d "$package/.thin-publish-backup.XXXXXX")
publishing=0
changed=()
existed=()
cleanup() {
  local status=$? index path rollback_failed=0
  trap - EXIT HUP INT TERM
  if [ "$publishing" -eq 1 ]; then
    # Reverse completed swaps, including the path whose replacement failed.
    for ((index=${#changed[@]}-1; index>=0; index--)); do
      path="${changed[index]}"
      if [ -e "$transaction/previous/$path" ] || [ -L "$transaction/previous/$path" ]; then
        rm -rf -- "$package/$path" && \
          mv -T -- "$transaction/previous/$path" "$package/$path" || rollback_failed=1
      elif [ "${existed[index]}" -eq 0 ]; then
        rm -rf -- "$package/$path" || rollback_failed=1
      fi
    done
    if [ "$rollback_failed" -eq 1 ]; then
      echo "[publish] ERROR: rollback incomplete; recovery files remain at $transaction" >&2
    else
      echo "[publish] failed; previous executable and runtime overlay restored" >&2
    fi
  fi
  if [ -n "$transaction" ] && [ "$rollback_failed" -eq 0 ]; then rm -rf -- "$transaction"; fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Finish all preparation before replacing any installed file. The empty Run argument deliberately
# leaves the large art archives alone; stage-overlay.sh owns the data list and INI line endings.
mkdir -p "$transaction/next/bin"
bash "$code/Tools/stage-overlay.sh" "$code/Data" "" "$transaction/next/$overlay"
strip --strip-all -o "$transaction/next/$executable" -- "$source_exe"
chmod 755 "$transaction/next/$executable"

# These are exactly the small paths owned by stage-overlay.sh, not the entire overlay. Replacing
# directories also removes files deleted upstream while preserving other Data/Art folders and art.
owned=(Data/INI Data/Patch.str Data/Scripts Data/Turkish Data/German Install_Final.bmp Art/Textures Window)
paths=()
for path in "${owned[@]}"; do paths+=("$overlay/$path"); done
paths+=("$executable")
publishing=1
for path in "${paths[@]}"; do
  mkdir -p -- "$(dirname "$package/$path")" "$(dirname "$transaction/previous/$path")"
  changed+=("$path")
  if [ -e "$package/$path" ] || [ -L "$package/$path" ]; then
    existed+=(1)
    mv -T -- "$package/$path" "$transaction/previous/$path"
  else
    existed+=(0)
  fi
  if [ -e "$transaction/next/$path" ] || [ -L "$transaction/next/$path" ]; then
    mv -T -- "$transaction/next/$path" "$package/$path"
  fi
done

# Keep the previous managed files for recovery; never remove an older backup implicitly.
rm -rf -- "$transaction/next"
recovery="$transaction"
publishing=0
transaction=""
echo "[publish] done: $package/$executable ($(stat -c %s -- "$package/$executable") bytes), runtime overlay updated"
echo "[publish] previous files: $recovery/previous"
