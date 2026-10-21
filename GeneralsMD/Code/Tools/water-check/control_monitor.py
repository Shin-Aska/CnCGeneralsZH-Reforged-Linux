from __future__ import annotations

import subprocess
import sys
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from control_client import Control, ControlError


@dataclass(frozen=True, slots=True)
class ControlPlan:
    port: int
    quit_frame: int
    screenshot_frames: tuple[int, ...]
    response_timeout_seconds: float = 1.0


@dataclass(frozen=True, slots=True)
class ControlOutcome:
    reached_frame: int
    screenshot_frames: tuple[int, ...]
    quit_requested: bool


@dataclass(frozen=True, slots=True)
class ControlMonitorError(RuntimeError):
    reason: str

    def __str__(self) -> str:
        return self.reason


def _status(value: object) -> tuple[int, bool]:
    if not isinstance(value, Mapping):
        raise ControlMonitorError("control status was not an object")
    frame = value.get("frame")
    in_game = value.get("inGame")
    if not isinstance(frame, int) or not isinstance(in_game, bool):
        raise ControlMonitorError("control status omitted frame or inGame")
    return frame, in_game


def response_timeout_seconds(plan: ControlPlan, deadline: float, now: float) -> float:
    if plan.response_timeout_seconds <= 0:
        raise ControlMonitorError("control response timeout must be positive")
    return min(plan.response_timeout_seconds, max(0.1, deadline - now))


def _set_response_timeout(game: Control, plan: ControlPlan, deadline: float) -> None:
    connection = game.socket
    if connection is None:
        raise ControlMonitorError("control socket closed before response")
    connection.settimeout(response_timeout_seconds(plan, deadline, time.monotonic()))


def monitor_until_evidence(
    process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float
) -> ControlOutcome:
    _ = response_timeout_seconds(plan, deadline, time.monotonic())
    game: Control | None = None
    while game is None and time.monotonic() < deadline:
        if process.poll() is not None:
            raise ControlMonitorError(f"process exited before control connected: {process.returncode}")
        try:
            game = Control(plan.port, timeout=min(1.0, max(0.1, deadline - time.monotonic())))
        except (TimeoutError, ConnectionError, OSError, ControlError):
            time.sleep(0.02)
    if game is None:
        raise ControlMonitorError("control socket did not become ready before the deadline")

    pending = sorted(plan.screenshot_frames)
    requested: list[int] = []
    reached = 0
    saw_match = False
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise ControlMonitorError(f"process exited before evidence frame: {process.returncode}")
            _set_response_timeout(game, plan, deadline)
            reached, in_game = _status(game.status())
            saw_match = saw_match or in_game
            if saw_match and not in_game:
                raise ControlMonitorError(f"match ended before evidence frame {plan.quit_frame}")
            while pending and reached >= pending[0]:
                if reached != pending[0]:
                    raise ControlMonitorError(
                        f"control status skipped screenshot frame {pending[0]} and reached {reached}"
                    )
                response = game.screenshot()
                if not isinstance(response, Mapping) or response.get("ok") is not True:
                    raise ControlMonitorError(f"screenshot request failed at frame {reached}")
                pending.pop(0)
                requested.append(reached)
            if reached >= plan.quit_frame:
                _set_response_timeout(game, plan, deadline)
                response = game.send("quit")
                if not isinstance(response, Mapping) or response.get("quitting") is not True:
                    raise ControlMonitorError(f"quit request failed at frame {reached}")
                return ControlOutcome(reached, tuple(requested), True)
            time.sleep(0.01)
    except (TimeoutError, ConnectionError, OSError, ControlError) as error:
        raise ControlMonitorError(f"control response failed: {error}") from error
    finally:
        game.close()
    raise ControlMonitorError(f"control deadline expired at frame {reached}")
