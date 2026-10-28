#!/usr/bin/env python3
"""Lay out the 3.5.1 release card using only the original gameplay captures.

Requires the already available Pillow and DejaVu Sans fonts. Run from any folder.
Screenshots are proportionally resized to fit, with no cropping, retouching,
colour adjustment, reconstruction, or text over the gameplay.
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
    canvas = Image.new("RGB", (2880, 1620), BACKGROUND)
    draw = ImageDraw.Draw(canvas)

    def text(x, y, value, size, color=WHITE, bold=False):
        draw.text((x, y), value, font=font(size, bold), fill=color, anchor="lt")

    draw.rectangle((0, 0, 1009, 1619), fill="#061b2c")
    text(80, 122, "ZERO HOUR", 120, bold=True)
    text(84, 280, "REFORGED LINUX", 76, color=GOLD, bold=True)
    draw.line((84, 407, 926, 407), fill=GOLD, width=3)
    text(64, 460, "3.5.1", 267, color=GOLD, bold=True)

    for y in (840, 946, 1052):
        draw.rectangle((84, y, 92, y + 51), fill=GOLD)
    text(120, 843, "More natural water", 44, bold=True)
    text(120, 949, "Better water performance", 44, bold=True)
    text(120, 1055, "Sharper path-traced", 44, bold=True)
    text(120, 1119, "water reflections", 44, bold=True)
    draw.line((84, 1320, 926, 1320), fill=GOLD, width=3)
    text(84, 1364, "New water quality controls", 38, color=MUTED)
    text(84, 1432, "Reflections · Refraction · Wave detail", 29, color=MUTED)
    text(84, 1544, "UPSTREAM 3.5.1 + LINUX IMPROVEMENTS", 24, color=GOLD,
         bold=True)

    panels = [
        (MEDIA / "v3.5.1-water.png", (1060, 40), 1780, None),
        (MEDIA / "v3.5.1-sunlit-water.png", (1260, 1014), None, 540),
        (MEDIA / "v3.5.1-reflections.png", (1840, 1014), None, 540),
    ]
    expected_panels = []
    for path, (x, y), target_width, target_height in panels:
        source = Image.open(path).convert("RGB")
        if target_width is not None:
            size = (target_width, round(source.height * target_width / source.width))
        else:
            size = (round(source.width * target_height / source.height), target_height)
        panel = source.resize(size, Image.Resampling.LANCZOS)
        width, height = panel.size
        draw.rectangle((x - 2, y - 2, x + width + 1, y + height + 1),
                       outline=GOLD, width=2)
        canvas.paste(panel, (x, y))
        expected_panels.append((path, panel, (x, y)))

    text(1060, 925, "NATURAL WAVES · ORIGINAL GAMEPLAY", 24, color=MUTED,
         bold=True)
    text(1260, 971, "SUNLIT WATER", 23, color=GOLD, bold=True)
    text(1840, 971, "SHARPER REFLECTION DETAIL", 23, color=GOLD, bold=True)

    output = MEDIA / "v3.5.1-launch.png"
    canvas.save(output, optimize=True)

    # Verify no changes beyond the specified proportional resampling.
    saved = Image.open(output).convert("RGB")
    for path, expected, (x, y) in expected_panels:
        pasted = saved.crop((x, y, x + expected.width, y + expected.height))
        if ImageChops.difference(expected, pasted).getbbox() is not None:
            raise SystemExit(f"Screenshot pixels changed: {path.name}")
        print(f"Original screenshot, proportional resize only: {path.name}, "
              f"{expected.width}x{expected.height}")
    print(f"Saved {output} ({saved.width}x{saved.height})")


if __name__ == "__main__":
    main()
