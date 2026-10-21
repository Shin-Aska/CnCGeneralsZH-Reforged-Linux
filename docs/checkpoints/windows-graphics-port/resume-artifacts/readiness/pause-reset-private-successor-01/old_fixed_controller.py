from __future__ import annotations

import json
import subprocess
import time
from collections.abc import Callable, Mapping
from pathlib import Path

from control_client import Control, ControlError
from control_monitor import ControlMonitorError, ControlOutcome, ControlPlan, _set_response_timeout, _status, response_timeout_seconds


def make_pause_reset_monitor(staged_map: Path, events_path: Path) -> Callable[[subprocess.Popen[bytes], ControlPlan, float], ControlOutcome]:
    """Bind one fixed landed-menu/reset HELPER flow; not a generic command language or Game proof."""
    if not staged_map.is_absolute() or not staged_map.is_file():
        raise ControlMonitorError("fixed monitor requires an existing absolute whole-map path")
    if events_path.exists():
        raise ControlMonitorError("fixed monitor event output already exists")

    def monitor(process: subprocess.Popen[bytes], plan: ControlPlan, deadline: float) -> ControlOutcome:
        """Use one connection for pause plateau, resume, normal leave/start and quit."""
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

        def check_live_process() -> None:
            if time.monotonic() >= deadline:
                raise ControlMonitorError("fixed controller deadline expired")
            if process.poll() is not None:
                raise ControlMonitorError(f"process exited during fixed controller: {process.returncode}")

        try:
            with events_path.open("x", encoding="utf-8") as record:
                def request(payload: str, field: str) -> None:
                    check_live_process()
                    _set_response_timeout(game, plan, deadline)
                    reply = game.send(payload)
                    record.write(json.dumps({"wall": time.monotonic(), "command": payload, "reply": reply}) + "\n")
                    record.flush()
                    match reply:
                        case Mapping() if reply.get("ok") is True and reply.get(field) is True:
                            return
                        case _:
                            raise ControlMonitorError(f"fixed request refused: {payload}")

                def status() -> tuple[int, bool]:
                    check_live_process()
                    _set_response_timeout(game, plan, deadline)
                    reply = game.status()
                    reached, in_game = _status(reply)
                    record.write(json.dumps({"wall": time.monotonic(), "command": "status", "reply": reply}) + "\n")
                    record.flush()
                    return reached, in_game

                first, live = status()
                if not live:
                    raise ControlMonitorError("fixed monitor needs a live landed match")
                request("key KEY_ESC", "pressed")
                paused, live = status()
                if not live:
                    raise ControlMonitorError("match ended before pause sample")
                plateau_started = time.monotonic()
                while time.monotonic() - plateau_started < 0.25:
                    time.sleep(0.01)
                    reached, live = status()
                    if not live or reached != paused:
                        raise ControlMonitorError("pause plateau did not hold")
                request("key KEY_ESC", "pressed")
                reached, live = status()
                while live and reached <= paused:
                    time.sleep(0.01)
                    reached, live = status()
                if not live:
                    raise ControlMonitorError("match ended before resume")
                request("leave", "leaving")
                reached, live = status()
                while live:
                    time.sleep(0.01)
                    reached, live = status()
                request("skirmish 2 1 " + str(staged_map), "starting")
                reached, live = status()
                while not live:
                    time.sleep(0.01)
                    reached, live = status()
                # This fixed helper demonstrates match-boundary transport, not real registry identity.
                record.write(json.dumps({"wall": time.monotonic(), "initialLogic": first, "newMatchLogic": reached,
                    "scope": "HELPER only; no registry/menu/image/native assertion"}) + "\n")
                record.flush()
                request("quit", "quitting")
                return ControlOutcome(reached, (), True)
        except (TimeoutError, ConnectionError, OSError, ControlError) as error:
            raise ControlMonitorError(f"control response failed: {error}") from error
        finally:
            game.close()

    return monitor
