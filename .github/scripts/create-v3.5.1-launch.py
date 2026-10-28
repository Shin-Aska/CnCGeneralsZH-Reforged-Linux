#!/usr/bin/env python3
"""Lay out the 3.5.1 release card without changing screenshot pixels.

Requires the already available Pillow and DejaVu Sans fonts. Run from any folder.
Screenshots are pasted at their native dimensions, with no crop, resampling,
retouching, colour adjustment, or text over the gameplay.
"""

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
MEDIA = ROOT / ".github/media/releases"
FONTS = Path("/usr/share/fonts/truetype/dejavu")
BACKGROUND = "#0b1721"
WHITE = "#edf2f3"
MUTED = "#aabcc7"
GOLD = "#e3bc72"


def font(size, bold=False):
    return ImageFont.truetype(
        str(FONTS / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")), size
    )


def main():
    canvas = Image.new("RGB", (2720, 1530), BACKGROUND)
    draw = ImageDraw.Draw(canvas)

    def text(x, y, value, size, color=WHITE, bold=False):
        draw.text((x, y), value, font=font(size, bold), fill=color, anchor="lt")

    draw.rectangle((80, 76, 92, 274), fill=GOLD)
    text(126, 82, "ZERO HOUR", 105, bold=True)
    text(130, 212, "REFORGED LINUX", 53, color=GOLD, bold=True)
    draw.text((2640, 70), "3.5.1", font=font(190, True), fill=GOLD, anchor="rt")
    draw.line((80, 328, 2640, 328), fill="#354957", width=2)

    text(80, 374, "MORE NATURAL WATER", 30, color=MUTED, bold=True)
    text(1698, 374, "SHARPER WATER REFLECTIONS", 30, color=MUTED, bold=True)

    panels = [
        (MEDIA / "v3.5.1-water.png", (80, 430)),
        (MEDIA / "v3.5.1-reflections.png", (1698, 430)),
    ]
    for path, (x, y) in panels:
        source = Image.open(path).convert("RGB")
        width, height = source.size
        draw.rectangle((x - 2, y - 2, x + width + 1, y + height + 1),
                       outline="#41596a", width=2)
        canvas.paste(source, (x, y))

    text(1698, 1111, "NEW WATER QUALITY CONTROLS", 28, color=GOLD, bold=True)
    text(1698, 1162, "Scene reflections  ·  On / Off", 29)
    text(1698, 1207, "Refraction  ·  Full / Simple / Off", 29)
    text(1698, 1252, "Wave detail  ·  High 256 / Low 128", 29)

    draw.line((80, 1343, 2640, 1343), fill="#354957", width=2)
    text(80, 1388, "Optimized water performance", 43, bold=True)
    draw.text((2640, 1402), "UPSTREAM 3.5.1 + LINUX IMPROVEMENTS",
              font=font(26, True), fill=MUTED, anchor="rt")

    output = MEDIA / "v3.5.1-launch.png"
    canvas.save(output, optimize=True)

    # Verify the saved artifact itself, including every original HUD/cursor pixel.
    saved = Image.open(output).convert("RGB")
    for path, (x, y) in panels:
        source = Image.open(path).convert("RGB")
        pasted = saved.crop((x, y, x + source.width, y + source.height))
        if ImageChops.difference(source, pasted).getbbox() is not None:
            raise SystemExit(f"Screenshot pixels changed: {path.name}")
        print(f"Unchanged screenshot: {path.name}, {source.width}x{source.height}")
    print(f"Saved {output} ({saved.width}x{saved.height})")


if __name__ == "__main__":
    main()
