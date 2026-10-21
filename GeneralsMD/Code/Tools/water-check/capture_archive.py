from __future__ import annotations

import hashlib
import json
import math
import os
import struct
import zipfile
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Protocol

MAX_BMP_MEMBER_BYTES = 64 * 1024 * 1024


@dataclass(frozen=True, slots=True)
class ArchiveError(ValueError):
    reason: str

    def __str__(self) -> str:
        return self.reason


class ReadableBytes(Protocol):
    def read(self, size: int = -1, /) -> bytes: ...


class ArchiveIO(Protocol):
    def write_archive(self, path: Path, frames: tuple[Path, ...]) -> None: ...
    def write_manifest(self, path: Path, value: Mapping[str, object]) -> None: ...
    def unlink_raw(self, path: Path) -> None: ...


class DefaultArchiveIO:
    def write_archive(self, path: Path, frames: tuple[Path, ...]) -> None:
        with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as zipped:
            for frame in frames:
                zipped.write(frame, frame.name)

    def write_manifest(self, path: Path, value: Mapping[str, object]) -> None:
        _write_json_atomic(path, value)

    def unlink_raw(self, path: Path) -> None:
        path.unlink()


def _sha256_stream(source: ReadableBytes) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    while True:
        block = source.read(1024 * 1024)
        if not block:
            return digest.hexdigest(), size
        digest.update(block)
        size += len(block)


def _file_sha256(path: Path) -> str:
    with path.open("rb") as source:
        return _sha256_stream(source)[0]


def _bmp_info(path: Path) -> tuple[int, int, int]:
    if path.stat().st_size > MAX_BMP_MEMBER_BYTES:
        raise ArchiveError(f"native BMP exceeds {MAX_BMP_MEMBER_BYTES} byte limit: {path}")
    with path.open("rb") as source:
        header = source.read(54)
    return _bmp_header_info(header, str(path))


def _bmp_header_info(header: bytes, label: str) -> tuple[int, int, int]:
    if len(header) < 54 or header[:2] != b"BM":
        raise ArchiveError(f"not a native BMP: {label}")
    _, width, signed_height, planes, bits, compression = struct.unpack_from("<IiiHHI", header, 14)
    if width <= 0 or signed_height == 0 or planes != 1 or bits not in (24, 32) or compression != 0:
        raise ArchiveError(f"unsupported native BMP: {label}")
    return width, abs(signed_height), bits


def _safe_member(name: str) -> bool:
    member = PurePosixPath(name)
    return bool(name) and not member.is_absolute() and ".." not in member.parts and len(member.parts) == 1


def _verify_members(archive: Path, records: tuple[dict[str, object], ...]) -> None:
    expected = {str(record["name"]): record for record in records}
    try:
        with zipfile.ZipFile(archive, "r") as zipped:
            names = zipped.namelist()
            if len(names) != len(set(names)) or set(names) != set(expected):
                raise ArchiveError("archive member inventory differs from manifest")
            for name in names:
                if not _safe_member(name):
                    raise ArchiveError(f"unsafe archive member: {name}")
                with zipped.open(name, "r") as source:
                    digest, size = _sha256_stream(source)
                record = expected[name]
                if digest != record["sha256"] or size != record["length"]:
                    raise ArchiveError(f"archive member bytes differ: {name}")
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        raise ArchiveError(f"could not verify archive: {error}") from error


def _json_bytes(value: Mapping[str, object]) -> bytes:
    return (json.dumps(value, indent=2) + "\n").encode("utf-8")


def _set_storage_totals(result: dict[str, object], fixed_bytes: int, storage_before: int) -> int:
    previous = -1
    after = fixed_bytes
    for _ in range(16):
        result["storage_after"] = after
        result["storage_saved"] = storage_before - after
        current = fixed_bytes + len(_json_bytes(result))
        if current == after:
            return storage_before - after
        previous, after = after, current
        if after == previous:
            return storage_before - after
    raise ArchiveError("archive storage accounting did not converge")


def seal_bmp_sequence(
    directory: Path,
    archive: Path,
    manifest: Path,
    keyframes: set[str],
    provenance: Mapping[str, str | int | float | bool | None] | None = None,
    *,
    keep_raw: bool = False,
    archive_io: ArchiveIO | None = None,
) -> dict[str, object]:
    io = archive_io or DefaultArchiveIO()
    for target in (archive, manifest, archive.with_suffix(archive.suffix + ".tmp")):
        if target.exists():
            raise FileExistsError(target)
    frames = tuple(sorted(directory.glob("*.bmp")))
    if not frames or not keyframes.issubset({path.name for path in frames}):
        raise ArchiveError("capture frames or declared keyframes are missing")
    records: list[dict[str, object]] = []
    for frame in frames:
        width, height, bits = _bmp_info(frame)
        records.append({
            "name": frame.name,
            "length": frame.stat().st_size,
            "sha256": _file_sha256(frame),
            "width": width,
            "height": height,
            "bit_depth": bits,
            "loose_keyframe": frame.name in keyframes,
        })
    frozen_records = tuple(records)
    archive.parent.mkdir(parents=True, exist_ok=True)
    temporary = archive.with_suffix(archive.suffix + ".tmp")
    created_archive = False
    try:
        io.write_archive(temporary, frames)
        _verify_members(temporary, frozen_records)
        archive_length = temporary.stat().st_size
        source_length = sum(frame.stat().st_size for frame in frames)
        retained_length = source_length if keep_raw else sum(
            frame.stat().st_size for frame in frames if frame.name in keyframes
        )
        minimum_savings = max(1024 * 1024, math.ceil(source_length * 0.05))
        result: dict[str, object] = {
            "schema_version": 1,
            "classification": "sealed",
            "native_capture_format": "bmp",
            "archive_format": "zip-deflate",
            "byte_exact_members": True,
            "archive": archive.name,
            "archive_sha256": _file_sha256(temporary),
            "archive_length": archive_length,
            "source_length": source_length,
            "member_count": len(records),
            "members": records,
            "provenance": dict(provenance or {}),
            "raw_cleanup_complete": not keep_raw,
            "storage_before": source_length,
            "minimum_savings": minimum_savings,
        }
        storage_saved = _set_storage_totals(result, archive_length + retained_length, source_length)
        if not keep_raw and storage_saved < minimum_savings:
            temporary.unlink()
            return {
                "schema_version": 1,
                "classification": "raw_retained_no_savings",
                "native_capture_format": "bmp",
                "member_count": len(records),
                "source_length": source_length,
                "storage_before": source_length,
                "storage_after": source_length,
                "storage_saved": 0,
                "minimum_savings": minimum_savings,
                "provenance": dict(provenance or {}),
                "raw_cleanup_complete": True,
            }
        os.replace(temporary, archive)
        created_archive = True
        _verify_members(archive, frozen_records)
        io.write_manifest(manifest, result)
        if not keep_raw:
            nonkeys = tuple(frame for frame in frames if frame.name not in keyframes)
            try:
                for frame in nonkeys:
                    io.unlink_raw(frame)
            except OSError as error:
                _restore_raw_members(archive, directory, frozen_records, nonkeys)
                archive.unlink(missing_ok=True)
                manifest.unlink(missing_ok=True)
                raise ArchiveError(f"raw cleanup failed; complete raw set restored: {error}") from error
        retained_after = sum(frame.stat().st_size for frame in frames if frame.exists())
        actual_after = archive.stat().st_size + manifest.stat().st_size + retained_after
        if actual_after != result["storage_after"]:
            raise ArchiveError("archive storage accounting differs from committed files")
        return result
    except (ArchiveError, OSError, zipfile.BadZipFile):
        temporary.unlink(missing_ok=True)
        if created_archive and not manifest.exists():
            archive.unlink(missing_ok=True)
        raise


def _restore_raw_members(
    archive: Path,
    directory: Path,
    records: tuple[dict[str, object], ...],
    candidates: tuple[Path, ...],
) -> None:
    by_name = {str(record["name"]): record for record in records}
    try:
        with zipfile.ZipFile(archive, "r") as zipped:
            for target in candidates:
                if target.exists():
                    continue
                record = by_name[target.name]
                expected_length = record["length"]
                expected_hash = record["sha256"]
                if not isinstance(expected_length, int) or not isinstance(expected_hash, str):
                    raise ArchiveError(f"invalid rollback metadata: {target.name}")
                temporary = directory / f".{target.name}.restore.tmp"
                digest = hashlib.sha256()
                written = 0
                with zipped.open(target.name, "r") as source, temporary.open("xb") as output:
                    while block := source.read(1024 * 1024):
                        written += len(block)
                        if written > expected_length or written > MAX_BMP_MEMBER_BYTES:
                            raise ArchiveError(f"rollback member exceeded bound: {target.name}")
                        digest.update(block)
                        output.write(block)
                if written != expected_length or digest.hexdigest() != expected_hash:
                    raise ArchiveError(f"rollback member bytes differ: {target.name}")
                os.replace(temporary, target)
    except (OSError, KeyError, zipfile.BadZipFile) as error:
        raise ArchiveError(f"could not restore raw capture after cleanup failure: {error}") from error


def _write_json_atomic(path: Path, value: Mapping[str, object], *, replace: bool = False) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    if path.exists() and not replace:
        raise FileExistsError(path)
    temporary.write_bytes(_json_bytes(value))
    os.replace(temporary, path)


def read_member(archive: Path, manifest: Path, member: str) -> bytes:
    if not _safe_member(member):
        raise ArchiveError(f"unsafe archive member: {member}")
    try:
        value: object = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ArchiveError(f"invalid archive manifest: {error}") from error
    if not isinstance(value, dict) or value.get("classification") != "sealed":
        raise ArchiveError("archive manifest is not sealed")
    records = value.get("members")
    if not isinstance(records, list):
        raise ArchiveError("archive manifest has no members")
    manifest_names = [record.get("name") for record in records if isinstance(record, dict)]
    if (
        len(manifest_names) != len(records) or not all(isinstance(name, str) and _safe_member(name) for name in manifest_names)
        or len(manifest_names) != len(set(manifest_names))
    ):
        raise ArchiveError("archive manifest member inventory is unsafe or duplicated")
    matches = [record for record in records if isinstance(record, dict) and record.get("name") == member]
    if len(matches) != 1:
        raise ArchiveError(f"archive member is missing or duplicated: {member}")
    record = matches[0]
    expected_hash = record.get("sha256")
    expected_length = record.get("length")
    if not isinstance(expected_hash, str) or not isinstance(expected_length, int):
        raise ArchiveError(f"archive member metadata is invalid: {member}")
    if expected_length < 54 or expected_length > MAX_BMP_MEMBER_BYTES:
        raise ArchiveError(f"archive member exceeds {MAX_BMP_MEMBER_BYTES} byte allocation limit: {member}")
    try:
        with zipfile.ZipFile(archive, "r") as zipped:
            names = zipped.namelist()
            if (
                len(names) != len(set(names)) or set(names) != set(manifest_names)
                or not all(_safe_member(name) for name in names)
            ):
                raise ArchiveError("archive member inventory differs from manifest")
            info = zipped.getinfo(member)
            if info.file_size != expected_length:
                raise ArchiveError(f"archive member declared size differs: {member}")
            with zipped.open(info, "r") as source:
                header = source.read(54)
                _bmp_header_info(header, member)
                if int.from_bytes(header[2:6], "little") != expected_length:
                    raise ArchiveError(f"BMP header size differs from manifest: {member}")
                data_buffer = bytearray(header)
                while len(data_buffer) < expected_length:
                    remaining = expected_length - len(data_buffer)
                    block = source.read(min(1024 * 1024, remaining))
                    if not block:
                        break
                    data_buffer.extend(block)
                if len(data_buffer) != expected_length or source.read(1):
                    raise ArchiveError(f"archive member stream length differs: {member}")
                data = bytes(data_buffer)
    except (OSError, zipfile.BadZipFile, KeyError) as error:
        raise ArchiveError(f"could not read archive member: {error}") from error
    if len(data) != expected_length or hashlib.sha256(data).hexdigest() != expected_hash:
        raise ArchiveError(f"archive member bytes differ: {member}")
    return data


def seal_new_capture_videos(
    user_data: Path, provenance: Mapping[str, str | int | float | bool | None]
) -> tuple[dict[str, object], ...]:
    directories = sorted({path.parent for path in user_data.rglob("frame*.bmp")})
    archives: list[dict[str, object]] = []
    for directory in directories:
        frames = tuple(sorted(directory.glob("frame*.bmp")))
        if not frames:
            continue
        keyframes = {frames[0].name, frames[len(frames) // 2].name, frames[-1].name}
        archive = directory.with_name(directory.name + ".frames.zip")
        manifest = directory.with_name(directory.name + ".frames.json")
        result = seal_bmp_sequence(directory, archive, manifest, keyframes, provenance)
        if result.get("raw_cleanup_complete") is not True:
            raise ArchiveError(f"raw frame cleanup is incomplete: {directory}")
        if result.get("classification") == "raw_retained_no_savings":
            archives.append({
                "classification": result["classification"],
                "source_directory": str(directory),
                "member_count": result["member_count"],
                "source_length": result["source_length"],
                "storage_after": result["storage_after"],
                "minimum_savings": result["minimum_savings"],
                "loose_keyframes": [],
            })
            continue
        archives.append({
            "classification": result["classification"],
            "archive": str(archive),
            "manifest": str(manifest),
            "source_directory": str(directory),
            "member_count": result["member_count"],
            "source_length": result["source_length"],
            "archive_length": result["archive_length"],
            "archive_sha256": result["archive_sha256"],
            "loose_keyframes": sorted(keyframes),
        })
    return tuple(archives)


def attach_archive_locations(
    observations: list[dict[str, object]], archives: tuple[dict[str, object], ...]
) -> None:
    for observation in observations:
        path_value = observation.get("path")
        if not isinstance(path_value, str):
            continue
        path = Path(path_value)
        for archive in archives:
            source_value = archive.get("source_directory")
            archive_value = archive.get("archive")
            manifest_value = archive.get("manifest")
            if (
                isinstance(source_value, str) and isinstance(archive_value, str)
                and isinstance(manifest_value, str) and path.parent == Path(source_value)
            ):
                observation["archive"] = archive_value
                observation["archive_manifest"] = manifest_value
                observation["archive_member"] = path.name
                observation["loose_path"] = str(path) if path.is_file() else None
                break
