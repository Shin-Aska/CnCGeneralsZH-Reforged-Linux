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
# Portable CI tools from their official releases. No system installation.
# Keep URL + SHA256 together; verify cached downloads again before execution.
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 <cmake|appimage> <tools-directory>" >&2; exit 2; }
mode=$1
mkdir -p "$2"
tools=$(cd "$2" && pwd)

fetch() { # URL destination SHA256
  local url=$1 destination=$2 expected=$3
  if [[ -f "$destination" ]] && printf '%s  %s\n' "$expected" "$destination" | sha256sum --check --status; then
    return
  fi
  rm -f -- "$destination" "$destination.part"
  curl --fail --location --proto '=https' --tlsv1.2 --retry 3 --connect-timeout 30 --max-time 600 \
    "$url" -o "$destination.part"
  printf '%s  %s\n' "$expected" "$destination.part" | sha256sum --check --strict
  mv "$destination.part" "$destination"
}

publish_path() { # GitHub environment variable and its absolute path
  printf '%s=%s\n' "$1" "$2"
  if [[ -n ${GITHUB_ENV:-} ]]; then
    printf '%s=%s\n' "$1" "$2" >> "$GITHUB_ENV"
  fi
}

case "$mode" in
  cmake)
    # Last 3.31 maintenance release: retains older dependency policy support;
    # Kitware's Linux tarball runs inside Sniper, unlike the runner's CMake.
    version=3.31.12
    directory="$tools/cmake"
    mkdir -p "$directory"
    archive="$directory/cmake-$version-linux-x86_64.tar.gz"
    fetch "https://github.com/Kitware/CMake/releases/download/v$version/cmake-$version-linux-x86_64.tar.gz" \
      "$archive" 0dc2e9a6860f06bf10bd8fadc03e35d9eeb4df46e33763a7e480e987758f385c
    rm -rf -- "$directory/cmake-$version-linux-x86_64"
    tar -xzf "$archive" -C "$directory"
    "$directory/cmake-$version-linux-x86_64/bin/cmake" --version
    publish_path ZH_CMAKE "$directory/cmake-$version-linux-x86_64/bin/cmake"
    ;;
  appimage)
    # Numbered/dated releases avoid the mutable 'continuous' download URLs.
    directory="$tools/appimage"
    mkdir -p "$directory"
    fetch 'https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage' \
      "$directory/appimagetool-1.9.1-x86_64.AppImage" ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
    fetch 'https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64' \
      "$directory/runtime-20251108-x86_64" 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
    chmod +x "$directory/appimagetool-1.9.1-x86_64.AppImage"
    rm -rf -- "$directory/squashfs-root"
    (cd "$directory" && env -u APPIMAGE_EXTRACT_AND_RUN \
      ./appimagetool-1.9.1-x86_64.AppImage --appimage-extract > /dev/null)
    [[ -x "$directory/squashfs-root/AppRun" ]]
    publish_path ZH_APPIMAGETOOL "$directory/squashfs-root/AppRun"
    publish_path ZH_APPIMAGE_RUNTIME "$directory/runtime-20251108-x86_64"
    ;;
  *) echo "unknown CI tool group: $mode" >&2; exit 2 ;;
esac
