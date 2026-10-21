from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from isolation import prepare_output

IDENTITY_FIELDS = ("data_sha256", "scene_id", "picture_time", "settings", "backend", "water_style")


@dataclass(frozen=True, slots=True)
class ComparisonInputError(ValueError):
    path: Path
    reason: str

    def __str__(self) -> str:
        return f"{self.path}: {self.reason}"


def _load(path: Path) -> dict[str, object]:
    try:
        value: object = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ComparisonInputError(path, f"invalid JSON at line {error.lineno}") from error
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise ComparisonInputError(path, "manifest must be an object")
    return {str(key): item for key, item in value.items()}


def compare_manifests(left_path: Path, right_path: Path, output: Path) -> int:
    prepare_output(output)
    left = _load(left_path.resolve(strict=True))
    right = _load(right_path.resolve(strict=True))
    missing = tuple(field for field in IDENTITY_FIELDS if field not in left or field not in right)
    mismatched = tuple(field for field in IDENTITY_FIELDS if field in left and field in right and left[field] != right[field])
    clock_unverified = (
        left.get("capture_clock_actual_verified") is not True
        or right.get("capture_clock_actual_verified") is not True
    )
    classification = "failed" if missing or mismatched or clock_unverified else "unavailable"
    reason = (
        "manifest identity mismatch" if mismatched else
        "manifest identity fields are missing" if missing else
        "capture clock lacks runtime verification" if clock_unverified else
        "identity passed; Task 1 does not implement image metrics"
    )
    result = {
        "schema_version": 1, "classification": classification, "left": str(left_path),
        "right": str(right_path), "missing_fields": missing, "mismatched_fields": mismatched,
        "capture_clock_verified": not clock_unverified, "reason": reason,
    }
    (output / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 1 if classification == "failed" else 3
