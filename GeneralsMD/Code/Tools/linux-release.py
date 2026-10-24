#!/usr/bin/env python3
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
"""Validate the engine-only handoff and reports used by Linux release CI.

Packaging stays in linux-packages.sh. This module checks the files crossing job
boundaries; it never downloads game data or treats skipped tests as passing.
"""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import xml.etree.ElementTree as ET


ROOT = "ZeroHourReforged-linux-x86_64"
TARBALL = ROOT + ".tar.zst"
FLATPAK = "io.github.olcayseygan.ZeroHourReforged.flatpak"
TOP_LEVEL = {"bin", "share", "UserData", "zero-hour-reforged.sh", "README.txt", "VERSION"}
GPU_TESTS = {
    "sdl_gpu_frame_selfcheck", "sdl_program_cache_selfcheck",
    "sdl_pipeline_state_selfcheck", "posix_gpu_draw_selfcheck",
    "ffref_gpu_selfcheck", "ffref_capture_selfcheck",
    "test_water_path_gpu", "test_sun_shadow_sorting",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def safe_member(name):
    path = PurePosixPath(name)
    require(not path.is_absolute() and ".." not in path.parts, f"unsafe path: {name}")
    require(path.parts and path.parts[0] == ROOT, f"unexpected archive root: {name}")
    relative = path.parts[1:]
    if relative:
        require(relative[0] in TOP_LEVEL, f"unexpected portable content: {name}")
    lower = tuple(part.lower() for part in relative)
    require(not {"gamedata", "zh_generals"}.intersection(lower), f"game data forbidden: {name}")
    require(not path.name.lower().endswith((".big", ".bik", ".wbank")),
            f"game archive, movie or captured scene forbidden: {name}")
    if relative and relative[0] == "UserData":
        require(relative in (("UserData",), ("UserData", "README.txt")),
                f"player data forbidden: {name}")
    return path


def audit_portable(folder):
    folder = Path(folder)
    require(folder.is_dir() and not folder.is_symlink(), f"not a portable directory: {folder}")
    for item in folder.rglob("*"):
        safe_member(str(PurePosixPath(ROOT, *item.relative_to(folder).parts)))
        mode = item.lstat().st_mode
        require(stat.S_ISDIR(mode) or stat.S_ISREG(mode), f"links/special files forbidden: {item}")
    for name in ("bin/generals", "bin/generals.debug", "zero-hour-reforged.sh", "VERSION", "README.txt",
                 "share/zero-hour-reforged/licenses/Zero-Hour-Reforged-LICENSE.md"):
        require((folder / name).is_file() and (folder / name).stat().st_size > 0,
                f"missing portable file: {name}")
    for name in ("bin/generals", "zero-hour-reforged.sh"):
        require(os.access(folder / name, os.X_OK), f"not executable: {name}")
    overlay = folder / "share/zero-hour-reforged/overlay"
    require(overlay.is_dir() and any(overlay.iterdir()), "missing/empty overlay")
    version_from_text((folder / "VERSION").read_text())


def extract_portable(archive, destination):
    """Stream zstd without temporary decompressed copies; reject links and traversal."""
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    require(not (destination / ROOT).exists(), f"destination already exists: {destination / ROOT}")
    with tempfile.TemporaryDirectory(prefix=".portable-", dir=destination) as temporary:
        seen = set()
        total_size = 0
        with subprocess.Popen(["zstd", "-q", "-d", "-c", "--", str(archive)], stdout=subprocess.PIPE) as process:
            try:
                with tarfile.open(fileobj=process.stdout, mode="r|") as source:
                    for member in source:
                        path = safe_member(member.name)
                        require(str(path) not in seen, f"duplicate archive member: {path}")
                        seen.add(str(path))
                        require(member.isdir() or member.isfile(), f"links/special files forbidden: {path}")
                        total_size += member.size
                        require(total_size <= 2 * 1024**3, "portable archive exceeds the 2 GiB engine-only budget")
                        target = Path(temporary).joinpath(*path.parts)
                        if member.isdir():
                            target.mkdir(parents=True, exist_ok=True)
                        else:
                            target.parent.mkdir(parents=True, exist_ok=True)
                            with source.extractfile(member) as incoming, target.open("xb") as outgoing:
                                shutil.copyfileobj(incoming, outgoing)
                            target.chmod(member.mode & 0o777)
                # Drain the pipe so zstd can validate its frame checksum and finish.
                while process.stdout.read(1024 * 1024):
                    pass
                require(process.wait() == 0, "zstd rejected the portable archive")
            except BaseException:
                process.kill()
                process.wait()
                raise
        audit_portable(Path(temporary) / ROOT)
        (Path(temporary) / ROOT).rename(destination / ROOT)


def version_from_text(text):
    match = re.search(r"^Zero Hour Reforged (\d+\.\d+\.\d+) for Linux \(x86_64\)$", text, re.M)
    require(match, "VERSION must identify an x86_64 Linux engine and x.y.z version")
    require("art left out" in text, "VERSION must declare that art was left out")
    return match.group(1)


def verify_tag(version_file, tag):
    number = r"(?:0|[1-9][0-9]*)"
    match = re.fullmatch(rf"v({number}\.{number}\.{number})(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?", tag)
    require(match, "release tag must be vX.Y.Z or vX.Y.Z-suffix (for example v3.2.0-linux.1)")
    for component in (match.group(2) or "").split("."):
        require(not (component.isdigit() and len(component) > 1 and component.startswith("0")),
                "numeric release tag suffix components cannot have leading zeroes")
    require(match.group(1) == version_from_text(Path(version_file).read_text()),
            "release tag's numeric version does not match the engine VERSION")


def digest(path):
    # hashlib.file_digest requires Python 3.11; the Ubuntu 22.04 baseline has 3.10.
    checksum = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def verify_checksums(folder):
    folder = Path(folder)
    entries = {}
    for line in (folder / "SHA256SUMS").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_.+-]+)", line)
        require(match, f"invalid checksum entry in {folder}: {line}")
        expected, name = match.groups()
        require(name not in entries and name != "SHA256SUMS", f"duplicate/invalid checksum name: {name}")
        path = folder / name
        require(path.is_file() and not path.is_symlink(), f"missing/nonregular package: {path}")
        require(digest(path) == expected, f"checksum mismatch: {path}")
        entries[name] = expected
    actual = {entry.name for entry in folder.iterdir()}
    require(actual == set(entries) | {"SHA256SUMS"}, f"unlisted files in package directory: {folder}")
    require(entries, f"empty checksum list: {folder}")
    return entries


def assemble(native, flatpak, output):
    native, flatpak, output = Path(native), Path(flatpak), Path(output)
    native_entries = verify_checksums(native)
    flatpak_entries = verify_checksums(flatpak)
    require(set(flatpak_entries) == {TARBALL, FLATPAK}, "unexpected Flatpak job output")
    require(native_entries.get(TARBALL) == flatpak_entries[TARBALL],
            "packaging jobs did not use the same portable engine archive")
    with tempfile.TemporaryDirectory(prefix="zhr-release-") as temporary:
        extract_portable(native / TARBALL, temporary)
        version = version_from_text((Path(temporary) / ROOT / "VERSION").read_text())
    expected_native = {
        TARBALL, f"zero-hour-reforged-{version}-x86_64.AppImage",
        f"zero-hour-reforged_{version}_amd64.deb", f"zero-hour-reforged-{version}-1.x86_64.rpm",
        f"zero-hour-reforged-{version}-1-x86_64.pkg.tar.zst",
    }
    require(set(native_entries) == expected_native, "native package inventory is incomplete or unexpected")
    output.mkdir(parents=True, exist_ok=True)
    require(not any(output.iterdir()), f"output must be empty: {output}")
    for name in sorted(expected_native):
        shutil.copyfile(native / name, output / name)
    shutil.copyfile(flatpak / FLATPAK, output / FLATPAK)
    names = sorted(expected_native | {FLATPAK})
    (output / "SHA256SUMS").write_text("".join(f"{digest(output / name)}  {name}\n" for name in names))
    verify_checksums(output)
    print(f"Verified six engine-only Linux package formats for {version} and SHA256SUMS")


def ctest_summary(report):
    cases = list(ET.parse(report).getroot().iter("testcase"))
    require(cases, "CTest report contains no tests")
    names = {case.attrib["name"]: case for case in cases}
    require(len(names) == len(cases), "CTest report contains duplicate test names")
    skipped = [case.attrib["name"] for case in cases
               if case.find("skipped") is not None or case.attrib.get("status") in {"notrun", "disabled"}]
    failed = [case.attrib["name"] for case in cases
              if case.find("failure") is not None or case.find("error") is not None
              or case.attrib.get("status") == "fail"]
    missing_gpu = sorted(name for name in GPU_TESTS
                         if name not in names or name in skipped or name in failed
                         or names[name].attrib.get("status") != "run")
    lines = ["## Linux test coverage", "", f"CTest: {len(cases)} recorded; {len(failed)} failed; {len(skipped)} skipped/disabled.", "",
             "Build: Steam Runtime Sniper, x86_64, glibc 2.31 ABI ceiling, static C++ runtime.",
             "Rendering: Mesa Lavapipe on a CPU runner; no hardware GPU coverage.", "",
             "### Skipped or disabled tests", ""]
    lines += [f"- `{name}`" for name in sorted(skipped)] or ["None reported by CTest."]
    lines += ["", "### Coverage limits", "",
              "- Original Generals/Zero Hour data and Reforged art are never downloaded or supplied.",
              "- Full replay CRCs, gameplay screenshots/render parity, game-data audio/video/terrain and multiplayer checks require a separate private run with legally owned data.",
              "- Darwin/Rosetta, Microsoft DLL oracles, Metal, real GPU drivers, Steam Deck/Game Mode and interactive portals are not validated here.",
              "- Some passing tests have synthetic-only coverage and log optional data/font/platform omissions.",
              "- `linux_portable_check` is excluded because it starts a second engine build; the actual portable build already enforces ABI, dependencies and licenses.", ""]
    if missing_gpu:
        lines += ["Required software-rendering checks did not pass: " + ", ".join(missing_gpu), ""]
    summary = "\n".join(lines)
    print(summary)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as stream:
            stream.write(summary + "\n")
    require(not failed, "CTest failures remain")
    require(not missing_gpu, "required Lavapipe checks are missing, skipped or failed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command, arguments in (
        ("audit-portable", ("folder",)), ("extract-portable", ("archive", "destination")),
        ("verify-tag", ("version_file", "tag")), ("verify-checksums", ("folder",)),
        ("assemble", ("native", "flatpak", "output")), ("ctest-summary", ("report",)),
    ):
        command_parser = sub.add_parser(command)
        for argument in arguments:
            command_parser.add_argument(argument)
    options = vars(parser.parse_args())
    command = options.pop("command").replace("-", "_")
    try:
        globals()[command](**options)
    except (ValueError, OSError, tarfile.TarError, ET.ParseError) as error:
        parser.exit(1, f"linux-release: {error}\n")


if __name__ == "__main__":
    main()
