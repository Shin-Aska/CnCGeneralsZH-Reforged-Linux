from __future__ import annotations

from models import CaptureIdentity


def compare_identity(left: CaptureIdentity, right: CaptureIdentity) -> tuple[str, ...]:
    differences: list[str] = []
    for field in (
        "executable_sha256",
        "data_sha256",
        "scene_id",
        "picture_time",
        "backend",
        "water_style",
    ):
        if getattr(left, field) != getattr(right, field):
            differences.append(field)
    return tuple(differences)
