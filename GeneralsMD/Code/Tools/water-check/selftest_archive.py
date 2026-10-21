from __future__ import annotations

import json
import shutil
from collections.abc import Mapping
from pathlib import Path

from bitmap import inspect_bmp, inspect_bmp_member
from capture_archive import (
    MAX_BMP_MEMBER_BYTES,
    ArchiveError,
    DefaultArchiveIO,
    read_member,
    seal_bmp_sequence,
)
from models import ExpectedImage, Rect


class ArchiveSelfTestFailure(RuntimeError):
    pass


class FaultArchiveIO:
    def __init__(
        self,
        *,
        fail_archive: bool = False,
        fail_manifest_call: int | None = None,
        fail_unlink_call: int | None = None,
    ) -> None:
        self._default = DefaultArchiveIO()
        self._fail_archive = fail_archive
        self._fail_manifest_call = fail_manifest_call
        self._fail_unlink_call = fail_unlink_call
        self.manifest_calls = 0
        self.unlink_calls = 0

    def write_archive(self, path: Path, frames: tuple[Path, ...]) -> None:
        if self._fail_archive:
            raise OSError("injected archive creation failure")
        self._default.write_archive(path, frames)

    def write_manifest(self, path: Path, value: Mapping[str, object]) -> None:
        self.manifest_calls += 1
        if self.manifest_calls == self._fail_manifest_call:
            raise OSError("injected manifest commit failure")
        self._default.write_manifest(path, value)

    def unlink_raw(self, path: Path) -> None:
        self.unlink_calls += 1
        if self.unlink_calls == self._fail_unlink_call:
            raise OSError("injected raw cleanup failure")
        self._default.unlink_raw(path)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ArchiveSelfTestFailure(message)


def test_capture_archive(root: Path) -> None:
    _test_short_capture_keeps_raw(root)
    _test_profitable_capture_is_sealed(root)
    _test_transaction_failures_keep_raw(root)


def _make_bmp(seed: int, payload_size: int = 192) -> bytes:
    body = bytearray((seed + offset * 7) % 256 for offset in range(payload_size))
    body[:2] = b"BM"
    body[2:6] = len(body).to_bytes(4, "little")
    body[10:14] = (54).to_bytes(4, "little")
    body[14:18] = (40).to_bytes(4, "little")
    body[18:22] = (8).to_bytes(4, "little", signed=True)
    body[22:26] = (4).to_bytes(4, "little", signed=True)
    body[26:28] = (1).to_bytes(2, "little")
    body[28:30] = (24).to_bytes(2, "little")
    body[30:34] = (0).to_bytes(4, "little")
    return bytes(body)


def _test_short_capture_keeps_raw(root: Path) -> None:
    source = root / "short-source"
    source.mkdir()
    originals = {f"frame{index:06d}.bmp": _make_bmp(seed) for index, seed in enumerate((11, 37, 83))}
    for name, data in originals.items():
        (source / name).write_bytes(data)
    archive = root / "short.zip"
    manifest = root / "short.json"
    result = seal_bmp_sequence(source, archive, manifest, {"frame000001.bmp"})
    _require(result["classification"] == "raw_retained_no_savings", "short capture was redundantly archived")
    _require(not archive.exists() and not manifest.exists(), "no-savings capture grew new storage")
    for name, expected in originals.items():
        _require((source / name).read_bytes() == expected, f"no-savings seal changed {name}")


def _test_profitable_capture_is_sealed(root: Path) -> None:
    source = root / "archive-source"
    source.mkdir()
    originals: dict[str, bytes] = {}
    for index, seed in enumerate(range(24)):
        name = f"frame{index:06d}.bmp"
        body = _make_bmp(seed, 128 * 1024)
        originals[name] = body
        (source / name).write_bytes(body)
    archive = root / "frames.zip"
    manifest = root / "frames.json"
    final_write_trap = FaultArchiveIO(fail_manifest_call=2)
    result = seal_bmp_sequence(
        source,
        archive,
        manifest,
        {"frame000000.bmp", "frame000012.bmp", "frame000023.bmp"},
        archive_io=final_write_trap,
    )
    _require(final_write_trap.manifest_calls == 1, "seal retained a destructive second manifest boundary")
    _require(result["member_count"] == 24, "archive omitted frames")
    storage_before = result.get("storage_before")
    storage_after = result.get("storage_after")
    _require(
        isinstance(storage_before, int) and isinstance(storage_after, int) and storage_after < storage_before,
        "sealed capture did not save storage",
    )
    _require((source / "frame000012.bmp").read_bytes() == originals["frame000012.bmp"], "keyframe changed")
    _require(not (source / "frame000001.bmp").exists(), "non-key raw frame was retained")
    _require(not archive.with_suffix(".zip.tmp").exists(), "temporary archive survived")
    for name, expected in originals.items():
        _require(read_member(archive, manifest, name) == expected, f"archive round trip changed {name}")

    image_expected = ExpectedImage(Rect(0, 0, 8, 4), 1, 0.0, 1.0)
    direct = inspect_bmp(source / "frame000012.bmp", image_expected)
    archived = inspect_bmp_member(archive, manifest, "frame000012.bmp", image_expected)
    _require(direct == archived, "archive-aware BMP validation changed image output")

    failure_source = root / "failure-source"
    failure_source.mkdir()
    failure_originals = {name: originals[name] for name in sorted(originals)[:3]}
    for name, data in failure_originals.items():
        (failure_source / name).write_bytes(data)
    failure_archive = root / "failure-source.zip"
    failure_manifest = root / "failure-source.json"
    seal_bmp_sequence(
        failure_source, failure_archive, failure_manifest, {"frame000000.bmp"}, keep_raw=True
    )
    wrong = root / "wrong-hash.json"
    wrong_data = json.loads(failure_manifest.read_text(encoding="utf-8"))
    wrong_data["members"][0]["sha256"] = "0" * 64
    wrong.write_text(json.dumps(wrong_data), encoding="utf-8")
    _expect_archive_error(failure_archive, wrong, "frame000000.bmp", "wrong hash passed")

    absent = root / "missing-member.json"
    absent_data = json.loads(failure_manifest.read_text(encoding="utf-8"))
    absent_data["members"][0]["name"] = "missing.bmp"
    absent.write_text(json.dumps(absent_data), encoding="utf-8")
    _expect_archive_error(failure_archive, absent, "missing.bmp", "missing member passed")

    missing = root / "missing-member.zip"
    shutil.copy2(failure_archive, missing)
    missing.write_bytes(missing.read_bytes()[:-20])
    _expect_archive_error(missing, failure_manifest, "frame000000.bmp", "partial archive passed")

    traversal = root / "traversal.json"
    traversal_data = json.loads(failure_manifest.read_text(encoding="utf-8"))
    traversal_data["members"][0]["name"] = "../escape.bmp"
    traversal.write_text(json.dumps(traversal_data), encoding="utf-8")
    _expect_archive_error(failure_archive, traversal, "../escape.bmp", "traversal member passed")
    for name, expected_data in failure_originals.items():
        _require((failure_source / name).read_bytes() == expected_data, f"archive failure removed raw {name}")

    oversize = root / "oversize-member.json"
    oversize_data = json.loads(failure_manifest.read_text(encoding="utf-8"))
    oversize_data["members"][0]["length"] = MAX_BMP_MEMBER_BYTES + 1
    oversize.write_text(json.dumps(oversize_data), encoding="utf-8")
    _expect_archive_error_contains(
        failure_archive, oversize, "frame000000.bmp", "allocation limit", "oversize member was allocated"
    )

    malformed_source = root / "malformed-source"
    malformed_source.mkdir()
    valid_raw = malformed_source / "frame000000.bmp"
    malformed_raw = malformed_source / "frame000001.bmp"
    valid_raw.write_bytes(originals["frame000000.bmp"])
    malformed_raw.write_bytes(b"not-a-bmp")
    try:
        seal_bmp_sequence(malformed_source, root / "malformed.zip", root / "malformed.json", set())
    except ArchiveError:
        pass
    else:
        raise ArchiveSelfTestFailure("malformed capture input passed")
    _require(valid_raw.is_file() and malformed_raw.is_file(), "malformed capture failure removed raw input")

    blocked_source = root / "blocked-source"
    blocked_source.mkdir()
    blocked_raw = blocked_source / "frame000000.bmp"
    blocked_raw.write_bytes(originals["frame000000.bmp"])
    blocked_archive = root / "blocked.zip"
    blocked_archive.write_bytes(b"pre-existing")
    try:
        seal_bmp_sequence(blocked_source, blocked_archive, root / "blocked.json", set())
    except FileExistsError:
        pass
    else:
        raise ArchiveSelfTestFailure("pre-existing archive was overwritten")
    _require(blocked_raw.read_bytes() == originals["frame000000.bmp"], "failed seal removed raw input")


def _test_transaction_failures_keep_raw(root: Path) -> None:
    for label, fault in (
        ("archive-create", FaultArchiveIO(fail_archive=True)),
        ("manifest-commit", FaultArchiveIO(fail_manifest_call=1)),
        ("raw-cleanup", FaultArchiveIO(fail_unlink_call=4)),
    ):
        source = root / f"transaction-{label}"
        source.mkdir()
        originals: dict[str, bytes] = {}
        for index in range(24):
            name = f"frame{index:06d}.bmp"
            data = _make_bmp(index, 128 * 1024)
            originals[name] = data
            (source / name).write_bytes(data)
        archive = root / f"transaction-{label}.zip"
        manifest = root / f"transaction-{label}.json"
        try:
            seal_bmp_sequence(
                source,
                archive,
                manifest,
                {"frame000000.bmp", "frame000012.bmp", "frame000023.bmp"},
                archive_io=fault,
            )
        except (ArchiveError, OSError):
            pass
        else:
            raise ArchiveSelfTestFailure(f"{label} failure was not propagated")
        for name, expected in originals.items():
            _require((source / name).read_bytes() == expected, f"{label} failure did not retain raw {name}")
        _require(not archive.exists(), f"{label} failure committed archive")
        _require(not manifest.exists(), f"{label} failure committed manifest")
        _require(not archive.with_suffix(".zip.tmp").exists(), f"{label} failure retained temporary archive")


def _expect_archive_error(archive: Path, manifest: Path, member: str, message: str) -> None:
    try:
        read_member(archive, manifest, member)
    except ArchiveError:
        return
    raise ArchiveSelfTestFailure(message)


def _expect_archive_error_contains(
    archive: Path, manifest: Path, member: str, expected: str, message: str
) -> None:
    try:
        read_member(archive, manifest, member)
    except ArchiveError as error:
        _require(expected in str(error), message)
        return
    raise ArchiveSelfTestFailure(message)
