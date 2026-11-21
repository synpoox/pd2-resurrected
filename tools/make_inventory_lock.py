"""Draws the inventory padlock button (#1578) from scratch and writes its HD and lowend sprites.

Frames, as the InventoryLock ToggleButtonWidget reads them: 0 unlocked, 1 unlocked pressed, 2 locked, 3 locked pressed.
A dark stone face with a bevel and a gold padlock; the unlocked shackle is raised and open on the right.
Needs Pillow. Run: py -3 tools/make_inventory_lock.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

import pd2_art

SIZE = 72  # HD frame; write_sprite makes the 36 px lowend twin
SS = 4  # drawn at 4x, then downsampled for smooth edges
OUT = Path(__file__).resolve().parents[1] / "files/data/hd/global/ui/panel/pd2r/inventory_lock.sprite"

GOLD, GOLD_DARK, GOLD_LIGHT, OUTLINE = (176, 138, 62), (112, 84, 34), (222, 196, 128), (34, 24, 10)
SHACKLE = (186, 176, 150)  # grey-gold steel


def frame(locked: bool, pressed: bool) -> Image.Image:
    n = SIZE * SS
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = lambda v: v * SS  # noqa: E731
    # stone face: dark rim, bevel (light top-left, dark bottom-right, swapped when pressed), face
    d.rounded_rectangle((s(2), s(2), s(70), s(70)), s(8), fill=(12, 11, 10, 255))
    light, dark = ((28, 26, 24), (92, 84, 74)) if pressed else ((92, 84, 74), (28, 26, 24))
    d.rounded_rectangle((s(4), s(4), s(68), s(68)), s(7), fill=dark + (255,))
    d.rounded_rectangle((s(4), s(4), s(66), s(66)), s(7), fill=light + (255,))
    d.rounded_rectangle((s(6), s(6), s(66), s(66)), s(6), fill=(38, 35, 31, 255) if pressed else (50, 46, 41, 255))
    off = s(1) if pressed else 0
    # shackle: a U of steel; open = raised 8 px with the right leg stopping short of the body
    lift = 0 if locked else s(8)
    w = s(6)
    x0, x1, top = s(25), s(47), s(14) - lift
    for col, width in ((OUTLINE, w + s(2)), (SHACKLE, w)):
        d.arc((x0, top + off, x1, top + s(22) + off), 180, 360, fill=col + (255,), width=width)
        left = x0 + w // 2
        d.line((left, top + s(11) + off, left, s(34) + off), fill=col + (255,), width=width)
        right_end = s(34) if locked else s(24) - lift
        d.line((x1 - w // 2, top + s(11) + off, x1 - w // 2, right_end + off), fill=col + (255,), width=width)
    # body: gold with outline, a light top edge and a keyhole
    d.rounded_rectangle((s(18), s(32) + off, s(54), s(60) + off), s(4), fill=OUTLINE + (255,))
    d.rounded_rectangle((s(20), s(34) + off, s(52), s(58) + off), s(3), fill=GOLD + (255,))
    d.rectangle((s(21), s(35) + off, s(51), s(37) + off), fill=GOLD_LIGHT + (255,))
    d.rectangle((s(21), s(54) + off, s(51), s(57) + off), fill=GOLD_DARK + (255,))
    d.ellipse((s(33), s(40) + off, s(39), s(46) + off), fill=OUTLINE + (255,))
    d.polygon([(s(34), s(44) + off), (s(38), s(44) + off), (s(39), s(52) + off), (s(33), s(52) + off)],
              fill=OUTLINE + (255,))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main() -> None:
    frames = [frame(locked, pressed) for locked in (False, True) for pressed in (False, True)]
    strip = Image.new("RGBA", (SIZE * len(frames), SIZE))
    for i, f in enumerate(frames):
        strip.paste(f, (i * SIZE, 0))
    _, _, images = pd2_art.modkit()
    images.write_sprite(OUT, (strip.width, strip.height, strip.tobytes()), SIZE, len(frames))
    print(f"wrote {OUT} and its lowend twin ({len(frames)} frames)")


if __name__ == "__main__":
    main()
