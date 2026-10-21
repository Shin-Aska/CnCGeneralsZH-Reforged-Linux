"""Real key/click menu driver; calibration must come from reviewed native images."""

from __future__ import annotations

import hashlib
import json
import struct
from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import ClassVar, Final, Literal, Protocol, TypeAlias, assert_never, final

from pydantic import BaseModel, ConfigDict, ValidationError
from pydantic.alias_generators import to_camel
from runtime_receipt import ReceiptError, RuntimeReceipt, parse_receipt

JsonValue: TypeAlias = str | int | float | bool | list["JsonValue"] | dict[str, "JsonValue"] | None
BMP_HEADER_BYTES: Final = 54


class ControlPort(Protocol):
    def send(self, command: str) -> JsonValue: ...


class Ack(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(strict=True, extra="ignore")
    ok: bool


class CalibrationReview(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(strict=True, extra="forbid", alias_generator=to_camel)
    approved: bool
    screenshot_sha256: str
    width: int
    height: int
    transform: Literal["fit", "stretch"]
    points: tuple[tuple[int, int], ...]


@dataclass(frozen=True, slots=True)
class Point:
    x: int
    y: int


@dataclass(frozen=True, slots=True)
class VerifiedLayout:
    screenshot: Path
    screenshot_sha256: str
    review: Path
    review_sha256: str
    width: int
    height: int
    transform: Literal["fit", "stretch"]
    quit_options: Point
    graphics: Point
    combo: Point
    classic_row: Point
    modern_row: Point
    accept: Point
    back: Point

    def verify(self) -> None:
        """Verify the evidence binding, not the human visual judgment itself."""
        for path, digest in ((self.screenshot, self.screenshot_sha256), (self.review, self.review_sha256)):
            if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ReceiptError("menu calibration evidence changed")
        image = self.screenshot.read_bytes()
        if len(image) < BMP_HEADER_BYTES or image[:2] != b"BM":
            raise ReceiptError("menu calibration lacks an actual BMP")
        width, height = struct.unpack_from("<ii", image, 18)
        if (width, abs(height)) != (self.width, self.height):
            raise ReceiptError("menu calibration dimensions differ")
        try:
            review = CalibrationReview.model_validate_json(self.review.read_bytes())
        except ValidationError as error:
            raise ReceiptError("menu calibration review is malformed") from error
        if not review.approved or (review.screenshot_sha256, review.width, review.height, review.transform) != (
            self.screenshot_sha256,
            self.width,
            self.height,
            self.transform,
        ):
            raise ReceiptError("menu calibration lacks matching visual approval")
        points = (
            self.quit_options,
            self.graphics,
            self.combo,
            self.classic_row,
            self.modern_row,
            self.accept,
            self.back,
        )
        if review.points != tuple((p.x, p.y) for p in points):
            raise ReceiptError("menu input coordinates differ from their visual review")
        if any(p.x < 0 or p.y < 0 or p.x >= width or p.y >= abs(height) for p in points):
            raise ReceiptError("menu calibration point lies outside the native image")


class MenuAction(StrEnum):
    OPEN = "open"
    GRAPHICS = "graphics"
    CLASSIC = "classic"
    MODERN = "modern"
    ACCEPT = "accept"
    BACK = "back"
    ESC = "esc"


@final
class MenuDriver:
    """Mutable command journal for one live Control connection and verified layout."""

    def __init__(self, control: ControlPort, layout: VerifiedLayout, wait: Callable[[], None]) -> None:
        layout.verify()
        self.control = control
        self.layout = layout
        self.wait = wait
        self.commands: list[str] = []
        self.last_sequence = 0

    def status(self) -> RuntimeReceipt:
        receipt = parse_receipt(json.dumps(self.control.send("status")))
        if receipt.sequence <= self.last_sequence:
            raise ReceiptError("menu status sequence is stale")
        self.last_sequence = receipt.sequence
        dimensions = (receipt.capture.width, receipt.capture.height)
        if receipt.view is not None:
            dimensions = (receipt.view.width, receipt.view.height)
        if dimensions != (self.layout.width, self.layout.height):
            raise ReceiptError("live dimensions differ from the reviewed menu calibration")
        return receipt

    def _send(self, command: str) -> None:
        try:
            ack = Ack.model_validate_json(json.dumps(self.control.send(command)))
        except ValidationError as error:
            raise ReceiptError(f"menu control request failed: {command}") from error
        if not ack.ok:
            raise ReceiptError(f"menu control request was refused: {command}")
        self.commands.append(command)
        self.wait()

    def _click(self, point: Point) -> None:
        self._send(f"click {point.x} {point.y}")

    def act(self, action: MenuAction) -> RuntimeReceipt:
        """Use only existing raw input routes, never an option setter."""
        _ = self.status()
        match action:
            case MenuAction.OPEN:
                self._send("key KEY_ESC")
                self._click(self.layout.quit_options)
            case MenuAction.GRAPHICS:
                self._click(self.layout.graphics)
            case MenuAction.CLASSIC:
                self._click(self.layout.combo)
                self._click(self.layout.classic_row)
            case MenuAction.MODERN:
                self._click(self.layout.combo)
                self._click(self.layout.modern_row)
            case MenuAction.ACCEPT:
                self._click(self.layout.accept)
            case MenuAction.BACK:
                self._click(self.layout.back)
            case MenuAction.ESC:
                self._send("key KEY_ESC")
            case unreachable:
                assert_never(unreachable)
        return self.status()

    def capture(self, previous: int) -> RuntimeReceipt:
        """A requested reply alone cannot satisfy actual image completion."""
        self._send("screenshot")
        for _ in range(100):
            receipt = self.status()
            if receipt.capture.sequence > previous and receipt.capture.succeeded and receipt.capture.native:
                return receipt
            self.wait()
        raise ReceiptError("native image completion event did not arrive")
