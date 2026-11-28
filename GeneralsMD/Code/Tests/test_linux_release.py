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
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import textwrap
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
    "test_water_path_gpu", "test_sun_shadow_sorting", "sdl_water_compute_fft",
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

    def metadata_sources(self):
        source = self.work / "repository"
        notes = source / "docs/releases/v3.2.0.md"
        image = source / ".github/media/releases/v3.2.0-launch.png"
        notes.parent.mkdir(parents=True, exist_ok=True)
        image.parent.mkdir(parents=True, exist_ok=True)
        notes.write_text("# Release 3.2.0\n\n![Launch](https://example.test/v3.2.0-launch.png)\n")
        image.write_bytes(b"\x89PNG\r\n\x1a\nsynthetic image fixture")
        return source, notes, image

    def stage_metadata(self, source, output=None, repository="owner/repository", tag="v3.2.0"):
        output = output or self.work / "release-metadata"
        with contextlib.redirect_stdout(io.StringIO()):
            release.release_metadata(source, repository, tag, output)
        return output

    def publish_fixture(self, custom=True, with_artwork=True):
        native, flatpak = self.package_jobs()
        packages = self.work / "release"
        with contextlib.redirect_stdout(io.StringIO()):
            release.assemble(native, flatpak, packages)
        source, notes, image = self.metadata_sources()
        if not custom:
            notes.unlink()
            image.unlink()
        elif not with_artwork:
            notes.write_text("# Release 3.2.0\n\nText-only release notes.\n")
            image.unlink()
        metadata = self.stage_metadata(source)
        fake_bin = self.work / "bin"
        fake_bin.mkdir()
        gh = fake_bin / "gh"
        gh.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import json
            import os
            from pathlib import Path
            import shutil
            import sys
            args = sys.argv[1:]
            root = Path(os.environ['RUNNER_TEMP'])
            with (root / 'gh-calls.jsonl').open('a') as log:
                log.write(json.dumps(args) + '\\n')
            if args[:2] == ['release', 'view']:
                state = os.environ.get('FAKE_RELEASE_STATE', 'missing')
                if state == 'missing':
                    sys.exit(1)
                print('true' if state == 'draft' else 'false')
            elif args[:2] == ['release', 'download']:
                name = args[args.index('--pattern') + 1]
                shutil.copyfile(root / 'remote' / name, Path(args[args.index('--dir') + 1]) / name)
            elif '--notes-file' in args:
                notes = Path(args[args.index('--notes-file') + 1]).read_bytes()
                (root / 'sent-notes.md').write_bytes(notes)
            """))
        gh.chmod(0o755)
        return packages, metadata

    def publish_script(self):
        workflow = (TOOLS.parents[2] / ".github/workflows/linux-release.yml").read_text()
        release_job = workflow.split("\n  release:\n", 1)[1]
        self.assertNotIn("actions/checkout@", release_job)
        self.assertNotIn("GeneralsMD/Code/Tools/", release_job)
        self.assertIn("needs: assemble", release_job)
        return textwrap.dedent(release_job.split("        run: |\n", 1)[1])

    def publish(self, state="missing", tag="v3.2.0", repository="owner/repository"):
        env = dict(os.environ, RUNNER_TEMP=str(self.work), RELEASE_TAG=tag,
                   GH_REPO=repository, FAKE_RELEASE_STATE=state,
                   PATH=str(self.work / "bin") + os.pathsep + os.environ["PATH"])
        return subprocess.run(["bash", "-e", "-o", "pipefail", "-c", self.publish_script()],
                              env=env, capture_output=True, text=True)

    def gh_calls(self):
        log = self.work / "gh-calls.jsonl"
        return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []

    def test_release_metadata_preserves_exact_notes_and_image_separately_from_packages(self):
        packages, metadata = self.publish_fixture()
        source = self.work / "repository"
        self.assertEqual((metadata / "release-notes.md").read_bytes(), (source / "docs/releases/v3.2.0.md").read_bytes())
        self.assertEqual((metadata / "v3.2.0-launch.png").read_bytes(), (source / ".github/media/releases/v3.2.0-launch.png").read_bytes())
        self.assertEqual(len(release.verify_checksums(metadata)), 3)
        self.assertEqual(len(release.verify_checksums(packages)), 6)
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        upload = next(call for call in self.gh_calls() if call[:2] == ["release", "upload"])
        self.assertIn("v3.2.0-launch.png", upload)
        self.assertEqual((self.work / "sent-notes.md").read_bytes(), (metadata / "release-notes.md").read_bytes())
        self.assertEqual(len((packages / "SHA256SUMS").read_text().splitlines()), 6)

    def test_release_metadata_absent_pair_uses_generic_notes_for_resumed_draft(self):
        packages, metadata = self.publish_fixture(custom=False)
        self.assertEqual(set(release.verify_checksums(metadata)), {"release-context.txt", "release-notes.md"})
        self.assertEqual((metadata / "release-notes.md").read_text(), release.GENERIC_RELEASE_NOTES)
        result = self.publish(state="draft")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        calls = self.gh_calls()
        self.assertFalse(any(call[:2] == ["release", "create"] for call in calls))
        edit = next(call for call in calls if call[:2] == ["release", "edit"])
        self.assertIn("--notes-file", edit)
        self.assertIn("--draft=false", edit)
        self.assertEqual(len(list(packages.iterdir())), 7)

    def test_release_publishes_custom_notes_with_only_package_assets(self):
        packages, metadata = self.publish_fixture(with_artwork=False)
        source_notes = self.work / "repository/docs/releases/v3.2.0.md"
        self.assertEqual((metadata / "release-notes.md").read_bytes(), source_notes.read_bytes())
        self.assertEqual((metadata / "release-context.txt").read_text(), "owner/repository\nv3.2.0\n-\n")
        self.assertEqual(set(release.verify_checksums(metadata)), {"release-context.txt", "release-notes.md"})
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        upload = next(call for call in self.gh_calls() if call[:2] == ["release", "upload"])
        self.assertEqual(set(upload[3:-1]), set(release.verify_checksums(packages)) | {"SHA256SUMS"})
        self.assertEqual((self.work / "sent-notes.md").read_bytes(), source_notes.read_bytes())

    def test_release_metadata_rejects_unsafe_identity_and_incomplete_sources(self):
        source, notes, _image = self.metadata_sources()
        for repository, tag in (("../repo", "v3.2.0"), ("owner/repo/extra", "v3.2.0"),
                                ("-owner/repo", "v3.2.0"), ("owner/repo", "../v3.2.0"),
                                ("owner/repo", "v3.2.0-linux.01"), ("owner/repo", "main")):
            with self.subTest(repository=repository, tag=tag), self.assertRaises(ValueError):
                self.stage_metadata(source, repository=repository, tag=tag)
        notes.unlink()
        with self.assertRaisesRegex(ValueError, "notes are required for a launch image"):
            self.stage_metadata(source)

    def test_release_metadata_rejects_links_invalid_content_and_existing_output(self):
        source, notes, image = self.metadata_sources()
        valid_image = image.read_bytes()
        image.write_bytes(b"not a PNG")
        with self.assertRaisesRegex(ValueError, "must be PNG"):
            self.stage_metadata(source)
        image.write_bytes(valid_image)
        original_notes = notes.read_bytes()
        notes.write_text("\0")
        with self.assertRaisesRegex(ValueError, "invalid release notes"):
            self.stage_metadata(source)
        notes.write_bytes(original_notes)
        external = self.work / "external.md"
        notes.rename(external)
        notes.symlink_to(external)
        with self.assertRaisesRegex(ValueError, "links forbidden"):
            self.stage_metadata(source)
        notes.unlink()
        notes.write_bytes(original_notes)
        output = self.stage_metadata(source)
        with self.assertRaisesRegex(ValueError, "must be empty"):
            self.stage_metadata(source, output)

    def test_publish_refuses_bad_metadata_and_identity_before_any_github_call(self):
        packages, metadata = self.publish_fixture()
        original = {path.name: path.read_bytes() for path in metadata.iterdir()}
        for mutation in ("corrupt", "incomplete", "missing-image", "context", "unlisted", "link"):
            with self.subTest(mutation=mutation):
                for path in metadata.iterdir():
                    path.unlink()
                for name, data in original.items():
                    (metadata / name).write_bytes(data)
                if mutation == "corrupt":
                    (metadata / "v3.2.0-launch.png").write_bytes(b"\x89PNG\r\n\x1a\nchanged")
                elif mutation == "incomplete":
                    (metadata / "release-notes.md").unlink()
                elif mutation == "missing-image":
                    (metadata / "v3.2.0-launch.png").unlink()
                    self.manifest(metadata)
                elif mutation == "context":
                    (metadata / "release-context.txt").write_text("other/repository\nv3.2.0\n")
                    self.manifest(metadata)
                elif mutation == "unlisted":
                    (metadata / "run-me.sh").write_text("exit 0")
                else:
                    (metadata / "v3.2.0-launch.png").unlink()
                    (metadata / "v3.2.0-launch.png").symlink_to(self.work / "repository/.github/media/releases/v3.2.0-launch.png")
                result = self.publish()
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.gh_calls(), [])
        for tag, repository in (("../v3.2.0", "owner/repository"), ("v3.2.0", "--repo=other/repo")):
            result = self.publish(tag=tag, repository=repository)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.gh_calls(), [])

    def test_publish_rejects_unsafe_or_incomplete_package_handoff_before_github(self):
        packages, metadata = self.publish_fixture()
        original = (packages / "SHA256SUMS").read_text()
        for invalid in (original.replace(release.TARBALL, "../" + release.TARBALL),
                        "\n".join(original.splitlines()[:-1]) + "\n", original + original.splitlines()[0] + "\n"):
            (packages / "SHA256SUMS").write_text(invalid)
            result = self.publish()
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.gh_calls(), [])

    def test_published_release_stays_immutable_including_launch_artwork(self):
        packages, metadata = self.publish_fixture()
        remote = self.work / "remote"
        shutil.copytree(packages, remote)
        shutil.copyfile(metadata / "v3.2.0-launch.png", remote / "v3.2.0-launch.png")
        result = self.publish(state="published")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("leaving the release unchanged", result.stdout)
        self.assertFalse(any(call[1] in {"create", "upload", "edit"} for call in self.gh_calls()))
        # Restore the pre-publication package inventory before exercising another run.
        (packages / "v3.2.0-launch.png").unlink()
        (remote / "v3.2.0-launch.png").write_bytes(b"different published image")
        result = self.publish(state="published")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different artwork", result.stdout)
        self.assertFalse(any(call[1] in {"create", "upload", "edit"} for call in self.gh_calls()))
        (packages / "v3.2.0-launch.png").unlink()
        (remote / "SHA256SUMS").write_text("different published package manifest\n")
        result = self.publish(state="published")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different packages", result.stdout)
        self.assertFalse(any(call[1] in {"create", "upload", "edit"} for call in self.gh_calls()))

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
