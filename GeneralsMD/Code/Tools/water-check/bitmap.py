from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from capture_archive import read_member
from models import ExpectedImage, ImageObservation


@dataclass(frozen=True, slots=True)
class InvalidImageError(ValueError):
    path: Path
    reason: str

    def __str__(self) -> str:
        return f"{self.path}: {self.reason}"


def inspect_bmp(path: Path, expected: ExpectedImage) -> ImageObservation:
    data = path.read_bytes()
    return _inspect_bmp_data(data, path, expected)


def inspect_bmp_member(
    archive: Path, manifest: Path, member: str, expected: ExpectedImage
) -> ImageObservation:
    return _inspect_bmp_data(read_member(archive, manifest, member), Path(f"{archive}!{member}"), expected)


def _inspect_bmp_data(data: bytes, path: Path, expected: ExpectedImage) -> ImageObservation:
    if len(data) < 54 or data[:2] != b"BM":
        raise InvalidImageError(path, "not a native BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size, width, signed_height, planes, bits, compression = struct.unpack_from("<IiiHHI", data, 14)
    if dib_size < 40 or width <= 0 or signed_height == 0 or planes != 1:
        raise InvalidImageError(path, "unsupported BMP header")
    if bits not in (24, 32) or compression != 0:
        raise InvalidImageError(path, "BMP must be uncompressed 24-bit or 32-bit")
    height = abs(signed_height)
    roi = expected.roi
    if roi.x < 0 or roi.y < 0 or roi.width <= 0 or roi.height <= 0:
        raise InvalidImageError(path, "invalid water ROI")
    if roi.x + roi.width > width or roi.y + roi.height > height:
        raise InvalidImageError(path, "water ROI is outside the BMP")
    if height >= 600 and roi.y + roi.height > height * 2 // 3:
        raise InvalidImageError(path, "water ROI overlaps the HUD exclusion band")
    pixel_bytes = bits // 8
    row_bytes = (width * pixel_bytes + 3) & ~3
    if pixel_offset < 54 or pixel_offset + row_bytes * height > len(data):
        raise InvalidImageError(path, "truncated BMP pixels")

    water_pixels = 0
    visible_pixels = 0
    minimum = 1.0
    maximum = 0.0
    for y in range(roi.y, roi.y + roi.height):
        source_y = height - 1 - y if signed_height > 0 else y
        for x in range(roi.x, roi.x + roi.width):
            at = pixel_offset + source_y * row_bytes + x * pixel_bytes
            blue, green, red = data[at : at + 3]
            luma = (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0
            minimum = min(minimum, luma)
            maximum = max(maximum, luma)
            if luma >= 0.03:
                visible_pixels += 1
            blue_water = blue >= green and blue >= red + 20 and green >= red
            brown_water = (
                red < 140 and green >= red * 0.55 and blue >= red * 0.25
                and max(red, green, blue) - min(red, green, blue) < 80
            )
            if blue_water or brown_water:
                water_pixels += 1
    if (
        minimum < expected.min_luma
        or maximum > expected.max_luma
        or maximum - minimum < 0.01
        or visible_pixels < roi.width * roi.height // 10
    ):
        raise InvalidImageError(path, "blank or clipped water ROI")
    if water_pixels < expected.min_water_pixels:
        raise InvalidImageError(path, "water signature is absent from ROI")
    return ImageObservation(
        width, height, water_pixels, minimum, maximum, visible_pixels, hashlib.sha256(data).hexdigest()
    )
