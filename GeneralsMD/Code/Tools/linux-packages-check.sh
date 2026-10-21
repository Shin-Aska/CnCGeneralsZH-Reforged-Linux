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
# linux-packages-check.sh: proves the release files linux-packages.sh made, each on a clean container of its
# distribution.  Per distribution, two containers:
#
#   minimal   the package installed with only what it Depends/Requires (no recommended or weak dependencies),
#             then cut off the network: E1 (Tools/replay-check.sh --installed) through /usr/bin/zero-hour-reforged,
#             its launcher and its own overlay discovery, at the pins: seed 0 at 2400 frames, seeds 0 and 1 at
#             24000.  The game data is mounted read-only (rule 9 as ever: the runs root at a farm of it);
#   drawn     the package installed as a player gets it (recommended dependencies on; on Arch, the Vulkan and
#             X11 optdepends), Xvfb, and Mesa's software Vulkan (lavapipe): one windowed skirmish frame, written
#             with -screenshot, which must be a picture (many colours), not a blank.
#
#   deb       debian:12 and ubuntu:24.04            rpm   fedora:latest and opensuse/tumbleweed
#   arch      archlinux:latest                      appimage   debian:12, run with APPIMAGE_EXTRACT_AND_RUN
#
# No run needs a person: the Zero Hour folder is given (Registry.ini for E1, -root for the frame), every run is
# unattended (ZH_UNATTENDED=1, which also keeps the launcher from fetching the art), and each has a time limit.
# The containers are removed as each distribution ends, and the images it pulled with --remove-images.
#
# Without proprietary data, --no-data checks the minimal package installation, its layout and dynamic
# dependencies, then runs Tests/run_generals_smoke.sh through its launcher. This checks the expected
# missing-data errors, not gameplay, replay CRCs or a rendered game frame. No data directory is mounted.
#
# Usage: linux-packages-check.sh --packages <folder> (--data <folder> | --no-data)
#          [--formats "deb rpm arch appimage"] [--distros "debian-12 ubuntu-24.04 fedora opensuse-tumbleweed archlinux appimage"]
#          [--no-draw] [--remove-images]
#   --packages   linux-packages.sh's --out
#   --data       a folder holding zerohour/ (the base game in zerohour/ZH_Generals), as replay-check.sh takes it
#   --distros    only these rows (default: every row for --formats); useful for CI jobs on fresh runners
# Exit status: the number of distributions that failed; 2 for invalid arguments; 77 without docker or required data.

set -u
PKGS="" DATA="" FORMATS="deb rpm arch appimage" DISTROS="" DRAW=1 REMOVE_IMAGES=0 NO_DATA=0
while [ $# -gt 0 ]; do
	case "$1" in
		--packages) PKGS="$2"; shift 2;;
		--data) DATA="$2"; shift 2;;
		--no-data) NO_DATA=1; shift;;
		--formats) FORMATS="$2"; shift 2;;
		--distros) DISTROS="$2"; shift 2;;
		--no-draw) DRAW=0; shift;;
		--remove-images) REMOVE_IMAGES=1; shift;;
		*) echo "linux-packages-check: unknown argument $1" >&2; exit 2;;
	esac
done
[ "$NO_DATA" -eq 0 ] || [ -z "$DATA" ] || { echo "linux-packages-check: --no-data and --data are mutually exclusive" >&2; exit 2; }
for distro in $DISTROS; do
	case "$distro" in debian-12|ubuntu-24.04|fedora|opensuse-tumbleweed|archlinux|appimage) ;;
		*) echo "linux-packages-check: unknown distribution $distro" >&2; exit 2;;
	esac
done
command -v docker > /dev/null || { echo "skip: no docker"; exit 77; }
if [ "$NO_DATA" -eq 0 ]; then
	[ -n "$DATA" ] && [ -d "$DATA/zerohour" ] || { echo "skip: no game data (--data); use --no-data for installation and missing-data smoke checks only"; exit 77; }
	DATA="$(cd "$DATA" && pwd -P)"
fi
[ -n "$PKGS" ] && [ -d "$PKGS" ] || { echo "linux-packages-check: --packages must name linux-packages.sh's --out" >&2; exit 2; }
CODE="$(cd "$(dirname "$0")/.." && pwd)"
PKGS="$(cd "$PKGS" && pwd -P)"
file_of() { ls "$PKGS"/$1 2>/dev/null | head -1; }
DEB="$(file_of 'zero-hour-reforged_*_amd64.deb')"; RPM="$(file_of 'zero-hour-reforged-*.x86_64.rpm')"
ARCHPKG="$(file_of 'zero-hour-reforged-*-x86_64.pkg.tar.zst')"; APPIMAGE="$(file_of 'zero-hour-reforged-*-x86_64.AppImage')"
PIN_2400=0x67F86891 PIN_0=0x6404352D PIN_1=0x90B6D230	# E1's pins since the logic went to 60 Hz (measured on Windows x64)
failed=0 checked=0 pulled=""
say() { echo "linux-packages-check: $*"; }
crc() { printf '%s\n' "$1" | sed -n "s/.*seed $2, 2 players: HEADLESS CRC \(0x[0-9A-Fa-f]*\) at frame $3.*/\1/p" | head -1; }	# <E1 output> <seed> <frame>

# the frame run's farm, the way replay-check.sh makes its own (links into the read-only data)
# a work folder that could not be made is the end of the run: going on with WORKROOT empty would write under /
WORKROOT="$(mktemp -d "${TMPDIR:-/tmp}/zhr-pkgcheck.XXXXXX")" || WORKROOT=""
if [ -z "$WORKROOT" ] || [ ! -d "$WORKROOT" ]; then echo "linux-packages-check: cannot make a work folder under ${TMPDIR:-/tmp}" >&2; exit 2; fi
trap 'docker rm -f $(docker ps -aq --filter "label=zhr-pkgcheck=$$") > /dev/null 2>&1; rm -rf -- "$WORKROOT"' EXIT
FARM="$WORKROOT/farm"
if [ "$NO_DATA" -eq 0 ]; then
	( cd "$DATA/zerohour" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$FARM/$d"; done
	( cd "$DATA/zerohour" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$DATA/zerohour/${f#./}" "$FARM/$f"; done
else
	say "--no-data: installation, dependency and missing-data smoke checks only"
	say "SKIP: replay CRCs and gameplay rendering require original proprietary game data"
fi

start_container() {	# start_container <name> <image>: a clean container, the packages, data and work folder mounted
	local data_mount=()
	[ "$NO_DATA" -eq 1 ] || data_mount=( -v "$DATA:$DATA:ro" )
	docker image inspect "$2" > /dev/null 2>&1 || pulled="$pulled $2"
	docker run -d --name "$1" --label "zhr-pkgcheck=$$" -v "$PKGS:$PKGS:ro" "${data_mount[@]}" -v "$WORKROOT:$WORKROOT" \
		-v "$CODE/Tests/run_generals_smoke.sh:/zhr-check/run_generals_smoke.sh:ro" \
		"$2" sleep 7200 > /dev/null
}
as_root() { docker exec "$1" sh -c "$2"; }

# install_cmd <distribution> <minimal|drawn>: the package manager's own command, as a player would type it
install_cmd() {
	local weak
	case "$1:$2" in
		debian*:minimal|ubuntu*:minimal) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install --no-install-recommends '$DEB'";;
		debian*:drawn|ubuntu*:drawn) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install '$DEB' xvfb";;
		fedora*:minimal) echo "dnf -q -y --setopt=install_weak_deps=False install '$RPM'";;
		fedora*:drawn) echo "dnf -q -y install '$RPM' xorg-x11-server-Xvfb";;
		opensuse*:minimal) echo "zypper -q -n install --allow-unsigned-rpm --no-recommends '$RPM'";;
		opensuse*:drawn) echo "zypper -q -n install --allow-unsigned-rpm '$RPM' xvfb-run xorg-x11-server-Xvfb";;
		arch*:minimal) echo "pacman -Sy --noconfirm && pacman -U --noconfirm '$ARCHPKG'";;
		arch*:drawn) echo "pacman -Sy --noconfirm && pacman -U --noconfirm '$ARCHPKG' && pacman -S --noconfirm vulkan-icd-loader vulkan-swrast libx11 libxkbcommon libxcursor libxrandr libxi alsa-lib dbus xorg-server-xvfb";;
		appimage:minimal) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install --no-install-recommends libfontconfig1";;
		appimage:drawn) echo "apt-get -q update && DEBIAN_FRONTEND=noninteractive apt-get -q -y install libfontconfig1 libvulkan1 mesa-vulkan-drivers libx11-6 libxext6 libxcursor1 libxi6 libxrandr2 libxkbcommon0 xvfb";;
	esac
}

check_without_data() {	# check_without_data <container> <format> <work>: the engine's existing C2 contract, installed
	local name="$1" format="$2" work="$3"
	mkdir -p "$work/home"
	docker exec -i -u "$(id -u):$(id -g)" -e HOME="$work/home" -e TMPDIR="$work/tmp" -e ZH_UNATTENDED=1 \
		-e APPIMAGE_EXTRACT_AND_RUN=1 -w "$work" "$name" bash -s -- "$format" "$APPIMAGE" "$work" <<'SMOKE'
set -eu
format="$1" appimage="$2" work="$3"
folder=/usr/lib/zero-hour-reforged
launcher=/usr/bin/zero-hour-reforged
if [ "$format" = appimage ]; then
	# Artifact downloads need not preserve executable bits. Copy into the writable work directory;
	# the package input stays read-only. Extract once for layout/ldd, then exercise the actual runtime.
	cp "$appimage" "$work/engine.AppImage"
	chmod +x "$work/engine.AppImage"
	launcher="$work/engine.AppImage"
	env -u APPIMAGE_EXTRACT_AND_RUN timeout 90 "$launcher" --appimage-extract > "$work/appimage-extract.log"
	folder="$work/squashfs-root"
fi
test -x "$launcher"
test -x "$folder/bin/generals"
test -s "$folder/VERSION"
test -d "$folder/share/zero-hour-reforged/overlay"
test -s "$folder/share/zero-hour-reforged/licenses/Zero-Hour-Reforged-LICENSE.md"
ldd "$folder/bin/generals" > "$work/ldd.log" 2>&1
cat "$work/ldd.log"
if grep -q 'not found' "$work/ldd.log"; then
	echo "unresolved dynamic dependencies" >&2
	exit 1
fi
timeout 90 bash /zhr-check/run_generals_smoke.sh "$launcher"
SMOKE
}

check_distribution() {	# check_distribution <label> <image> <format>
	local label="$1" image="$2" format="$3" name="zhr-pkgcheck-$$-$1" T="$WORKROOT/$1" bad=0
	mkdir -p "$T/tmp"
	say "== $label ($image, $format)"

	# Minimal installation: either the missing-data smoke test, or E1 at the pins.
	start_container "$name" "$image" || { say "$label: cannot start $image"; return 1; }
	if ! as_root "$name" "$(install_cmd "$label" minimal)" > "$T/install-minimal.log" 2>&1; then
		say "$label: the minimal install failed:"; tail -5 "$T/install-minimal.log"; docker rm -f "$name" > /dev/null; return 1
	fi
	if ! docker network disconnect bridge "$name" > /dev/null 2>&1; then
		say "$label: could not disconnect the test container from the network"; docker rm -f "$name" > /dev/null; return 1
	fi
	if [ "$NO_DATA" -eq 1 ]; then
		if check_without_data "$name" "$format" "$T" > "$T/smoke.log" 2>&1; then
			cat "$T/smoke.log"
			say "$label: minimal installation, layout, dependencies and missing-data launcher smoke passed (offline)"
		else
			cat "$T/smoke.log"; bad=1
			say "$label: asset-independent package check failed"
		fi
		docker rm -f "$name" > /dev/null
		[ $bad -eq 0 ]; return
	fi
	cat > "$T/runner" <<RUNNER
#!/bin/sh
exec docker exec -i -u $(id -u):$(id -g) -e HOME -e ZH_USER_DATA_DIR -e ZH_UNATTENDED -e APPIMAGE_EXTRACT_AND_RUN=1 -e TMPDIR=$T/tmp -w "\$PWD" $name timeout 900 "\$@"
RUNNER
	chmod +x "$T/runner"
	local target=( --installed /usr/bin/zero-hour-reforged ) e1a e1b
	[ "$format" = appimage ] && target=( --appimage "$APPIMAGE" )
	e1a="$(TMPDIR="$T/tmp" REPLAY_CHECK_RUNNER="$T/runner" bash "$CODE/Tools/replay-check.sh" "${target[@]}" --data "$DATA" --seeds 0 --maxframes 2400 2>&1)"
	e1b="$(TMPDIR="$T/tmp" REPLAY_CHECK_RUNNER="$T/runner" bash "$CODE/Tools/replay-check.sh" "${target[@]}" --data "$DATA" --seeds "0 1" --maxframes 24000 2>&1)"
	printf '%s\n%s\n' "$e1a" "$e1b" | grep -E "HEADLESS CRC|agree|DIVERGED|no result|INSTALL" | sed 's/^/  /'
	[ "$(crc "$e1a" 0 2400)" = $PIN_2400 ] && printf '%s' "$e1a" | grep -q "the playback and the second run agree" \
		|| { say "$label: E1 seed 0 at 2400 is not $PIN_2400 (or did not agree)"; bad=1; }
	[ "$(crc "$e1b" 0 24000)" = $PIN_0 ] && [ "$(crc "$e1b" 1 24000)" = $PIN_1 ] \
		&& [ "$(printf '%s' "$e1b" | grep -c "the playback and the second run agree")" -eq 2 ] \
		|| { say "$label: E1 seeds 0 and 1 at 24000 are not $PIN_0 and $PIN_1 (or did not agree)"; bad=1; }
	[ $bad -eq 0 ] && say "$label: E1 at the three pins, through the installed launcher (minimal install, offline)"
	docker rm -f "$name" > /dev/null

	# drawn: one software-rendered frame
	if [ $DRAW -eq 1 ]; then
		start_container "$name" "$image" || { say "$label: cannot start $image"; return 1; }
		if ! as_root "$name" "$(install_cmd "$label" drawn)" > "$T/install-drawn.log" 2>&1; then
			say "$label: the default install failed:"; tail -5 "$T/install-drawn.log"; docker rm -f "$name" > /dev/null; return 1
		fi
		local run=/usr/bin/zero-hour-reforged shot
		[ "$format" = appimage ] && run="$APPIMAGE"
		mkdir -p "$T/draw-user" "$T/draw-home"
		docker exec -u "$(id -u):$(id -g)" -e HOME="$T/draw-home" -e ZH_USER_DATA_DIR="$T/draw-user/" -e ZH_UNATTENDED=1 \
			-e APPIMAGE_EXTRACT_AND_RUN=1 -e TMPDIR="$T/tmp" -e LIBGL_ALWAYS_SOFTWARE=1 -w "$T" "$name" sh -c \
			"Xvfb :77 -screen 0 1024x768x24 -nolisten tcp > '$T/xvfb.log' 2>&1 & sleep 2; DISPLAY=:77 timeout 900 '$run' -root '$FARM' \
				-quickstart -noshellmap -multiInstance -noaudio -xres 800 -yres 600 -randommap 0 2 -autoskirmish 2 -aidiff brutal \
				-seed 0 -observer -maxframes 180 -screenshot 160 > '$T/draw.out' 2> '$T/draw.err'; echo \$? > '$T/draw.status'"
		shot="$(find "$T/draw-user" -name 'sshot*.bmp' | head -1)"
		local colours=0
		[ -n "$shot" ] && colours="$(od -An -v -tu1 -j 54 "$shot" | tr -s ' ' '\n' | sort -u | wc -l)"
		if [ -n "$shot" ] && [ "$colours" -gt 64 ]; then
			say "$label: a software-rendered frame ($(wc -c < "$shot") bytes, $colours byte values; exit $(cat "$T/draw.status"))"
		else
			say "$label: no picture from the software-rendered run (screenshot '${shot:-none}', exit $(cat "$T/draw.status" 2>/dev/null))"
			tail -5 "$T/draw.err"; bad=1
		fi
		docker rm -f "$name" > /dev/null
	fi
	[ $bad -eq 0 ]
}

selected() {
	[ -z "$DISTROS" ] && return 0
	case " $DISTROS " in *" $1 "*) return 0;; esac
	return 1
}
check_selected() {	# <label> <image> <format> <package path>: do not require unselected formats to exist
	selected "$1" || return 0
	checked=$((checked + 1))
	if [ -z "$4" ]; then
		say "$1: no $3 package in $PKGS"; failed=$((failed + 1)); return
	fi
	check_distribution "$1" "$2" "$3" || failed=$((failed + 1))
}

for format in $FORMATS; do
	case "$format" in
		deb) check_selected debian-12 debian:12 deb "$DEB"
			check_selected ubuntu-24.04 ubuntu:24.04 deb "$DEB";;
		rpm) check_selected fedora fedora:latest rpm "$RPM"
			check_selected opensuse-tumbleweed opensuse/tumbleweed rpm "$RPM";;
		arch) check_selected archlinux archlinux:latest arch "$ARCHPKG";;
		appimage) check_selected appimage debian:12 appimage "$APPIMAGE";;
		*) say "unknown format $format"; failed=$((failed + 1));;
	esac
done
[ "$checked" -gt 0 ] || { say "no distributions selected by --formats and --distros"; exit 2; }
if [ $REMOVE_IMAGES -eq 1 ] && [ -n "$pulled" ]; then
	docker rmi $pulled > /dev/null 2>&1 && say "removed the images it pulled:$pulled"
fi
say "$failed distribution(s) failed"
exit $failed
