"""CPU state-machine tests only; no game, native UI or visual approval is faked."""

from __future__ import annotations

import hashlib
import json
from collections.abc import Generator
from contextlib import contextmanager
from dataclasses import replace
from enum import StrEnum
from pathlib import Path
from typing import Literal, assert_never, final

import pytest
from menu_control import JsonValue, MenuAction, MenuDriver, Point, VerifiedLayout
from receipt_evidence import ClockMode, verify_evidence
from runtime_receipt import ReceiptError, parse_receipt
from selftest_settings import bitmap, expectation, mutate, native_status
from settings_cases import FailureResolution, SettingsCase, SettingsSession, default_cases, stage_case


def calibration(root: Path) -> VerifiedLayout:
    image = root / "native.bmp"
    bitmap(image)
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    review = root / "review.json"
    _ = review.write_text(
        json.dumps(
            {
                "approved": True,
                "screenshotSha256": digest,
                "width": 8,
                "height": 8,
                "transform": "fit",
                "points": [[1, 1], [2, 1], [3, 1], [3, 2], [3, 3], [4, 1], [5, 1]],
            }
        ),
        encoding="utf-8",
    )
    return VerifiedLayout(
        image,
        digest,
        review,
        hashlib.sha256(review.read_bytes()).hexdigest(),
        8,
        8,
        "fit",
        Point(1, 1),
        Point(2, 1),
        Point(3, 1),
        Point(3, 2),
        Point(3, 3),
        Point(4, 1),
        Point(5, 1),
    )


class UnitCommand(StrEnum):
    STATUS = "status"
    ESC = "key KEY_ESC"
    BACK = "click 5 1"
    OPEN = "click 1 1"
    GRAPHICS = "click 2 1"
    COMBO = "click 3 1"
    CLASSIC = "click 3 2"
    MODERN = "click 3 3"
    ACCEPT = "click 4 1"
    SCREENSHOT = "screenshot"


@final
class PythonMenuStateMachine:
    """Mutable in-memory input consumer; cannot attest any native observation."""

    def __init__(self, path: Path, user: Path | None = None) -> None:
        self.body = native_status(path)
        initial = self.body["waterValidation"]
        assert isinstance(initial, dict)
        self.native_view = initial["view"]
        self.commands: list[str] = []
        self.open = False
        self.graphics = False
        self.selection: int | None = None
        self.style = 1
        self.available = True
        self.sequence = 1
        self.error = False
        self.fail_save = False
        self.complete_images = True
        self.stale = False
        self.capture_sequence = 1
        self.user = user

    def _status(self) -> JsonValue:
        self.sequence += 0 if self.stale else 1
        modern = bool(self.style) and self.available
        body = self.body["waterValidation"]
        assert isinstance(body, dict)
        body["sequence"] = self.sequence
        body["requestedStyle"] = self.style
        body["effectiveStyle"] = self.style if self.available else 0
        body["view"] = self.native_view if modern else None
        body["emission"] = {
            "frameId": 7 if modern else 0,
            "features": 65535 if modern else 0,
            "draws": 3 if modern else 0,
            "refusals": 0,
            "refusalReason": "None",
        }
        body["menu"] = {
            "optionsOpen": self.open,
            "graphicsVisible": self.graphics,
            "selection": self.selection,
            "pending": self.selection != self.style and self.open,
            "errorVisible": self.error,
            "waterStyleEnabled": self.available,
        }
        capture = body["capture"]
        assert isinstance(capture, dict)
        capture["sequence"] = self.capture_sequence
        capture["waterFrameId"] = 7 if modern else 0
        return self.body

    def _accept(self) -> None:
        if self.fail_save:
            self.error = True
        else:
            assert self.selection is not None
            self.style = self.selection
            if self.user is not None:
                _ = (self.user / "Options.ini").write_text(f"WaterStyle = {self.style}\n", encoding="utf-8")
            self.open = False
            self.graphics = False
            self.selection = None
            self.error = False

    def send(self, command: str) -> JsonValue:
        self.commands.append(command)
        parsed = UnitCommand(command)
        match parsed:
            case UnitCommand.STATUS:
                return self._status()
            case UnitCommand.ESC | UnitCommand.BACK:
                self.open = False
                self.graphics = False
                self.selection = None
                self.error = False
            case UnitCommand.OPEN:
                self.open = True
                self.selection = self.style
            case UnitCommand.GRAPHICS:
                self.graphics = True
            case UnitCommand.COMBO:
                pass
            case UnitCommand.CLASSIC:
                self.selection = 0
            case UnitCommand.MODERN:
                self.selection = 1
            case UnitCommand.ACCEPT:
                self._accept()
            case UnitCommand.SCREENSHOT:
                self.capture_sequence += int(self.complete_images)
            case unreachable:
                assert_never(unreachable)
        return {"ok": True}


def settings_fixture(root: Path, case: SettingsCase) -> tuple[SettingsSession, PythonMenuStateMachine]:
    layout = calibration(root)
    user = stage_case(root, case)
    control = PythonMenuStateMachine(layout.screenshot, user)
    return SettingsSession(MenuDriver(control, layout, lambda: None), user, expectation(root)), control


def test_driver_when_menu_actions_use_real_input_routes(tmp_path: Path) -> None:
    # Given: a typed Python state machine and a bounded calibration fixture.
    layout = calibration(tmp_path)
    control = PythonMenuStateMachine(layout.screenshot)
    driver = MenuDriver(control, layout, lambda: None)
    # When: the driver opens the menu and selects Classic through the dropdown.
    for action in (MenuAction.OPEN, MenuAction.GRAPHICS, MenuAction.CLASSIC, MenuAction.ACCEPT):
        receipt = driver.act(action)
    # Then: only raw input requests committed the selection.
    assert receipt.requested_style == 0
    assert driver.commands == ["key KEY_ESC", "click 1 1", "click 2 1", "click 3 1", "click 3 2", "click 4 1"]


def test_driver_when_review_rejects_calibration(tmp_path: Path) -> None:
    # Given: native-looking image bytes with an explicitly rejected review.
    layout = calibration(tmp_path)
    _ = layout.review.write_text('{"approved":false}', encoding="utf-8")
    rejected = replace(layout, review_sha256=hashlib.sha256(layout.review.read_bytes()).hexdigest())
    # When/Then: no input is allowed from unapproved calibration.
    with pytest.raises(ReceiptError):
        _ = MenuDriver(PythonMenuStateMachine(layout.screenshot), rejected, lambda: None)


def test_driver_when_coordinates_change_after_review(tmp_path: Path) -> None:
    # Given: reviewed image dimensions with an unreviewed input location.
    layout = replace(calibration(tmp_path), combo=Point(6, 6))
    # When/Then: changed coordinates cannot inherit the old review approval.
    with pytest.raises(ReceiptError):
        _ = MenuDriver(PythonMenuStateMachine(layout.screenshot), layout, lambda: None)


def test_driver_when_status_sequence_is_stale(tmp_path: Path) -> None:
    # Given: the control consumer replays the same status response.
    layout = calibration(tmp_path)
    control = PythonMenuStateMachine(layout.screenshot)
    driver = MenuDriver(control, layout, lambda: None)
    _ = driver.status()
    control.stale = True
    # When/Then: the stale response fails before another click.
    with pytest.raises(ReceiptError):
        _ = driver.act(MenuAction.OPEN)
    assert driver.commands == []


def test_capture_when_only_requested_reply_arrives(tmp_path: Path) -> None:
    # Given: screenshot returns ok while its actual completion sequence stays unchanged.
    layout = calibration(tmp_path)
    control = PythonMenuStateMachine(layout.screenshot)
    control.complete_images = False
    driver = MenuDriver(control, layout, lambda: None)
    # When/Then: the request cannot pass as a completed image.
    with pytest.raises(ReceiptError):
        _ = driver.capture(1)


def test_settings_when_real_input_sequence_covers_paused_semantics(tmp_path: Path) -> None:
    # Given: CPU-only preferences and the input-consuming Python state machine.
    session, control = settings_fixture(tmp_path, default_cases()[3])
    # When: the complete cancel/accept/same-selection/paused-toggle driver runs.
    session.run_semantics()
    # Then: the driver reached Classic through input, with actual small image checks.
    assert control.style == 0
    assert len(session.images) == 17


@contextmanager
def unit_save_fault(control: PythonMenuStateMachine) -> Generator[None]:
    control.fail_save = True
    try:
        yield
    finally:
        control.fail_save = False


@pytest.mark.parametrize("resolution", tuple(FailureResolution))
@pytest.mark.parametrize("existing", [False, True])
def test_settings_when_failed_accept_is_retried_or_cancelled(
    tmp_path: Path,
    resolution: FailureResolution,
    existing: bool,
) -> None:
    # Given: an existing or absent destination and a CPU-only save fault scope.
    session, control = settings_fixture(tmp_path, default_cases()[3 if existing else 0])
    # When: real-menu request ordering encounters failed Accept and resolution.
    session.run_failed_accept(unit_save_fault(control), resolution)
    # Then: fault lifetime is restored and only successful retry commits.
    assert not control.fail_save
    assert control.style == (0 if resolution is FailureResolution.RETRY else 1)


@pytest.mark.parametrize("same_camera", [True, False])
def test_matched_view_when_pointer_changes_or_camera_differs(tmp_path: Path, same_camera: bool) -> None:
    # Given: a view reference from the independent fixture input.
    layout = calibration(tmp_path)
    status = native_status(layout.screenshot)
    reference = parse_receipt(json.dumps(status)).view
    mutate(status, "clock.captureEnabled", True)
    expected = replace(
        expectation(tmp_path), clock_mode=ClockMode.MATCHED, picture_seconds=4.0, expected_view=reference
    )
    # When: either a local pointer identity or the actual camera matrix changes.
    if same_camera:
        mutate(status, "view.viewId", 900)
        _ = verify_evidence(parse_receipt(json.dumps(status)), expected)
    else:
        changed_matrix: list[JsonValue] = [0.0] * 16
        mutate(status, "view.viewMatrix", changed_matrix)
        # Then: changed camera semantics fail while a different local identity can match.
        with pytest.raises(ReceiptError):
            _ = verify_evidence(parse_receipt(json.dumps(status)), expected)


@pytest.mark.parametrize("backend", ["NativeD3D11", "D3D9"])
def test_unavailable_accept_when_core_or_backend_is_unavailable(
    tmp_path: Path, backend: Literal["NativeD3D11", "D3D9"]
) -> None:
    # Given: an independently expected unavailable native core or D3D9 backend.
    session, control = settings_fixture(tmp_path, default_cases()[3])
    control.available = False
    reason = {"NativeD3D11": "CoreShaderUnavailable", "D3D9": "UnsupportedBackend"}[backend]
    capability: JsonValue = {"backend": backend, "features": 0, "reason": reason}
    mutate(control.body, "capability", capability)
    session.expectation = replace(session.expectation, backend=backend, effective_style=0, production=False)
    session.production_required = False
    # When: unrelated Accept is driven through the disabled real-menu contract.
    session.run_unavailable_accept()
    # Then: the requested Modern preference survives with disabled-UI image evidence.
    assert control.style == 1
    assert len(session.images) == 3
