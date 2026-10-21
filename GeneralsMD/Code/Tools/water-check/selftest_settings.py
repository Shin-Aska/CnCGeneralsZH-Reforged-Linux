#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pytest>=8,<10", "pydantic>=2.10,<3"]
# ///
# ─── How to run ───
# Install uv, then: uv run selftest_settings.py
# ──────────────────
"""CPU harness tests, never evidence of real native UI or water rendering."""

from __future__ import annotations

import json
import os
import struct
from dataclasses import replace
from pathlib import Path
from typing import TypeAlias

import pytest
from models import ExpectedImage, Rect
from receipt_evidence import (
    ClockMode,
    EvidenceExpectation,
    LaunchIdentity,
    verify_evidence,
)
from runtime_receipt import (
    ReceiptError,
    ReceiptUnavailableError,
    parse_receipt,
)
from settings_cases import default_cases, require_ordinary_environment, stage_case

JsonValue: TypeAlias = str | int | float | bool | list["JsonValue"] | dict[str, "JsonValue"] | None


def bitmap(path: Path) -> None:
    pixels = b"".join(bytes((100 + x * 8, 70 + y * 3, 20)) for y in range(8) for x in range(8))
    header = struct.pack(
        "<2sIHHIIIIHHIIIIII", b"BM", 54 + len(pixels), 0, 0, 54, 40, 8, 8, 1, 24, 0, len(pixels), 0, 0, 0, 0
    )
    _ = path.write_bytes(header + pixels)


def expectation(root: Path) -> EvidenceExpectation:
    identity = LaunchIdentity(1234, (root / "checked.exe").resolve(), "a" * 64, "b" * 64, "c" * 64)
    return EvidenceExpectation(
        identity,
        1,
        0,
        1,
        1,
        "NativeD3D11",
        True,
        ClockMode.ORDINARY,
        None,
        root,
        0,
        ExpectedImage(Rect(0, 0, 8, 8), 30, 0.0, 1.0),
        capture_request_ns=lambda: 0,
    )


def mutate(status: dict[str, JsonValue], field: str, value: JsonValue) -> None:
    body = status["waterValidation"]
    assert isinstance(body, dict)
    keys = field.split(".")
    for key in keys[:-1]:
        nested = body[key]
        assert isinstance(nested, dict)
        body = nested
    body[keys[-1]] = value


def native_status(path: Path) -> dict[str, JsonValue]:
    matrix: list[JsonValue] = [1.0, 0.0, 0.0, 0.0] * 4
    return {
        "waterValidation": {
            "schemaVersion": 1,
            "pid": 1234,
            "sequence": 2,
            "requestedStyle": 1,
            "effectiveStyle": 1,
            "capability": {"backend": "NativeD3D11", "features": 65535, "reason": "None"},
            "adapter": {
                "deviceGeneration": 1,
                "name": "CPU test device",
                "vendorId": 1,
                "deviceId": 2,
                "driverVersion": "unit-fixture",
                "software": False,
                "featureLevel": 45056,
            },
            "view": {
                "frameId": 7,
                "deviceGeneration": 1,
                "resourceGeneration": 1,
                "viewId": 8,
                "captureSerial": 5,
                "colourIdentity": 100,
                "depthIdentity": 101,
                "width": 8,
                "height": 8,
                "samples": 1,
                "colourFormat": 10,
                "depthFormat": 44,
                "viewMatrix": matrix,
                "projectionMatrix": matrix,
                "viewport": [0.0, 0.0, 8.0, 8.0, 0.0, 1.0],
            },
            "clock": {
                "logicFrame": 120,
                "pictureFrame": 240,
                "pictureSeconds": 4.0,
                "waterSeconds": 4.0,
                "elapsedSeconds": 0.0,
                "captureEnabled": False,
                "frozen": True,
            },
            "emission": {"frameId": 7, "features": 65535, "draws": 3, "refusals": 0, "refusalReason": "None"},
            "history": {"generation": 1, "fftTime": 4.0, "foamRevision": 2, "shoreRevision": 3, "vesselRevision": 4},
            "menu": {
                "optionsOpen": False,
                "graphicsVisible": False,
                "selection": None,
                "pending": False,
                "errorVisible": False,
                "waterStyleEnabled": True,
            },
            "capture": {
                "sequence": 1,
                "frameId": 7,
                "waterFrameId": 7,
                "path": str(path),
                "width": 8,
                "height": 8,
                "succeeded": True,
                "native": True,
            },
        }
    }


def test_receipt_when_versioned_native_status_is_present(tmp_path: Path) -> None:
    # Given: a native versioned receipt, distinct from the current legacy status.
    status = json.dumps(native_status(tmp_path / "sshot000.bmp"))
    # When: the receipt is parsed.
    receipt = parse_receipt(status)
    # Then: a typed requested style is available for independent evidence checks.
    assert receipt.requested_style == 1


@pytest.mark.parametrize("field", ["schemaVersion", "requestedStyle", "effectiveStyle"])
def test_receipt_when_integer_is_boolean(tmp_path: Path, field: str) -> None:
    # Given: a boolean in a native numeric field.
    status = native_status(tmp_path / "sshot000.bmp")
    body = status["waterValidation"]
    assert isinstance(body, dict)
    body[field] = True
    # When/Then: parsing rejects Python's bool-as-int ambiguity.
    with pytest.raises(ReceiptError):
        _ = parse_receipt(json.dumps(status))


def test_defaults_when_fresh_or_legacy_options_are_staged(tmp_path: Path) -> None:
    # Given: all four required independent initial preference cases.
    cases = default_cases()
    # When: each case receives a distinct private user root.
    users = tuple(stage_case(tmp_path, case) for case in cases)
    # Then: no missing-key case is contaminated with an explicit WaterStyle.
    assert tuple(case.requested_style for case in cases) == (1, 1, 0, 1)
    assert len(set(users)) == 4
    assert not (users[0] / "Options.ini").exists()
    assert b"WaterStyle" not in (users[1] / "Options.ini").read_bytes()


@pytest.mark.parametrize(
    ("field", "value"),
    [
        ("pid", 9999),
        ("sequence", 1),
        ("effectiveStyle", 0),
        ("capability.backend", "D3D9"),
        ("capability.features", 127),
        ("emission.features", 127),
        ("emission.draws", 0),
        ("emission.refusals", 1),
        ("view.captureSerial", 0),
        ("view.samples", 2),
        ("clock.captureEnabled", None),
        ("clock.captureEnabled", True),
        ("capture.native", False),
        ("capture.waterFrameId", 6),
        ("view", None),
        ("view.width", 16),
        ("adapter.software", True),
        ("adapter.featureLevel", 40960),
        ("adapter.deviceGeneration", 2),
    ],
)
def test_evidence_when_native_observation_is_incorrect(tmp_path: Path, field: str, value: JsonValue) -> None:
    # Given: one independently invalid observation and a real small BMP fixture.
    image = tmp_path / "native.bmp"
    bitmap(image)
    status = native_status(image)
    mutate(status, field, value)
    # When/Then: incorrect runtime evidence fails rather than inheriting expected state.
    with pytest.raises(ReceiptError):
        _ = verify_evidence(parse_receipt(json.dumps(status)), expectation(tmp_path))


def test_evidence_when_native_image_is_bound(tmp_path: Path) -> None:
    # Given: actual bytes and an independently specified expected process/style/features.
    image = tmp_path / "native.bmp"
    bitmap(image)
    receipt = parse_receipt(json.dumps(native_status(image)))
    # When: all observations and image completion are checked.
    result = verify_evidence(receipt, expectation(tmp_path))
    # Then: the actual image hash is returned for the harness envelope.
    assert (result.width, result.height, result.water_pixels) == (8, 8, 64)


@pytest.mark.parametrize("enabled", [None, False, True])
def test_matched_clock_when_capture_eligibility_is_observed(tmp_path: Path, enabled: bool | None) -> None:
    # Given: matched picture/water time with separately observed eligibility.
    image = tmp_path / "native.bmp"
    bitmap(image)
    status = native_status(image)
    mutate(status, "clock.captureEnabled", enabled)
    expected = replace(
        expectation(tmp_path),
        clock_mode=ClockMode.MATCHED,
        picture_seconds=4.0,
        expected_view=parse_receipt(json.dumps(native_status(image))).view,
    )
    # When/Then: only a true native eligibility result permits matched evidence.
    if enabled is True:
        _ = verify_evidence(parse_receipt(json.dumps(status)), expected)
    else:
        with pytest.raises(ReceiptError):
            _ = verify_evidence(parse_receipt(json.dumps(status)), expected)


def test_receipt_when_current_binary_has_no_protocol() -> None:
    # Given: the current native status shape.
    legacy = '{"ok":true,"frame":120,"inGame":true,"players":[]}'
    # When/Then: absence is unavailable, never a native pass.
    with pytest.raises(ReceiptUnavailableError):
        _ = parse_receipt(legacy)


def test_defaults_when_environment_is_contaminated() -> None:
    # Given: a copied caller environment with one inherited water override.
    original = dict(os.environ)
    contaminated = original | {"ZH_WATER_FFT": "0"}
    # When/Then: the rejected run leaves caller existence and values untouched.
    with pytest.raises(ReceiptError):
        require_ordinary_environment(contaminated)
    assert dict(os.environ) == original


def test_classic_when_modern_emissions_are_still_claimed(tmp_path: Path) -> None:
    # Given: requested Classic with contradictory Modern emissions.
    image = tmp_path / "native.bmp"
    bitmap(image)
    status = native_status(image)
    mutate(status, "requestedStyle", 0)
    mutate(status, "effectiveStyle", 0)
    expected = replace(expectation(tmp_path), requested_style=0, effective_style=0, production=False)
    # When/Then: effective Classic cannot claim an issued Modern pass.
    with pytest.raises(ReceiptError):
        _ = verify_evidence(parse_receipt(json.dumps(status)), expected)


def test_image_when_completed_event_has_no_file(tmp_path: Path) -> None:
    # Given: a successful-looking completion event without image bytes.
    status = native_status(tmp_path / "absent.bmp")
    # When/Then: the real file read fails.
    with pytest.raises(FileNotFoundError):
        _ = verify_evidence(parse_receipt(json.dumps(status)), expectation(tmp_path))


def test_image_when_file_predates_request(tmp_path: Path) -> None:
    # Given: a pre-existing image with a newer required capture boundary.
    image = tmp_path / "native.bmp"
    bitmap(image)
    expected = replace(expectation(tmp_path), earliest_image_ns=image.stat().st_mtime_ns + 1)
    # When/Then: old bytes cannot satisfy a new screenshot request.
    with pytest.raises(ReceiptError):
        _ = verify_evidence(parse_receipt(json.dumps(native_status(image))), expected)


def test_production_when_actual_device_is_unobserved(tmp_path: Path) -> None:
    # Given: a hardware host is insufficient when the launched device is unknown.
    image = tmp_path / "native.bmp"
    bitmap(image)
    status = native_status(image)
    mutate(status, "adapter", None)
    # When/Then: production cannot pass without actual native device evidence.
    with pytest.raises(ReceiptUnavailableError):
        _ = verify_evidence(parse_receipt(json.dumps(status)), expectation(tmp_path))


def test_matched_when_reference_view_is_unknown(tmp_path: Path) -> None:
    # Given: matching times without an independently supplied view reference.
    image = tmp_path / "native.bmp"
    bitmap(image)
    status = native_status(image)
    mutate(status, "clock.captureEnabled", True)
    expected = replace(expectation(tmp_path), clock_mode=ClockMode.MATCHED, picture_seconds=4.0)
    # When/Then: camera identity cannot be silently inferred from the observation.
    with pytest.raises(ReceiptUnavailableError):
        _ = verify_evidence(parse_receipt(json.dumps(status)), expected)


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, str(Path(__file__).with_name("selftest_menu_control.py")), "-q"]))
