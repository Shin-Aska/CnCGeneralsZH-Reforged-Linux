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
# Check vendor.sh's CI boundary without network access or third-party/game downloads.
set -euo pipefail
code="$(cd "$(dirname "$0")/.." && pwd)"
temporary=$(mktemp -d "${TMPDIR:-/tmp}/test-vendor-no-art.XXXXXX")
trap 'rm -rf -- "$temporary"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
fixture="$temporary/source tree/GeneralsMD/Code"
sources="$fixture/Libraries/Source"
mkdir -p "$fixture/Tools" "$temporary/bin" "$temporary/tmp"
cp "$code/Tools/vendor.sh" "$fixture/Tools/"

# An already populated dependency tree. Only the sentinels vendor.sh checks are needed;
# no fixture is compiled, installed or mistaken for a usable third-party library.
for entry in \
  Compression/ZLib/deflate.c \
  Compression/LZHCompress/CompLibSource/Lzhl.cpp \
  GameSpy/CMakeLists.txt SDL3/CMakeLists.txt miniaudio/miniaudio.c \
  glslang/glslang/HLSL/hlslParseHelper.cpp SPIRV-Cross/spirv_msl.cpp \
  SDL_shadercross/src/SDL_shadercross.c freetype/src/truetype/ttinterp.c \
  FFmpeg/ffmpeg-8.1.2.tar.xz; do
  mkdir -p "$(dirname "$sources/$entry")"
  printf 'synthetic dependency sentinel\n' > "$sources/$entry"
done
mkdir -p "$sources/Compression/LZHCompress/CompLibHeader" \
  "$sources/GameSpy/include/gamespy" "$sources/SDL3/src/gpu/metal"
printf '// Zero Hour Reforged: altered\n' > "$sources/Compression/LZHCompress/CompLibHeader/_lz.h"
printf '// Zero Hour Reforged: C only\n// Zero Hour Reforged: GSI_UNIX\n' \
  > "$sources/GameSpy/include/gamespy/gsplatform.h"
printf '// ZH_SDL_GPU_METAL_WINDOWLESS\n' > "$sources/SDL3/src/gpu/metal/SDL_gpu_metal.m"

# Stub the external boundaries, not vendor.sh. curl always refuses network access;
# the control run proves the fake can observe art requests when --no-art is absent.
cat > "$temporary/bin/curl" <<'CURL'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$VENDOR_TEST_REQUESTS"
exit 7
CURL
cat > "$temporary/bin/sha256sum" <<'HASH'
#!/usr/bin/env bash
case "${*: -1}" in
  */FFmpeg/ffmpeg-8.1.2.tar.xz)
    printf '%s  %s\n' '464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c' "${*: -1}" ;;
  *) echo 'unexpected fixture hash request' >&2; exit 1 ;;
esac
HASH
cp "$temporary/bin/sha256sum" "$temporary/bin/shasum"
chmod +x "$temporary/bin/"*
export PATH="$temporary/bin:$PATH" TMPDIR="$temporary/tmp"
export ZHR_CHANNEL_URL='https://art.invalid/channel'
export VENDOR_TEST_REQUESTS="$temporary/requests"
: > "$VENDOR_TEST_REQUESTS"

bash "$fixture/Tools/vendor.sh" --no-art > "$temporary/no-art.log" 2>&1 \
  || { cat "$temporary/no-art.log"; fail '--no-art failed'; }
[ ! -s "$VENDOR_TEST_REQUESTS" ] || fail '--no-art queried or downloaded an art resource'
[ ! -e "$fixture/../Run" ] || fail '--no-art created a game-art folder'
grep -q 'skipping upscaled art (--no-art)' "$temporary/no-art.log" \
  || fail '--no-art did not report that art was omitted'
echo 'ok: --no-art makes no art network requests and creates no art folder'

bash "$fixture/Tools/vendor.sh" > "$temporary/default.log" 2>&1 \
  || { cat "$temporary/default.log"; fail 'default vendor run failed'; }
grep -qF 'https://art.invalid/channel/_versions.json' "$VENDOR_TEST_REQUESTS" \
  || fail 'default mode no longer queries the configured channel'
grep -qF 'https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/art-latest/art.json' \
  "$VENDOR_TEST_REQUESTS" || fail 'default mode no longer tries the art-release fallback'
[ "$(wc -l < "$VENDOR_TEST_REQUESTS")" -eq 2 ] || fail 'unexpected dependency or art requests'
echo 'ok: default mode retains the channel and release-manifest lookup'

bash "$fixture/Tools/vendor.sh" --help | grep -q -- '--no-art' \
  || fail '--help does not document --no-art'
echo 'ok: --help documents the CI option'
