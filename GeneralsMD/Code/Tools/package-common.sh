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
# package-common.sh: what the two packagers share, sourced by both (P1's make-macos-app.sh, P3's
# linux-portable.sh).  Each function reports failure by its status; the caller says why, through its own fail.
#
#   package_link_libraries <build.ninja> <paths file>
#       the static libraries on generals' link line, one name a line (lib<name>.a); their paths go into
#       <paths file>, for the checks that read the libraries themselves
#   package_license_entries <table> <names...>
#       the licence entries those libraries need, with the table's "+" lines; status 1, printing the
#       libraries the table does not name, when any is missing
#   license_entry <entry> <folder>
#       copies that entry's files into <folder>; CODE, REPO and BUILD are the caller's

# package_check_linux_abi <binary> <allow-newer-glibc:0|1> <x86_64|aarch64>
# Print the highest required GLIBC version on success. The portable builder calls this inside the SDK;
# a packaging-only job calls it on the imported binary, without loading or executing that binary.
package_check_linux_abi() {
	local binary="$1" host="${2:-0}" arch="$3" header dynamic symbols libs glibc extra machine
	header="$(LC_ALL=C readelf -h "$binary")" || { echo "cannot read the ELF header of $binary" >&2; return 1; }
	case "$arch" in
		x86_64) machine='Advanced Micro Devices X86-64';;
		aarch64) machine=AArch64;;
		*) echo "unsupported Linux package architecture: $arch" >&2; return 1;;
	esac
	if ! grep -Eq 'Class:[[:space:]]+ELF64' <<< "$header" || ! grep -Eq "Machine:[[:space:]]+$machine$" <<< "$header"; then
		echo "refused: $binary is not an ELF64 $arch binary" >&2; return 1
	fi
	dynamic="$(LC_ALL=C readelf -d "$binary")" && symbols="$(LC_ALL=C objdump -T "$binary")" \
		|| { echo "cannot read $binary's dynamic section or symbols" >&2; return 1; }
	libs="$(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<< "$dynamic")"
	glibc="$(grep -o 'GLIBC_[0-9.]*' <<< "$symbols" | sort -uV | tail -1)"
	[ -n "$glibc" ] || { echo "cannot read $binary's glibc symbols" >&2; return 1; }
	if [ "$host" -ne 1 ] && [ "$(printf '%s\n%s\n' "$glibc" GLIBC_2.31 | sort -V | tail -1)" != GLIBC_2.31 ]; then
		echo "refused: generals needs $glibc, newer than GLIBC_2.31" >&2; return 1
	fi
	if grep -qE 'GLIBCXX_|CXXABI_' <<< "$symbols"; then
		echo "refused: generals needs libstdc++ symbols (it must link it statically)" >&2; return 1
	fi
	extra="$(grep -vE '^(libc\.so\.6|libm\.so\.6|libdl\.so\.2|libpthread\.so\.0|librt\.so\.1|ld-linux-x86-64\.so\.2|ld-linux-aarch64\.so\.1|libfontconfig\.so\.1)$' <<< "$libs" || true)"
	[ -z "$extra" ] || { echo "refused: generals needs shared libraries a SteamOS may lack: $extra" >&2; return 1; }
	printf '%s\n' "$glibc"
}

# package_make_appimage <portable folder> <appimagetool> <type-2 runtime> <architecture>
# The original build and CI's packaging-only jobs use the same AppDir construction. Nothing is downloaded.
package_make_appimage() {
	local folder="$1" tool="$2" runtime="$3" arch="$4" appdir="$1.AppDir" offset
	[ -x "$tool" ] && [ -f "$runtime" ] || { echo "AppImage creation needs an executable appimagetool and a type-2 runtime file" >&2; return 1; }
	rm -rf -- "$appdir" "$folder.AppImage"
	if ! cp -al "$folder" "$appdir" 2>/dev/null; then
		rm -rf -- "$appdir"
		cp -R "$folder" "$appdir" || { echo "cannot make $appdir" >&2; return 1; }
	fi
	rm -f "$appdir/bin/generals.debug" # the debug file stays in the portable tarball
	if ! { cp "$folder/zero-hour-reforged.sh" "$appdir/AppRun" && cp "$folder/share/applications/zero-hour-reforged.desktop" "$appdir/" \
		&& cp "$folder/share/icons/hicolor/128x128/apps/zero-hour-reforged.png" "$appdir/zero-hour-reforged.png" \
		&& ln -s zero-hour-reforged.png "$appdir/.DirIcon"; }; then
		rm -rf -- "$appdir"; echo "cannot complete $appdir" >&2; return 1
	fi
	if ! ARCH="$arch" "$tool" --no-appstream --runtime-file "$runtime" "$appdir" "$folder.AppImage" > "$folder.appimagetool.log" 2>&1; then
		tail -5 "$folder.appimagetool.log" >&2; rm -rf -- "$appdir"; echo "appimagetool failed" >&2; return 1
	fi
	rm -rf -- "$appdir"
	# A type-2 runtime answers this without FUSE or mounting anything.
	offset="$("$folder.AppImage" --appimage-offset 2>/dev/null)" || { echo "the AppImage runtime failed" >&2; return 1; }
	case "$offset" in ''|*[!0-9]*) echo "the AppImage does not answer --appimage-offset: its runtime is not a type-2 runtime" >&2; return 1;; esac
}

package_link_libraries() {
	python3 - "$1" "$2" <<'LINK_EOF'
import re, sys
text = open(sys.argv[1]).read()
m = re.search(r"^build generals: CXX_EXECUTABLE_LINKER.*?\n  LINK_LIBRARIES = (.*?)\n", text, re.S | re.M)
if not m:
    sys.exit("no generals link line in " + sys.argv[1])
names = sorted(set(re.findall(r"(?:^|[\s/])lib([\w\-+]+)\.a(?=\s|$)", m.group(1))))
print("\n".join(names))
with open(sys.argv[2], "w") as paths:		# the libraries themselves
    paths.write("\n".join(sorted(set(t for t in m.group(1).split() if t.endswith(".a")))) + "\n")
LINK_EOF
}

package_license_entries() {
	local table="$1"; shift
	local lib unlicensed=""
	for lib in "$@"; do
		awk -v l="$lib" '$1 == l { found = 1 } END { exit !found }' "$table" || unlicensed="$unlicensed $lib"
	done
	if [ -n "$unlicensed" ]; then
		echo "$unlicensed"
		return 1
	fi
	{ for lib in "$@"; do awk -v l="$lib" '$1 == l { print $2 }' "$table"; done; awk '$1 == "+" { print $2 }' "$table"; } | sort -u
}

license_entry() {
	local L="$2" s="$CODE/Libraries/Source"
	case "$1" in
		game) cp "$REPO/LICENSE.md" "$L/Zero-Hour-Reforged-LICENSE.md";;
		ffmpeg)
			cp "$BUILD/ffmpeg/LICENSE.txt" "$L/FFmpeg-LICENSE.txt" || return 1
			local v; v="$(awk -F= '/^version=/ {print $2; exit}' "$CODE/Tools/ffmpeg-build-posix.sh")"
			printf '%s\n' "FFmpeg $v, statically linked under the LGPL version 2.1 or later." \
				"Source: https://ffmpeg.org/releases/ffmpeg-$v.tar.xz, also kept in this game's repository at" \
				"GeneralsMD/Code/Libraries/Source/FFmpeg/ffmpeg-$v.tar.xz, configured by" \
				"GeneralsMD/Code/Tools/ffmpeg-build-posix.sh (its configure line is the build's)." \
				"To relink the game against another FFmpeg, build it from this game's source:" \
				"https://github.com/olcayseygan/CnCGeneralsZH-Reforged" > "$L/FFmpeg-SOURCE.txt";;
		sdl3) cp "$s/SDL3/LICENSE.txt" "$L/SDL3-LICENSE.txt";;
		shadercross) cp "$s/SDL_shadercross/LICENSE.txt" "$L/SDL_shadercross-LICENSE.txt";;
		freetype)		# dual-licensed; this game takes the GNU GPL (version 2 or later) option, as NOTICE.md says
			cp "$s/freetype/LICENSE.TXT" "$L/FreeType-LICENSE.txt" && cp "$s/freetype/docs/GPLv2.TXT" "$L/FreeType-GPLv2.txt" &&
			printf '%s\n' "FreeType is dual-licensed (FreeType-LICENSE.txt): the FreeType License or the GNU General Public" \
				"License, version 2 or later.  This game uses it under the GNU GPL option, version 2 or later" \
				"(FreeType-GPLv2.txt), as NOTICE.md in the game's source says." > "$L/FreeType-OPTION.txt";;
		glslang) cp "$s/glslang/LICENSE.txt" "$L/glslang-LICENSE.txt";;
		spirv-cross) cp "$s/SPIRV-Cross/LICENSE" "$L/SPIRV-Cross-LICENSE.txt";;
		miniaudio) cp "$s/miniaudio/LICENSE" "$L/miniaudio-LICENSE.txt";;
		gamespy) cp "$s/GameSpy/LICENSE" "$L/GameSpy-LICENSE.txt";;
		lzhl)		# LZH-Light's notice is the comment that opens lzhl.h (inside libcompression, unaltered)
			awk 'NR == 1 && !/^\/\*/ { exit 1 } { print } /\*\// { exit }' "$s/Compression/LZHCompress/CompLibHeader/lzhl.h" > "$L/LZH-Light-LICENSE.txt" &&
			grep -q "this notice may not be removed or altered" "$L/LZH-Light-LICENSE.txt";;
		zlib)		# zlib's licence is the comment that opens zlib.h
			awk 'NR == 1 && !/^\/\*/ { exit 1 } { print } /\*\// { exit }' "$s/Compression/ZLib/zlib.h" > "$L/zlib-LICENSE.txt" &&
			grep -q "This notice may not be removed" "$L/zlib-LICENSE.txt";;
		*) echo "no license_entry named $1" >&2; return 1;;
	esac
}
