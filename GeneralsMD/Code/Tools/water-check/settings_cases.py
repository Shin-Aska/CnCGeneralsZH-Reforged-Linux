"""Private default cases and menu observations, separate from native acceptance."""

from __future__ import annotations

import re
import uuid
from collections.abc import Callable, Mapping
from contextlib import AbstractContextManager
from dataclasses import dataclass, replace
from enum import StrEnum
from pathlib import Path
from typing import Literal, assert_never, final

from menu_control import MenuAction, MenuDriver
from receipt_evidence import EvidenceExpectation, verify_evidence
from runtime import inherited_overrides
from runtime_receipt import (
    History,
    ReceiptError,
    RuntimeReceipt,
)


@dataclass(frozen=True, slots=True)
class SettingsCase:
    name: str
    initial_options: bytes | None
    requested_style: Literal[0, 1]


def default_cases() -> tuple[SettingsCase, ...]:
    return (
        SettingsCase("fresh", None, 1),
        SettingsCase("legacy", b"Resolution = 1280 720\r\nUnrelated = keep\r\n", 1),
        SettingsCase("classic", b"WaterStyle = 0\r\nUnrelated = keep\r\n", 0),
        SettingsCase("modern", b"WaterStyle = 1\r\nUnrelated = keep\r\n", 1),
    )


def stage_case(root: Path, case: SettingsCase) -> Path:
    if re.fullmatch(r"[A-Za-z0-9_-]{1,80}", case.name) is None:
        raise ReceiptError("private settings case name is not a safe path token")
    user = root / (case.name + "-" + uuid.uuid4().hex)
    user.mkdir(parents=False, exist_ok=False)
    if case.initial_options is not None:
        _ = (user / "Options.ini").write_bytes(case.initial_options)
    return user


def require_ordinary_environment(environment: Mapping[str, str]) -> None:
    overrides = inherited_overrides(environment)
    if overrides:
        raise ReceiptError("ordinary defaults reject inherited overrides: " + ", ".join(overrides))


@dataclass(frozen=True, slots=True)
class SettingsSnapshot:
    options: bytes | None
    hotkeys: bytes | None
    requested_style: int
    effective_style: int
    history: History
    logic_frame: int
    temporaries: tuple[str, ...]


def snapshot(user: Path, receipt: RuntimeReceipt) -> SettingsSnapshot:
    options = user / "Options.ini"
    hotkeys = user / "Hotkeys.ini"
    return SettingsSnapshot(
        options.read_bytes() if options.is_file() else None,
        hotkeys.read_bytes() if hotkeys.is_file() else None,
        receipt.requested_style,
        receipt.effective_style,
        receipt.history,
        receipt.clock.logic_frame,
        tuple(sorted(path.name for path in user.glob("Options.ini.tmp.*"))),
    )


def require_preserved(before: SettingsSnapshot, after: SettingsSnapshot) -> None:
    if before != after:
        raise ReceiptError("cancel/failed Accept changed file, water, history, hotkeys or paused frame")


class FailureResolution(StrEnum):
    RETRY = "retry"
    BACK = "back"
    ESC = "esc"


@final
class SettingsSession:
    """One checked private launch; actions are real input and reads are observations."""

    def __init__(self, driver: MenuDriver, user: Path, expectation: EvidenceExpectation) -> None:
        self.driver = driver
        self.user = user
        self.expectation = expectation
        self.production_required = expectation.production
        self.images: list[tuple[str, str]] = []
        self.receipts: list[RuntimeReceipt] = []

    def checkpoint(self, style: Literal[0, 1]) -> RuntimeReceipt:
        request_time = self.expectation.capture_request_ns()
        receipt = self.driver.capture(self.expectation.previous_capture)
        effective = style if self.expectation.backend == "NativeD3D11" and self.production_required else 0
        expected = replace(
            self.expectation,
            requested_style=style,
            effective_style=effective,
            production=self.production_required and bool(effective),
            earliest_image_ns=max(request_time, self.expectation.earliest_image_ns),
        )
        image = verify_evidence(receipt, expected)
        self.images.append((receipt.capture.path, image.sha256))
        self.receipts.append(receipt)
        self.expectation = replace(
            expected, previous_sequence=receipt.sequence, previous_capture=receipt.capture.sequence
        )
        return receipt

    def select(self, style: Literal[0, 1]) -> RuntimeReceipt:
        for action in (MenuAction.OPEN, MenuAction.GRAPHICS, MenuAction.MODERN if style else MenuAction.CLASSIC):
            receipt = self.driver.act(action)
        if not receipt.menu.options_open or not receipt.menu.graphics_visible or receipt.menu.selection != style:
            raise ReceiptError("real menu did not show the requested pending selection")
        observed = self.checkpoint(self.expectation.requested_style)
        if not observed.menu.options_open or observed.menu.selection != style:
            raise ReceiptError("native pending-choice screenshot no longer matches its menu selection")
        return observed

    def require_saved(self, style: Literal[0, 1]) -> RuntimeReceipt:
        receipt = self.checkpoint(style)
        values = re.findall(rb"(?mi)^\s*WaterStyle\s*=\s*([01])\s*$", (self.user / "Options.ini").read_bytes())
        if values != [str(style).encode("ascii")] or receipt.menu.options_open or receipt.menu.error_visible:
            raise ReceiptError("successful real Accept did not persist and close the options menu")
        return receipt

    def run_semantics(self) -> None:
        """Back, Esc, Accept, same selection and four paused toggles on one launch."""
        initial = self.expectation.requested_style
        opposite: Literal[0, 1] = 0 if initial else 1
        receipt = self.checkpoint(initial)
        if not receipt.clock.frozen:
            raise ReceiptError("menu toggle scenario requires an actually paused water clock")
        before = snapshot(self.user, receipt)
        for cancel in (MenuAction.BACK, MenuAction.ESC):
            _ = self.select(opposite)
            _ = self.driver.act(cancel)
            after = self.checkpoint(initial)
            require_preserved(before, snapshot(self.user, after))
            if after.menu.options_open:
                raise ReceiptError("cancel left the options menu open")
        _ = self.select(opposite)
        _ = self.driver.act(MenuAction.ACCEPT)
        accepted = self.require_saved(opposite)
        history = accepted.history
        _ = self.select(opposite)
        _ = self.driver.act(MenuAction.ACCEPT)
        same = self.require_saved(opposite)
        if same.history != history:
            raise ReceiptError("same-selection Accept reset water history")
        for style in (initial, opposite, initial, opposite):
            _ = self.select(style)
            _ = self.driver.act(MenuAction.ACCEPT)
            toggled = self.require_saved(style)
            if toggled.clock.logic_frame != before.logic_frame or toggled.history.fft_time != before.history.fft_time:
                raise ReceiptError("paused style toggle advanced gameplay or FFT time")

    def run_failed_accept(self, fault: AbstractContextManager[None], resolution: FailureResolution) -> None:
        """The caller supplies a real isolated filesystem fault or diagnostic boundary."""
        initial = self.expectation.requested_style
        target: Literal[0, 1] = 0 if initial else 1
        before = snapshot(self.user, self.checkpoint(initial))
        _ = self.select(target)
        with fault:
            _ = self.driver.act(MenuAction.ACCEPT)
            failed = self.checkpoint(initial)
        require_preserved(before, snapshot(self.user, failed))
        menu = failed.menu
        if not (menu.options_open and menu.pending and menu.error_visible and menu.selection == target):
            raise ReceiptError("failed Accept did not retain the pending selection, error and menu")
        match resolution:
            case FailureResolution.RETRY:
                _ = self.driver.act(MenuAction.ACCEPT)
                _ = self.require_saved(target)
            case FailureResolution.BACK:
                _ = self.driver.act(MenuAction.BACK)
                require_preserved(before, snapshot(self.user, self.checkpoint(initial)))
            case FailureResolution.ESC:
                _ = self.driver.act(MenuAction.ESC)
                require_preserved(before, snapshot(self.user, self.checkpoint(initial)))
            case unreachable:
                assert_never(unreachable)

    def require_relaunch(self, launch: Callable[[Path], SettingsSession]) -> None:
        """The external isolated launcher must reap the old process and bind a fresh one."""
        reopened = launch(self.user)
        if reopened.expectation.launch.pid == self.expectation.launch.pid:
            raise ReceiptError("relaunch reused the old native process identity")
        _ = reopened.checkpoint(self.expectation.requested_style)

    def run_unavailable_accept(self) -> None:
        initial = self.expectation.requested_style
        before = self.checkpoint(initial)
        for action in (MenuAction.OPEN, MenuAction.GRAPHICS):
            _ = self.driver.act(action)
        menu = self.checkpoint(initial).menu
        if menu.water_style_enabled:
            raise ReceiptError("unavailable backend exposed an enabled water-style control")
        _ = self.driver.act(MenuAction.ACCEPT)
        after = self.require_saved(initial)
        if after.history != before.history or after.effective_style != 0:
            raise ReceiptError("unrelated unavailable Accept changed water history or fallback")
