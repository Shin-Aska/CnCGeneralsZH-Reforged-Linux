from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum, unique
from pathlib import Path


@unique
class Platform(StrEnum):
    WINDOWS = "windows"
    LINUX = "linux"
    MACOS = "macos"


@unique
class ResultClass(StrEnum):
    PASSED = "passed"
    FAILED = "failed"
    UNAVAILABLE = "unavailable"


@dataclass(frozen=True, slots=True)
class Rect:
    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True, slots=True)
class ExpectedImage:
    roi: Rect
    min_water_pixels: int
    min_luma: float
    max_luma: float


@dataclass(frozen=True, slots=True)
class ImageObservation:
    width: int
    height: int
    water_pixels: int
    min_luma: float
    max_luma: float
    visible_pixels: int
    sha256: str


@dataclass(frozen=True, slots=True)
class CaptureIdentity:
    executable_sha256: str
    data_sha256: str
    scene_id: str
    picture_time: float
    backend: str
    water_style: str


@dataclass(frozen=True, slots=True)
class LaunchResult:
    pid: int
    returncode: int
    timed_out: bool
    elapsed_seconds: float
    evidence_frame: int
    screenshot_frames: tuple[int, ...]
    control_quit_requested: bool
    control_error: str | None


@dataclass(frozen=True, slots=True)
class Camera:
    x: float
    y: float


@dataclass(frozen=True, slots=True)
class RoutePose:
    frame: int
    x: float
    y: float
    phase: str


@dataclass(frozen=True, slots=True)
class FixtureRoute:
    player_name: str
    template: str
    poses: tuple[RoutePose, ...]


@dataclass(frozen=True, slots=True)
class Scene:
    scene_id: str
    map_path: str
    camera: Camera
    screenshot_frame: int
    max_frames: int
    scenario: str | None
    required_assets: tuple[str, ...]
    game_data_append: str | None
    expected_image: ExpectedImage
    requires_motion: bool
    requires_vessel: bool
    route: FixtureRoute | None


@dataclass(frozen=True, slots=True)
class SceneCatalog:
    schema_version: int
    scenes: tuple[Scene, ...]


@dataclass(frozen=True, slots=True)
class RunPaths:
    executable: Path
    root: Path
    overlay: Path | None
    output: Path


@dataclass(frozen=True, slots=True)
class SourceIdentity:
    git_head: str
    tracked_patch_sha256: str
    untracked_product_sha256: str
    untracked_product_files: int
