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
"""Exercise the Linux CI handoff gates using synthetic files and real tar/zstd.

No game data, downloads, installed packages or compiled engine are needed. Native
package fixtures deliberately contain marker text: these tests cover the handoff
inventory, not DEB/RPM/Flatpak construction or distribution installation.
"""

import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


TOOLS = Path(__file__).resolve().parents[1] / "Tools"
SPEC = importlib.util.spec_from_file_location("linux_release", TOOLS / "linux-release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)

REQUIRED_GPU_TESTS = (
    "sdl_gpu_frame_selfcheck", "sdl_program_cache_selfcheck",
    "sdl_pipeline_state_selfcheck", "posix_gpu_draw_selfcheck",
    "ffref_gpu_selfcheck", "ffref_capture_selfcheck",
    "test_water_path_gpu", "test_sun_shadow_sorting",
)


class LinuxReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="test-linux-release-")
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.portable = self.work / "fixture" / release.ROOT
        self.files = {
            "bin/generals": b"#!/bin/sh\nexit 0\n",
            "bin/generals.debug": b"synthetic debug marker\n",
            "zero-hour-reforged.sh": b"#!/bin/sh\nexit 0\n",
            "VERSION": ("Zero Hour Reforged 3.2.0 for Linux (x86_64)\n"
                        "needs glibc GLIBC_2.31 or newer, and fontconfig; art left out\n").encode(),
            "README.txt": b"synthetic engine-only fixture\n",
            "UserData/README.txt": b"empty user folder description\n",
            "share/zero-hour-reforged/licenses/Zero-Hour-Reforged-LICENSE.md": b"synthetic licence marker\n",
            "share/zero-hour-reforged/overlay/Data/INI/CiFixture.ini": b"synthetic overlay fixture\n",
        }
        for name, data in self.files.items():
            path = self.portable / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        for name in ("bin/generals", "zero-hour-reforged.sh"):
            (self.portable / name).chmod(0o755)

    def archive(self, name="portable", extra=None):
        raw = self.work / (name + ".tar")
        with tarfile.open(raw, "w") as archive:
            archive.add(self.portable, arcname=release.ROOT)
            if extra is not None:
                member, payload = extra
                archive.addfile(member, io.BytesIO(payload) if member.isfile() else None)
        compressed = self.work / (name + ".tar.zst")
        with compressed.open("wb") as stream:
            subprocess.run(["zstd", "-q", "-c", "--", str(raw)], stdout=stream, check=True)
        return compressed

    def regular_member(self, name, data=b"synthetic content\n"):
        member = tarfile.TarInfo(name)
        member.size = len(data)
        member.mode = 0o644
        return member, data

    def manifest(self, folder):
        names = sorted(path.name for path in folder.iterdir() if path.name != "SHA256SUMS")
        (folder / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256((folder / name).read_bytes()).hexdigest()}  {name}\n"
            for name in names
        ))

    def package_jobs(self):
        archive = self.archive()
        native, flatpak = self.work / "native", self.work / "flatpak"
        native.mkdir()
        flatpak.mkdir()
        for folder in (native, flatpak):
            shutil.copyfile(archive, folder / release.TARBALL)
        for name in (
            "zero-hour-reforged-3.2.0-x86_64.AppImage",
            "zero-hour-reforged_3.2.0_amd64.deb",
            "zero-hour-reforged-3.2.0-1.x86_64.rpm",
            "zero-hour-reforged-3.2.0-1-x86_64.pkg.tar.zst",
        ):
            (native / name).write_text("synthetic package fixture: " + name)
        (flatpak / release.FLATPAK).write_text("synthetic Flatpak fixture")
        self.manifest(native)
        self.manifest(flatpak)
        return native, flatpak

    def report(self, gpu_state=None, missing=None, ordinary_failure=False):
        suite = ET.Element("testsuite")
        for name in REQUIRED_GPU_TESTS:
            if name == missing:
                continue
            case = ET.SubElement(suite, "testcase", name=name, status="run", time="0.01")
            if gpu_state and name == "test_water_path_gpu":
                if gpu_state in {"skipped", "failure", "error"}:
                    ET.SubElement(case, gpu_state, message="synthetic unavailable/failing renderer")
                else:
                    case.set("status", gpu_state)
        case = ET.SubElement(suite, "testcase", name="replay_check", status="notrun")
        ET.SubElement(case, "skipped", message="no original game data")
        if ordinary_failure:
            case = ET.SubElement(suite, "testcase", name="widechar_check", status="fail")
            ET.SubElement(case, "failure", message="synthetic source-check failure")
        path = self.work / "ctest.xml"
        ET.ElementTree(suite).write(path, encoding="unicode")
        return path

    def summarize(self, report):
        with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": ""}):
            with contextlib.redirect_stdout(io.StringIO()) as output:
                release.ctest_summary(report)
        return output.getvalue()

    def test_valid_archive_extracts_exact_files_and_permissions(self):
        archive = self.archive()
        destination = self.work / "extracted"
        release.extract_portable(archive, destination)
        result = destination / release.ROOT
        for name, data in self.files.items():
            self.assertEqual((result / name).read_bytes(), data)
        self.assertTrue(os.access(result / "bin/generals", os.X_OK))
        self.assertTrue(os.access(result / "zero-hour-reforged.sh", os.X_OK))
        self.assertEqual(list(destination.iterdir()), [result])
        with self.assertRaisesRegex(ValueError, "destination already exists"):
            release.extract_portable(archive, destination)

    def test_digest_streams_files_without_python311_helpers(self):
        # Jammy's Python 3.10 lacks file_digest. Also cross multiple read boundaries
        # and include every byte value so the portable helper cannot omit a chunk.
        path = self.work / "digest-input"
        with mock.patch.object(release.hashlib, "file_digest", create=True,
                               side_effect=AssertionError("unavailable on Python 3.10")):
            for payload in (b"", bytes(range(256)) * 8193):
                path.write_bytes(payload)
                self.assertEqual(release.digest(path), hashlib.sha256(payload).hexdigest())

    def test_forbidden_game_and_player_files_are_refused_without_partial_output(self):
        names = (
            "share/zero-hour-reforged/overlay/INIZH.BIG",
            "share/zero-hour-reforged/overlay/intro.bik",
            "share/zero-hour-reforged/overlay/audio.wbank",
            "share/zero-hour-reforged/overlay/GameData/README.txt",
            "share/zero-hour-reforged/overlay/ZH_Generals/data.txt",
            "UserData/Registry.ini",
            "UserData/Save/player.sav",
        )
        for index, name in enumerate(names):
            with self.subTest(name=name):
                archive = self.archive(str(index), self.regular_member(f"{release.ROOT}/{name}"))
                destination = self.work / f"rejected-{index}"
                with self.assertRaises(ValueError):
                    release.extract_portable(archive, destination)
                self.assertEqual(list(destination.iterdir()), [])

    def test_archive_traversal_links_duplicates_and_special_files_are_refused(self):
        escaped = self.work / "escaped"
        malformed = [
            self.regular_member(f"{release.ROOT}/../../escaped"),
            self.regular_member(str(escaped)),
            self.regular_member("unexpected-root/VERSION"),
            self.regular_member(f"{release.ROOT}/VERSION"),
            self.regular_member(f"{release.ROOT}/./VERSION"),
        ]
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE):
            member = tarfile.TarInfo(f"{release.ROOT}/bin/unsafe-link")
            member.type = kind
            member.linkname = f"{release.ROOT}/bin/generals"
            malformed.append((member, b""))
        for index, extra in enumerate(malformed):
            with self.subTest(name=extra[0].name, kind=extra[0].type):
                archive = self.archive(f"unsafe-{index}", extra)
                destination = self.work / f"unsafe-out-{index}"
                with self.assertRaises(ValueError):
                    release.extract_portable(archive, destination)
                self.assertFalse(escaped.exists())
                self.assertEqual(list(destination.iterdir()), [])

    def test_direct_audit_refuses_links_and_missing_required_files(self):
        link = self.portable / "bin/unexpected"
        link.symlink_to("generals")
        with self.assertRaisesRegex(ValueError, "links/special files forbidden"):
            release.audit_portable(self.portable)
        link.unlink()
        (self.portable / "bin/generals.debug").unlink()
        with self.assertRaisesRegex(ValueError, "missing portable file"):
            release.audit_portable(self.portable)

    def test_truncated_zstd_frame_is_not_accepted_after_tar_entries(self):
        archive = self.archive()
        archive.write_bytes(archive.read_bytes()[:-4])
        destination = self.work / "truncated-out"
        with self.assertRaises((ValueError, tarfile.TarError)):
            release.extract_portable(archive, destination)
        self.assertEqual(list(destination.iterdir()), [])

    def test_assembly_merges_six_formats_and_rechecks_all_checksums(self):
        native, flatpak = self.package_jobs()
        output = self.work / "assembled"
        with contextlib.redirect_stdout(io.StringIO()):
            release.assemble(native, flatpak, output)
        entries = release.verify_checksums(output)
        self.assertEqual(len(entries), 6)
        self.assertEqual(len(list(output.iterdir())), 7)
        self.assertIn(release.FLATPAK, entries)
        for name in entries:
            source = flatpak if name == release.FLATPAK else native
            self.assertEqual((output / name).read_bytes(), (source / name).read_bytes())
        # An existing destination must not be overwritten on a subsequent run.
        (output / "keep.txt").write_text("existing content")
        with self.assertRaisesRegex(ValueError, "output must be empty"):
            release.assemble(native, flatpak, output)
        self.assertEqual((output / "keep.txt").read_text(), "existing content")

    def test_assembly_refuses_different_engine_handoffs(self):
        native, flatpak = self.package_jobs()
        (self.portable / "README.txt").write_text("a different synthetic build")
        shutil.copyfile(self.archive("other"), flatpak / release.TARBALL)
        self.manifest(flatpak)
        with self.assertRaisesRegex(ValueError, "same portable engine archive"):
            release.assemble(native, flatpak, self.work / "assembled")
        self.assertFalse((self.work / "assembled").exists())

    def test_assembly_refuses_missing_and_unexpected_native_formats(self):
        native, flatpak = self.package_jobs()
        package = native / "zero-hour-reforged_3.2.0_amd64.deb"
        saved = package.read_bytes()
        package.unlink()
        self.manifest(native)
        with self.assertRaisesRegex(ValueError, "native package inventory"):
            release.assemble(native, flatpak, self.work / "assembled")
        package.write_bytes(saved)
        (native / "unexpected.zip").write_text("synthetic extra package")
        self.manifest(native)
        with self.assertRaisesRegex(ValueError, "native package inventory"):
            release.assemble(native, flatpak, self.work / "assembled")
        self.assertFalse((self.work / "assembled").exists())

    def test_checksums_reject_corruption_unlisted_files_and_unsafe_manifests(self):
        folder = self.work / "checksums"
        folder.mkdir()
        package = folder / "fixture.deb"
        package.write_bytes(b"synthetic package")
        self.manifest(folder)
        original = (folder / "SHA256SUMS").read_text()
        package.write_bytes(b"modified after hashing")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            release.verify_checksums(folder)
        package.write_bytes(b"synthetic package")
        (folder / "unlisted.log").write_text("unlisted")
        with self.assertRaisesRegex(ValueError, "unlisted files"):
            release.verify_checksums(folder)
        (folder / "unlisted.log").unlink()
        for manifest in (original + original, "0" * 64 + "  ../outside\n", ""):
            with self.subTest(manifest=manifest):
                (folder / "SHA256SUMS").write_text(manifest)
                with self.assertRaises(ValueError):
                    release.verify_checksums(folder)
        (folder / "SHA256SUMS").write_text(original)
        target = self.work / "outside.deb"
        package.rename(target)
        package.symlink_to(target)
        with self.assertRaisesRegex(ValueError, "missing/nonregular package"):
            release.verify_checksums(folder)

    def test_tag_requires_the_matching_numeric_engine_version(self):
        version = self.portable / "VERSION"
        for tag in ("v3.2.0", "v3.2.0-linux.1", "v3.2.0-rc.1", "v3.2.0-linux.0"):
            with self.subTest(tag=tag):
                release.verify_tag(version, tag)
        for tag in (
            "main", "3.2.0", "v3.2", "v3.2.0-", "v3.2.0/evil", "v3.2.1", "v4.0.0-linux.1",
            "v03.2.0", "v3.02.0", "v3.2.00", "v3.2.0-linux..1", "v3.2.0-linux.", "v3.2.0-linux.01",
        ):
            with self.subTest(tag=tag):
                with self.assertRaises(ValueError):
                    release.verify_tag(version, tag)
        version.write_text(version.read_text().replace("x86_64", "aarch64"))
        with self.assertRaisesRegex(ValueError, "x86_64"):
            release.verify_tag(version, "v3.2.0")

    def test_junit_reports_asset_skips_without_hiding_gpu_requirements(self):
        report = self.report()
        summary = self.summarize(report)
        self.assertIn("0 failed; 1 skipped/disabled", summary)
        self.assertIn("`replay_check`", summary)
        self.assertIn("Coverage limits", summary)
        self.assertIn("synthetic-only coverage", summary)
        step_summary = self.work / "summary.md"
        step_summary.write_text("Previous step\n")
        with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(step_summary)}):
            with contextlib.redirect_stdout(io.StringIO()):
                release.ctest_summary(report)
        self.assertTrue(step_summary.read_text().startswith("Previous step\n"))
        self.assertIn("Linux test coverage", step_summary.read_text())

    def test_junit_refuses_skipped_failed_and_missing_gpu_tests(self):
        for state in ("skipped", "notrun", "disabled", "failure", "error", "fail", "unknown"):
            with self.subTest(state=state):
                with self.assertRaises(ValueError):
                    self.summarize(self.report(gpu_state=state))
        for missing in REQUIRED_GPU_TESTS:
            with self.subTest(missing=missing):
                with self.assertRaisesRegex(ValueError, "required Lavapipe checks"):
                    self.summarize(self.report(missing=missing))
        with self.assertRaisesRegex(ValueError, "CTest failures remain"):
            self.summarize(self.report(ordinary_failure=True))
        empty = self.work / "empty.xml"
        empty.write_text("<testsuite />")
        with self.assertRaisesRegex(ValueError, "no tests"):
            self.summarize(empty)

    def test_junit_refuses_duplicate_names_and_unconfirmed_gpu_execution(self):
        report = self.report()
        tree = ET.parse(report)
        ET.SubElement(tree.getroot(), "testcase", name="test_water_path_gpu", status="run")
        tree.write(report, encoding="unicode")
        with self.assertRaisesRegex(ValueError, "duplicate test names"):
            self.summarize(report)
        report = self.report()
        tree = ET.parse(report)
        for case in tree.getroot().iter("testcase"):
            if case.attrib["name"] == "test_water_path_gpu":
                del case.attrib["status"]
        tree.write(report, encoding="unicode")
        with self.assertRaisesRegex(ValueError, "required Lavapipe checks"):
            self.summarize(report)


if __name__ == "__main__":
    if shutil.which("zstd") is None:
        raise SystemExit("test_linux_release requires zstd; no tests were run")
    unittest.main(verbosity=2)
