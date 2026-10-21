"""Evidence binding for one independently checked native game launch."""

from __future__ import annotations

import time
from collections.abc import Callable
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final, Literal, assert_never

from bitmap import inspect_bmp
from models import ExpectedImage, ImageObservation
from runtime_receipt import (
    NativeAdapter,
    NativeClock,
    NativeView,
    ReceiptError,
    ReceiptUnavailableError,
    RuntimeReceipt,
)

PRODUCTION_REQUIRED: Final = 65535
DIGEST_HEX_LENGTH: Final = 64
CLOCK_TOLERANCE: Final = 1e-6
SHADER_MODEL_5_FEATURE_LEVEL: Final = 0xB000


class ClockMode(StrEnum):
    ORDINARY = "ordinary"
    MATCHED = "matched"
    BASELINE = "baseline"


@dataclass(frozen=True, slots=True)
class LaunchIdentity:
    pid: int
    process_path: Path
    executable_sha256: str
    data_sha256: str
    source_sha256: str


@dataclass(frozen=True, slots=True)
class EvidenceExpectation:
    launch: LaunchIdentity
    previous_sequence: int
    previous_capture: int
    requested_style: Literal[0, 1]
    effective_style: Literal[0, 1]
    backend: Literal["D3D9", "NativeD3D11"]
    production: bool
    clock_mode: ClockMode
    picture_seconds: float | None
    output_root: Path
    earliest_image_ns: int
    image: ExpectedImage
    actual_adapter: NativeAdapter | None = None
    expected_view: NativeView | None = None
    capture_request_ns: Callable[[], int] = time.time_ns


def _verify_clock(clock: NativeClock, expected: EvidenceExpectation) -> None:
    match expected.clock_mode:
        case ClockMode.ORDINARY:
            if clock.capture_enabled is not False:
                raise ReceiptError("ordinary defaults did not attest the capture clock is off")
        case ClockMode.MATCHED:
            if clock.capture_enabled is not True or expected.picture_seconds is None:
                raise ReceiptError("matched capture clock is unverified")
            if (
                max(
                    abs(clock.picture_seconds - expected.picture_seconds),
                    abs(clock.water_seconds - expected.picture_seconds),
                )
                > CLOCK_TOLERANCE
            ):
                raise ReceiptError("matched picture/water time differs")
        case ClockMode.BASELINE:
            pass
        case unreachable:
            assert_never(unreachable)


def _verify_adapter(receipt: RuntimeReceipt, expected: EvidenceExpectation) -> None:
    adapter = receipt.adapter if receipt.adapter is not None else expected.actual_adapter
    if adapter is None or not adapter.driver_version:
        raise ReceiptUnavailableError("actual launched device/driver observation is unavailable")
    if (
        receipt.adapter is not None
        and expected.actual_adapter is not None
        and receipt.adapter != expected.actual_adapter
    ):
        raise ReceiptError("native device differs from its bound startup observation")
    if adapter.software or adapter.feature_level < SHADER_MODEL_5_FEATURE_LEVEL:
        raise ReceiptError("production requires an actual hardware Shader Model 5 device")
    if receipt.view is None or adapter.device_generation != receipt.view.device_generation:
        raise ReceiptError("actual device observation is stale for the current water view")


def _verify_launch(receipt: RuntimeReceipt, expected: EvidenceExpectation) -> None:
    identity = expected.launch
    if identity.pid <= 0 or not identity.process_path.is_absolute():
        raise ReceiptError("launch identity was not bound to an actual process")
    for digest in (identity.executable_sha256, identity.data_sha256, identity.source_sha256):
        if len(digest) != DIGEST_HEX_LENGTH or any(c not in "0123456789abcdef" for c in digest):
            raise ReceiptError("launch identity has an invalid digest")
    if receipt.pid != identity.pid or receipt.sequence <= expected.previous_sequence:
        raise ReceiptError("runtime process or receipt sequence is stale")


def _verify_view(receipt: RuntimeReceipt, expected: EvidenceExpectation) -> None:
    reference = expected.expected_view
    if reference is None:
        match expected.clock_mode:
            case ClockMode.MATCHED:
                raise ReceiptUnavailableError("matched camera/projection/viewport reference is unavailable")
            case ClockMode.ORDINARY | ClockMode.BASELINE:
                return
            case unreachable:
                assert_never(unreachable)
    view = receipt.view
    if view is None:
        raise ReceiptError("expected native view is absent")
    if (view.width, view.height, view.samples, view.colour_format, view.depth_format) != (
        reference.width,
        reference.height,
        reference.samples,
        reference.colour_format,
        reference.depth_format,
    ):
        raise ReceiptError("matched native target semantics differ")
    observed = view.view_matrix + view.projection_matrix + view.viewport
    desired = reference.view_matrix + reference.projection_matrix + reference.viewport
    if max(abs(a - b) for a, b in zip(observed, desired, strict=True)) > CLOCK_TOLERANCE:
        raise ReceiptError("matched camera/projection/viewport differs")


def verify_evidence(receipt: RuntimeReceipt, expected: EvidenceExpectation) -> ImageObservation:
    """Bind native observations to a previously checked launch and actual BMP."""
    _verify_launch(receipt, expected)
    _verify_view(receipt, expected)
    if (receipt.requested_style, receipt.effective_style, receipt.capability.backend) != (
        expected.requested_style,
        expected.effective_style,
        expected.backend,
    ):
        raise ReceiptError("requested/effective style or backend differs")
    if expected.production and (
        receipt.capability.reason != "None"
        or receipt.capability.features & PRODUCTION_REQUIRED != PRODUCTION_REQUIRED
        or receipt.emission.features & PRODUCTION_REQUIRED != PRODUCTION_REQUIRED
        or receipt.emission.draws == 0
        or receipt.emission.refusals != 0
        or receipt.view is None
        or receipt.view.capture_serial == 0
        or receipt.view.samples != 1
    ):
        raise ReceiptError("complete native production water was not emitted")
    if expected.production:
        _verify_adapter(receipt, expected)
    if receipt.effective_style == 0 and receipt.emission.draws != 0:
        raise ReceiptError("effective Classic contradicts issued Modern emissions")
    _verify_clock(receipt.clock, expected)
    capture = receipt.capture
    if (
        not capture.succeeded
        or not capture.native
        or capture.sequence <= expected.previous_capture
        or capture.water_frame_id != receipt.emission.frame_id
        or (receipt.view is not None and capture.water_frame_id != receipt.view.frame_id)
        or capture.frame_id < capture.water_frame_id
    ):
        raise ReceiptError("actual native image event is absent or stale")
    path = Path(capture.path).resolve(strict=True)
    if not path.is_relative_to(expected.output_root.resolve(strict=True)):
        raise ReceiptError("native image escaped the private output root")
    if path.stat().st_mtime_ns < expected.earliest_image_ns:
        raise ReceiptError("native image predates this capture request")
    observation = inspect_bmp(path, expected.image)
    if (observation.width, observation.height) != (capture.width, capture.height):
        raise ReceiptError("native image dimensions differ from its completion event")
    if receipt.view is not None and (capture.width, capture.height) != (receipt.view.width, receipt.view.height):
        raise ReceiptError("native image dimensions differ from the observed water view")
    return observation
