from __future__ import annotations

import json
import re
from dataclasses import dataclass
from itertools import pairwise
from pathlib import Path

from models import (
    Camera,
    ExpectedImage,
    FixtureRoute,
    Rect,
    RoutePose,
    Scene,
    SceneCatalog,
)

SAFE_TOKEN = re.compile(r"^[A-Za-z0-9_-]+$")


@dataclass(frozen=True, slots=True)
class SchemaError(ValueError):
    location: str
    reason: str

    def __str__(self) -> str:
        return f"{self.location}: {self.reason}"


def _mapping(value: object, location: str) -> dict[str, object]:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise SchemaError(location, "must be an object")
    return {str(key): item for key, item in value.items()}


def _sequence(value: object, location: str) -> list[object]:
    if not isinstance(value, list):
        raise SchemaError(location, "must be an array")
    return value


def _string(value: object, location: str) -> str:
    if not isinstance(value, str) or not value:
        raise SchemaError(location, "must be a non-empty string")
    return value


def _integer(value: object, location: str, minimum: int = 0) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        raise SchemaError(location, f"must be an integer >= {minimum}")
    return value


def _number(value: object, location: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise SchemaError(location, "must be a number")
    result = float(value)
    if not (-1_000_000.0 <= result <= 1_000_000.0):
        raise SchemaError(location, "must be finite and bounded")
    return result


def _boolean(value: object, location: str) -> bool:
    if not isinstance(value, bool):
        raise SchemaError(location, "must be a boolean")
    return value


def _asset_path(value: object, location: str) -> str:
    asset = _string(value, location)
    path = Path(asset)
    if "\\" in asset or path.is_absolute() or ".." in path.parts:
        raise SchemaError(location, "must be a safe forward-slash relative path")
    return asset


def _fixture_basename(value: object, location: str) -> str:
    name = _string(value, location)
    if "\\" in name or "/" in name or Path(name).name != name or not name.endswith(".ini"):
        raise SchemaError(location, "must be a safe fixture-root .ini basename")
    return name


def _parse_route(value: object, location: str) -> FixtureRoute:
    route = _mapping(value, location)
    player_name = _string(route.get("player_name"), f"{location}.player_name")
    template = _string(route.get("template"), f"{location}.template")
    if template != "WaterCaptureCarrier":
        raise SchemaError(f"{location}.template", "must be WaterCaptureCarrier")
    raw_poses = _sequence(route.get("poses"), f"{location}.poses")
    poses = tuple(
        RoutePose(
            _integer(_mapping(raw, f"{location}.poses[{index}]").get("frame"), f"{location}.poses[{index}].frame"),
            _number(_mapping(raw, f"{location}.poses[{index}]").get("x"), f"{location}.poses[{index}].x"),
            _number(_mapping(raw, f"{location}.poses[{index}]").get("y"), f"{location}.poses[{index}].y"),
            _string(_mapping(raw, f"{location}.poses[{index}]").get("phase"), f"{location}.poses[{index}].phase"),
        )
        for index, raw in enumerate(raw_poses)
    )
    if len(poses) < 2 or any(current.frame <= prior.frame for prior, current in pairwise(poses)):
        raise SchemaError(f"{location}.poses", "needs at least two poses in increasing frame order")
    return FixtureRoute(player_name, template, poses)


def _parse_scene(value: object, index: int) -> Scene:
    location = f"scenes[{index}]"
    raw = _mapping(value, location)
    scene_id = _string(raw.get("id"), f"{location}.id")
    if SAFE_TOKEN.fullmatch(scene_id) is None:
        raise SchemaError(f"{location}.id", "contains unsafe characters")
    map_path = _string(raw.get("map_path"), f"{location}.map_path")
    if "\\" in map_path or not map_path.endswith(".map") or Path(map_path).is_absolute() or ".." in Path(map_path).parts:
        raise SchemaError(f"{location}.map_path", "must be a safe forward-slash relative .map path")
    camera = _mapping(raw.get("camera"), f"{location}.camera")
    roi = _mapping(raw.get("water_roi"), f"{location}.water_roi")
    expected = _mapping(raw.get("expected"), f"{location}.expected")
    shot = _integer(raw.get("screenshot_frame"), f"{location}.screenshot_frame", 1)
    maximum = _integer(raw.get("max_frames"), f"{location}.max_frames", 2)
    if maximum <= shot:
        raise SchemaError(f"{location}.max_frames", "must exceed screenshot_frame")
    scenario_value = raw.get("scenario")
    scenario = None if scenario_value is None else _string(scenario_value, f"{location}.scenario")
    assets = tuple(
        _asset_path(item, f"{location}.required_assets[{asset_index}]")
        for asset_index, item in enumerate(_sequence(raw.get("required_assets"), f"{location}.required_assets"))
    )
    append_value = raw.get("game_data_append")
    game_data_append = (
        None if append_value is None else _fixture_basename(append_value, f"{location}.game_data_append")
    )
    requires_motion = _boolean(expected.get("requires_motion"), f"{location}.expected.requires_motion")
    requires_vessel = _boolean(expected.get("requires_vessel"), f"{location}.expected.requires_vessel")
    route_value = raw.get("route")
    route = None if route_value is None else _parse_route(route_value, f"{location}.route")
    if requires_vessel and (scenario is None or route is None):
        raise SchemaError(location, "vessel evidence requires scenario and route/template metadata")
    if game_data_append is not None and scene_id != "water-fixture":
        raise SchemaError(f"{location}.game_data_append", "is only valid for the authored vessel fixture")
    return Scene(
        scene_id,
        map_path,
        Camera(_number(camera.get("x"), f"{location}.camera.x"), _number(camera.get("y"), f"{location}.camera.y")),
        shot,
        maximum,
        scenario,
        assets,
        game_data_append,
        ExpectedImage(
            Rect(
                _integer(roi.get("x"), f"{location}.water_roi.x"),
                _integer(roi.get("y"), f"{location}.water_roi.y"),
                _integer(roi.get("width"), f"{location}.water_roi.width", 1),
                _integer(roi.get("height"), f"{location}.water_roi.height", 1),
            ),
            _integer(expected.get("min_water_pixels"), f"{location}.expected.min_water_pixels", 1),
            _number(expected.get("min_luma"), f"{location}.expected.min_luma"),
            _number(expected.get("max_luma"), f"{location}.expected.max_luma"),
        ),
        requires_motion,
        requires_vessel,
        route,
    )


def load_scenes(path: Path) -> SceneCatalog:
    try:
        decoded: object = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SchemaError(str(path), f"invalid JSON at line {error.lineno}") from error
    root = _mapping(decoded, "root")
    version = _integer(root.get("schema_version"), "schema_version", 1)
    if version != 1:
        raise SchemaError("schema_version", "unsupported version")
    scenes = tuple(_parse_scene(value, index) for index, value in enumerate(_sequence(root.get("scenes"), "scenes")))
    if not scenes or len({scene.scene_id for scene in scenes}) != len(scenes):
        raise SchemaError("scenes", "must be non-empty with unique ids")
    return SceneCatalog(version, scenes)
