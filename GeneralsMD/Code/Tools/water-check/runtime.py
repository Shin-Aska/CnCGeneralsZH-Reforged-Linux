from __future__ import annotations

import hashlib
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import assert_never

from models import Platform, RunPaths, Scene, SourceIdentity

OVERRIDE_PATTERN = re.compile(r"^(ZH_WATER_|ZH_DX11_|ZH_D3D9_|ZH_GPU_|SDL_GPU_)")


@dataclass(frozen=True, slots=True)
class PathError(ValueError):
    path: Path
    reason: str

    def __str__(self) -> str:
        return f"{self.path}: {self.reason}"


@dataclass(frozen=True, slots=True)
class GameDataBase:
    data: bytes
    source_kind: str
    source_path: Path
    source_sha256: str
    member: str | None
    member_sha256: str
    member_length: int


@dataclass(frozen=True, slots=True)
class GameDataFragment:
    source_path: Path
    source_sha256: str
    source_length: int
    normalized_data: bytes


def normalize_paths(executable: Path, root: Path, overlay: Path | None, output: Path) -> RunPaths:
    resolved_executable = executable.resolve(strict=True)
    resolved_root = root.resolve(strict=True)
    resolved_overlay = overlay.resolve(strict=True) if overlay is not None else None
    resolved_output = output.resolve(strict=False)
    if not resolved_executable.is_file():
        raise PathError(resolved_executable, "executable is not a file")
    if not resolved_root.is_dir():
        raise PathError(resolved_root, "root is not a directory")
    if resolved_overlay is not None and not resolved_overlay.is_dir():
        raise PathError(resolved_overlay, "overlay is not a directory")
    if resolved_output.exists():
        raise FileExistsError(resolved_output)
    return RunPaths(resolved_executable, resolved_root, resolved_overlay, resolved_output)


def inherited_overrides(environment: Mapping[str, str]) -> tuple[str, ...]:
    return tuple(sorted(name for name in environment if OVERRIDE_PATTERN.match(name)))


def clean_child_environment(overrides: Mapping[str, str]) -> dict[str, str]:
    environment = {name: value for name, value in os.environ.items() if not OVERRIDE_PATTERN.match(name)}
    environment.update(overrides)
    return environment


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def asset_digest(root: Path) -> tuple[str, int]:
    digest = hashlib.sha256()
    count = 0
    for path in sorted(candidate for candidate in root.rglob("*") if candidate.is_file()):
        if path.name.endswith(("DebugLogFile.txt", ".log")):
            continue
        relative = path.relative_to(root).as_posix()
        digest.update(relative.encode("utf-8"))
        digest.update(bytes.fromhex(sha256(path)))
        count += 1
    return digest.hexdigest(), count


def source_identity(repository: Path) -> SourceIdentity:
    head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=repository, text=True, encoding="utf-8"
    ).strip()
    patch = subprocess.check_output(
        ["git", "diff", "--binary", "HEAD", "--", "GeneralsMD/Code"], cwd=repository
    )
    names = subprocess.check_output(
        ["git", "ls-files", "--others", "--exclude-standard", "-z", "--", "GeneralsMD/Code"],
        cwd=repository,
    ).decode("utf-8").split("\0")
    untracked = hashlib.sha256()
    count = 0
    for name in sorted(name for name in names if name):
        path = repository / name
        if path.is_file() and "__pycache__" not in path.parts:
            untracked.update(name.replace("\\", "/").encode("utf-8"))
            untracked.update(bytes.fromhex(sha256(path)))
            count += 1
    return SourceIdentity(head, hashlib.sha256(patch).hexdigest(), untracked.hexdigest(), count)


def nearby_baseline_revision(root: Path) -> str | None:
    candidates = tuple(parent / "intake" / "head.txt" for parent in root.parents[:4])
    for candidate in candidates:
        if candidate.is_file():
            revision = candidate.read_text(encoding="utf-8").strip()
            if len(revision) == 40 and all(character in "0123456789abcdef" for character in revision):
                return revision
    return None


def reserve_control_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind(("127.0.0.1", 0))
        return int(listener.getsockname()[1])


def materialize_farm(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    for path in source.rglob("*"):
        relative = path.relative_to(source)
        target = destination / relative
        if path.is_dir():
            target.mkdir(exist_ok=True)
        elif path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def materialize_overlay(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=False)
    merge_private_files(source, destination)


def materialize_windows_inputs(
    executable: Path, fixture_overlay: Path, farm: Path
) -> tuple[Path, dict[str, str]]:
    private_executable = farm / executable.name
    if executable.resolve() != private_executable.resolve():
        if private_executable.exists():
            private_executable.unlink()
        shutil.copy2(executable, private_executable)
    merge_private_files(fixture_overlay, farm)
    child_hashes: dict[str, str] = {}
    for source in sorted(path for path in fixture_overlay.rglob("*") if path.is_file()):
        relative = source.relative_to(fixture_overlay)
        target = farm / relative
        source_hash = sha256(source)
        if not target.is_file() or sha256(target) != source_hash:
            raise RuntimeError(f"Windows private input differs after staging: {relative}")
        child_hashes[relative.as_posix()] = source_hash
    if sha256(private_executable) != sha256(executable):
        raise RuntimeError("Windows private executable differs after staging")
    return private_executable, child_hashes


def stage_windows_user_map(map_path: str, fixture_overlay: Path, user_data: Path) -> tuple[Path, dict[str, str]]:
    relative_map = Path(map_path)
    if relative_map.is_absolute() or ".." in relative_map.parts or not relative_map.parts:
        raise ValueError(f"invalid fixture map path: {map_path}")
    source_map = fixture_overlay / relative_map
    source_ini = source_map.parent / "map.ini"
    if not source_map.is_file() or not source_ini.is_file():
        raise FileNotFoundError(source_map if not source_map.is_file() else source_ini)
    destination_directory = user_data / relative_map.parent
    destination_directory.mkdir(parents=True, exist_ok=False)
    hashes: dict[str, str] = {}
    for source in (source_map, source_ini):
        destination = destination_directory / source.name
        shutil.copy2(source, destination)
        source_hash = sha256(source)
        if sha256(destination) != source_hash:
            raise RuntimeError(f"Windows user map differs after staging: {source.name}")
        hashes[destination.relative_to(user_data).as_posix()] = source_hash
    return (destination_directory / source_map.name).resolve(), hashes


def merge_private_files(source: Path, destination: Path) -> None:
    for path in source.rglob("*"):
        target = destination / path.relative_to(source)
        if path.is_dir():
            target.mkdir(exist_ok=True)
        elif path.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                target.unlink()
            shutil.copy2(path, target)


def merge_standard_map_cache(farm: Path, fragment: Path) -> Path:
    archive = farm / "MapsZH.big"
    if not archive.is_file() or not fragment.is_file():
        raise FileNotFoundError(archive if not archive.is_file() else fragment)
    tool = Path(__file__).resolve().parent.parent / "bigfile.py"
    result = subprocess.run(
        [sys.executable, str(tool), "extract", str(archive), "Maps/MapCache.ini", "-o", str(farm)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
    )
    cache = farm / "Maps" / "MapCache.ini"
    if result.returncode != 0 or not cache.is_file():
        raise RuntimeError(f"could not extract standard map cache: {result.stdout.strip()}")
    standard = cache.read_text(encoding="latin-1")
    addition = fragment.read_text(encoding="utf-8")
    if standard.count("MapCache ") < 10 or not addition.startswith("MapCache "):
        raise ValueError("standard cache or water fixture fragment is malformed")
    cache.write_text(standard.rstrip() + "\n\n" + addition.rstrip() + "\n", encoding="latin-1")
    return cache


def resolve_game_data_base(root: Path, expected_sha256: str, expected_length: int) -> GameDataBase:
    loose_matches = tuple(
        path
        for path in root.rglob("*")
        if path.is_file() and path.relative_to(root).as_posix().casefold() == "data/ini/gamedata.ini"
    )
    if len(loose_matches) > 1:
        raise ValueError("effective loose GameData source is ambiguous")
    if loose_matches:
        source = loose_matches[0]
        data = source.read_bytes()
        text = data.decode("latin-1")
        if not text.lstrip().startswith("GameData") or "End" not in text.splitlines():
            raise ValueError("effective loose GameData source is malformed")
        result = GameDataBase(
            data, "loose_override", source, sha256(source), None, hashlib.sha256(data).hexdigest(), len(data)
        )
    else:
        archives = tuple(path for path in root.iterdir() if path.is_file() and path.name.casefold() == "inizh.big")
        if len(archives) != 1:
            raise ValueError("expected exactly one immutable INIZH.big source")
        archive = archives[0]
        tool = Path(__file__).resolve().parent.parent / "bigfile.py"
        with tempfile.TemporaryDirectory(prefix="water-check-gamedata-") as temporary:
            extraction = Path(temporary)
            completed = subprocess.run(
                [
                    sys.executable,
                    str(tool),
                    "extract",
                    str(archive),
                    "Data/INI/GameData.ini",
                    "-o",
                    str(extraction),
                ],
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
            )
            extracted = extraction / "Data" / "INI" / "GameData.ini"
            if completed.returncode != 0 or not extracted.is_file():
                raise ValueError(f"effective INIZH GameData member is unavailable: {completed.stdout.strip()}")
            data = extracted.read_bytes()
        result = GameDataBase(
            data,
            "inizh_big_member",
            archive,
            sha256(archive),
            "Data\\INI\\GameData.ini",
            hashlib.sha256(data).hexdigest(),
            len(data),
        )
    if result.source_kind == "inizh_big_member" and (
        result.member_sha256 != expected_sha256 or result.member_length != expected_length
    ):
        raise ValueError(
            "effective GameData member identity differs: "
            f"{result.member_length} bytes {result.member_sha256}"
        )
    return result


def _normalized_game_data_fragment(path: Path) -> tuple[bytes, str]:
    source_hash = sha256(path)
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"GameData fragment is not UTF-8: {path}") from error
    if "\x00" in text:
        raise ValueError("GameData fragment contains NUL")
    lines = text.splitlines()
    if not lines or lines[0].strip() != "GameData" or lines[-1].strip() != "End":
        raise ValueError("GameData fragment must be one complete GameData block")
    available = tuple(line for line in lines if "VertexWaterAvailableMaps" in line)
    if len(available) != 1 or '"' not in available[0]:
        raise ValueError("GameData fragment must quote exactly one vertex-water map path")
    normalized = ("\r\n".join(lines) + "\r\n").encode("utf-8")
    if sha256(path) != source_hash:
        raise RuntimeError("GameData fragment changed while it was read")
    return normalized, source_hash


def load_game_data_fragments(
    scenes: tuple[Scene, ...], fixture_root: Path
) -> dict[str, GameDataFragment]:
    loaded: dict[str, GameDataFragment] = {}
    for scene in scenes:
        if scene.game_data_append is None:
            continue
        fragment = fixture_root / scene.game_data_append
        if not fragment.is_file():
            raise FileNotFoundError(fragment)
        normalized, source_hash = _normalized_game_data_fragment(fragment)
        loaded[scene.scene_id] = GameDataFragment(
            fragment.resolve(), source_hash, fragment.stat().st_size, normalized
        )
    return loaded


def _atomic_private_write(target: Path, data: bytes) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".water-check.tmp")
    if temporary.exists():
        raise FileExistsError(temporary)
    created = False
    try:
        with temporary.open("xb") as output:
            created = True
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, target)
    except OSError:
        if created:
            temporary.unlink(missing_ok=True)
        raise


def stage_scene_game_data(
    farm: Path,
    fragment: GameDataFragment | None,
    base: GameDataBase,
) -> dict[str, object]:
    target = farm / "Data" / "INI" / "GameData.ini"
    common: dict[str, object] = {
        "source_kind": base.source_kind,
        "source_path": str(base.source_path),
        "source_sha256": base.source_sha256,
        "member": base.member,
        "base_sha256": base.member_sha256,
        "base_length": base.member_length,
        "target": str(target),
    }
    if fragment is None:
        if base.source_kind == "loose_override":
            if not target.is_file() or target.read_bytes() != base.data:
                _atomic_private_write(target, base.data)
            return common | {
                "mode": "loose_base_restored",
                "generated_sha256": sha256(target),
                "generated_length": target.stat().st_size,
                "extra_loose_files": [str(target)],
            }
        target.unlink(missing_ok=True)
        return common | {"mode": "archive_base_no_loose_override", "extra_loose_files": []}
    boundary = b"" if base.data.endswith(b"\r\n") else b"\r\n"
    generated = base.data + boundary + fragment.normalized_data
    _atomic_private_write(target, generated)
    if target.read_bytes() != generated:
        raise RuntimeError("generated private GameData bytes differ after staging")
    return common | {
        "mode": "authored_append",
        "fragment": str(fragment.source_path),
        "fragment_sha256": fragment.source_sha256,
        "fragment_length": fragment.source_length,
        "boundary": "existing_crlf" if not boundary else "added_crlf",
        "generated_sha256": sha256(target),
        "generated_length": target.stat().st_size,
        "extra_loose_files": [str(target)],
    }


def backend_name(platform: Platform) -> str:
    match platform:
        case Platform.WINDOWS:
            return "d3d11"
        case Platform.LINUX:
            return "vulkan"
        case Platform.MACOS:
            return "metal"
        case unreachable:
            assert_never(unreachable)
