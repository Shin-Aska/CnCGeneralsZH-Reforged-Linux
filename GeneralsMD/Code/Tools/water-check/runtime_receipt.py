"""Versioned native observations; harness identities never authenticate themselves."""

from __future__ import annotations

from typing import Annotated, ClassVar, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError
from pydantic.alias_generators import to_camel

Count = Annotated[int, Field(strict=True, ge=0)]
Identity = Annotated[int, Field(strict=True, gt=0)]
Style = Annotated[int, Field(strict=True, ge=0, le=1)]
Number = Annotated[float, Field(strict=True, allow_inf_nan=False)]


class ReceiptError(RuntimeError):
    """Mutable exception traceback is required by Python exception machinery."""

    def __init__(self, reason: str) -> None:
        self.reason: str = reason
        super().__init__(reason)


class ReceiptUnavailableError(ReceiptError):
    pass


class WireModel(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(frozen=True, strict=True, extra="forbid", alias_generator=to_camel)


class Capability(WireModel):
    backend: Literal["Unavailable", "D3D9", "NativeD3D11", "SdlGpu"]
    features: Count
    reason: str


class NativeAdapter(WireModel):
    device_generation: Identity
    name: str = Field(min_length=1)
    vendor_id: Count
    device_id: Count
    driver_version: str | None
    software: bool
    feature_level: Count


class NativeView(WireModel):
    frame_id: Identity
    device_generation: Identity
    resource_generation: Identity
    view_id: Identity
    capture_serial: Count
    colour_identity: Identity
    depth_identity: Identity
    width: Identity
    height: Identity
    samples: Identity
    colour_format: Count
    depth_format: Count
    view_matrix: tuple[Number, ...] = Field(min_length=16, max_length=16)
    projection_matrix: tuple[Number, ...] = Field(min_length=16, max_length=16)
    viewport: tuple[Number, ...] = Field(min_length=6, max_length=6)


class NativeClock(WireModel):
    logic_frame: Count
    picture_frame: Count
    picture_seconds: Number
    water_seconds: Number
    elapsed_seconds: Number
    capture_enabled: bool | None
    frozen: bool


class Emission(WireModel):
    frame_id: Count
    features: Count
    draws: Count
    refusals: Count
    refusal_reason: str


class History(WireModel):
    generation: Count
    fft_time: Number
    foam_revision: Count
    shore_revision: Count
    vessel_revision: Count


class NativeMenu(WireModel):
    options_open: bool
    graphics_visible: bool
    selection: Style | None
    pending: bool
    error_visible: bool
    water_style_enabled: bool


class NativeCapture(WireModel):
    sequence: Identity
    frame_id: Count
    water_frame_id: Count
    path: str = Field(min_length=1)
    width: Identity
    height: Identity
    succeeded: bool
    native: bool


class RuntimeReceipt(WireModel):
    schema_version: Annotated[int, Field(strict=True, ge=1, le=1)]
    pid: Identity
    sequence: Identity
    requested_style: Style
    effective_style: Style
    capability: Capability
    view: NativeView | None
    clock: NativeClock
    emission: Emission
    history: History
    menu: NativeMenu
    capture: NativeCapture
    adapter: NativeAdapter | None = None


class Status(BaseModel):
    model_config: ClassVar[ConfigDict] = ConfigDict(strict=True, extra="ignore")
    water_validation: RuntimeReceipt | None = Field(default=None, alias="waterValidation")


def parse_receipt(text: str) -> RuntimeReceipt:
    """Missing protocol is unavailable; malformed advertised protocol is failure."""
    try:
        status = Status.model_validate_json(text)
    except ValidationError as error:
        raise ReceiptError(str(error)) from error
    if status.water_validation is None:
        raise ReceiptUnavailableError("native water receipt protocol is unavailable")
    return status.water_validation
