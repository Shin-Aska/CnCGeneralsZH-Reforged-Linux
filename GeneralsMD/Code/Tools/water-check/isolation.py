from __future__ import annotations

import json
import logging
import os
import signal
import subprocess
import time
from collections.abc import Callable, Mapping, Sequence
from dataclasses import asdict
from pathlib import Path

from control_monitor import (
    ControlMonitorError,
    ControlOutcome,
    ControlPlan,
    monitor_until_evidence,
)
from models import LaunchResult
from runtime import clean_child_environment


def prepare_output(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=False)


def _write_diagnostic(path: Path, payload: str) -> None:
    """Keep diagnostic I/O failures from changing the process/control result."""
    try:
        path.write_text(payload, encoding="utf-8")
    except OSError as error:
        logging.getLogger(__name__).warning("Could not persist %s: %s", path.name, error)


def launch_isolated(
    command: Sequence[str],
    output: Path,
    timeout_seconds: float,
    environment: Mapping[str, str],
    working_directory: Path | None = None,
    control_plan: ControlPlan | None = None,
    *,
    monitor: Callable[[subprocess.Popen[bytes], ControlPlan, float], ControlOutcome] = monitor_until_evidence,
) -> LaunchResult:
    started = time.monotonic()
    child_environment = clean_child_environment(environment)
    resources = output / "resources.log"
    with (output / "run.log").open("w", encoding="utf-8") as log:
        creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
        process = subprocess.Popen(
            list(command),
            cwd=working_directory or output,
            env=child_environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=creation_flags,
            start_new_session=os.name != "nt",
        )
        with resources.open("a", encoding="utf-8") as record:
            record.write(f"RESOURCE_CREATED pid={process.pid} output={output}\n")
        timed_out = False
        control = ControlOutcome(0, (), False)
        control_error: str | None = None
        try:
            deadline = started + timeout_seconds
            if control_plan is not None:
                monitor_started = time.monotonic()
                try:
                    control = monitor(process, control_plan, deadline)
                except ControlMonitorError as error:
                    control_error = str(error)
                _write_diagnostic(output / "control-monitor.json", json.dumps({
                    "phase": "monitor-failed" if control_error is not None else "monitor-completed",
                    "elapsed_seconds": time.monotonic() - monitor_started,
                    "outcome_returned": control_error is None,
                    "outcome_scope": "returned" if control_error is None else "isolation-default-no-outcome-returned",
                    "outcome": asdict(control),
                    "error": control_error,
                }, indent=2))
            returncode = process.wait(timeout=max(0.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            timed_out = True
            if os.name == "nt":
                subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    check=False,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            else:
                os.killpg(process.pid, signal.SIGKILL)
            try:
                returncode = process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                polled = process.poll()
                if polled is None:
                    process.kill()
                    returncode = process.wait(timeout=10)
                else:
                    returncode = polled
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            with resources.open("a", encoding="utf-8") as record:
                record.write(f"RESOURCE_CLEANED pid={process.pid} returncode={process.returncode}\n")
    result = LaunchResult(
        process.pid, returncode, timed_out, time.monotonic() - started,
        control.reached_frame, control.screenshot_frames, control.quit_requested, control_error,
    )
    _write_diagnostic(output / "launch-result.json", json.dumps(asdict(result), indent=2))
    return result
