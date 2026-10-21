from __future__ import annotations

import re
from dataclasses import dataclass

from models import FixtureRoute

SCENARIO_REPORT = re.compile(r"HEADLESS SCENARIO:\s+(\d+) actions,\s+(\d+) ran,\s+(\d+) refused")
FIXTURE_COMMAND = re.compile(
    r"water-capture-command:\s+frame=(\d+) requested=(\d+) action=(\d+) player=(\S+) "
    r"index=(-?\d+) allowed=(\d+) success=(\d+) object=(\d+) "
    r"x=([-+0-9.]+) y=([-+0-9.]+)"
)
LOADED_SCENARIO = re.compile(r"SCENARIO: loaded .*?,\s*(\d+) actions,\s*(\d+) lines refused")
WATER_DRAW = re.compile(r"-dx11 drew:.*engine:[^\s:]*water[^\s:]*:.*->\s*([1-9]\d*) draws", re.IGNORECASE)
BROKEN_AUTHORED_WORLD = re.compile(r"Failed to find initial camera|Marking AI player .* as defeated")


@dataclass(frozen=True, slots=True)
class FixtureCommand:
    frame: int
    action: int
    player: str
    allowed: bool
    success: bool
    object_id: str
    x: float
    y: float


def scenario_report_valid(text: str) -> bool:
    matches = tuple(SCENARIO_REPORT.finditer(text))
    return bool(matches) and all(
        int(match.group(1)) > 0
        and int(match.group(2)) == int(match.group(1))
        and int(match.group(3)) == 0
        for match in matches
    )


def fixture_commands(text: str) -> tuple[FixtureCommand, ...]:
    return tuple(
        FixtureCommand(
            int(match.group(1)), int(match.group(3)), match.group(4),
            match.group(6) == "1", match.group(7) == "1", match.group(8),
            float(match.group(9)), float(match.group(10)),
        )
        for match in FIXTURE_COMMAND.finditer(text)
    )


def vessel_route_valid(text: str, route: FixtureRoute) -> bool:
    commands = fixture_commands(text)
    successful = tuple(command for command in commands if command.allowed and command.success)
    if not successful or any(command.player != route.player_name for command in successful):
        return False
    object_ids = {command.object_id for command in successful if command.object_id != "0"}
    if len(object_ids) != 1:
        return False
    object_id = next(iter(object_ids))
    spawn = route.poses[0]
    if not any(
        command.action == 0 and command.frame == spawn.frame and command.object_id == object_id
        and abs(command.x - spawn.x) < 0.01 and abs(command.y - spawn.y) < 0.01
        for command in successful
    ):
        return False
    for expected in route.poses[1:]:
        stops = tuple(command for command in successful if command.action == 4 and command.frame == expected.frame)
        prior_moves = tuple(
            command for command in successful
            if command.action == 1 and command.frame < expected.frame and command.object_id == object_id
        )
        if not stops or not prior_moves:
            return False
        target = prior_moves[-1]
        if abs(target.x - expected.x) >= 0.01 or abs(target.y - expected.y) >= 0.01:
            return False
    return True


def scenario_commands_valid(text: str, route: FixtureRoute) -> bool:
    commands = fixture_commands(text)
    loaded = tuple(LOADED_SCENARIO.finditer(text))
    return (
        len(loaded) == 1 and int(loaded[0].group(1)) == len(commands)
        and int(loaded[0].group(2)) == 0
        and all(command.allowed and command.success for command in commands)
        and vessel_route_valid(text, route)
    )


def water_draw_valid(text: str) -> bool:
    return WATER_DRAW.search(text) is not None


def authored_world_valid(text: str) -> bool:
    return BROKEN_AUTHORED_WORLD.search(text) is None
