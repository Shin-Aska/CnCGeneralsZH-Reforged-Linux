#!/usr/bin/env python3
"""Composite original screenshots into the user's annotated launch layout.

Retains the approved typography and angled panel positions. Gameplay comes only
from the supplied screenshots, proportionally resized and clipped into panels.
No reconstruction, retouching, recolouring or generated gameplay is used.
"""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / ".github/media/releases"
SIZE = (1672, 941)
GOLD = "#e8c570"
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
OUTPUT = MEDIA / "v3.5.1-launch-annotated-layout-v2.png"


def fitted(name, size, center=(0.5, 0.5)):
    original = Image.open(MEDIA / name).convert("RGB")
    return ImageOps.fit(original, size, method=Image.Resampling.LANCZOS,
                        centering=center)


def main():
    canvas = fitted("v3.5.1-shore-waves-refraction.png", SIZE).convert("RGBA")

    # Match the original layout's dark fade behind its unchanged typography.
    stops = [(0, 255), (350, 253), (520, 240), (680, 180), (880, 30), (1040, 0)]
    row = Image.new("L", (SIZE[0], 1))
    for (start, a), (end, b) in zip(stops, stops[1:]):
        for x in range(start, end):
            row.putpixel((x, 0), round(a + (b - a) * (x - start) / (end - start)))
    fade = Image.new("RGBA", SIZE, "#001a2d")
    fade.putalpha(row.resize(SIZE))
    canvas = Image.alpha_composite(canvas, fade)

    def panel(name, polygon, center=(0.5, 0.5)):
        xs, ys = zip(*polygon)
        box = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
        photo = fitted(name, (box[2] - box[0], box[3] - box[1]), center)
        layer = Image.new("RGBA", SIZE)
        layer.paste(photo, box[:2])
        mask = Image.new("L", SIZE)
        ImageDraw.Draw(mask).polygon(polygon, fill=255)
        canvas.paste(layer, (0, 0), mask)

    # Same two angled inset outlines as the approved design. The left inset is
    # split horizontally: Low 128 on top, High 256 below, as annotated.
    low = [(739, 648), (1221, 648), (1149, 794), (640, 794)]
    high = [(640, 794), (1149, 794), (1077, 940), (540, 940)]
    path = [(1239, 648), (1671, 648), (1671, 940), (1097, 940)]
    panel("v3.5.1-low-wave-detail.png", low, (0.5, 0.32))
    panel("v3.5.1-sunlit-water.png", high, (0.5, 0.32))
    panel("v3.5.1-reflections.png", path, (0.5, 0.55))

    draw = ImageDraw.Draw(canvas)
    draw.line([(540, 940), (739, 648), (1221, 648), (1077, 940)],
              fill=GOLD, width=3)
    draw.line([(640, 794), (1149, 794)], fill=GOLD, width=2)
    draw.line(path + [path[0]], fill=GOLD, width=3)

    # These pixels retain only the approved title, version, bullets and rules.
    canvas = Image.alpha_composite(
        canvas, Image.open(MEDIA / "v3.5.1-title-treatment.png").convert("RGBA")
    )

    def label(x, y, lines, size=17):
        face = ImageFont.truetype(FONT, size)
        measure = ImageDraw.Draw(canvas)
        width = max(measure.textlength(line, font=face) for line in lines)
        height = len(lines) * (size + 7) + 12
        layer = Image.new("RGBA", SIZE)
        ink = ImageDraw.Draw(layer)
        ink.rounded_rectangle((x - 10, y - 8, x + width + 10, y + height - 8),
                              radius=4, fill=(0, 20, 36, 215))
        for i, line in enumerate(lines):
            ink.text((x, y + i * (size + 7)), line, font=face,
                     fill="#f2e3b7", anchor="lt")
        return Image.alpha_composite(canvas, layer)

    canvas = label(770, 578, ["Waves now follow the shore"], 18)
    canvas = label(773, 669, ["LOW WAVE DETAIL · 128×128 FFT"], 16)
    canvas = label(683, 817, ["HIGH WAVE DETAIL · 256×256 FFT"], 16)
    canvas = label(1262, 668, ["SHARPER PATH TRACING", "REFLECTIONS"], 17)
    canvas.convert("RGB").save(OUTPUT, optimize=True)
    print(f"Saved {OUTPUT} ({SIZE[0]}x{SIZE[1]})")
    print("Original angled layout; original screenshots; Low above High.")


if __name__ == "__main__":
    main()
