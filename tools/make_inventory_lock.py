"""The inventory padlock buttons (#1578), from PD2's own padlock, slightly altered toward D2R's UI.

Source: PD2's buysellbtn.dc6 frames 24-27 (pd2assets.mpq: unlocked, unlocked pressed, locked, locked pressed),
upscaled 4x with Real-ESRGAN by pd2_art.upscale and committed as merge/art/ui/lock<frame>-<hash>.png.
Changes: the bright metal is warmed to D2R's gold and bronze, the shackle stands a little taller, and the face gets a soft
bevel (light top-left, shade bottom-right). Frames go to the InventoryLock (0-1) and InventoryLockOn (2-3) ButtonWidgets.
The legacy view gets the same four frames as a 26 px DC6 (#1659), on the act 1 palette.
Needs Pillow and numpy. Run: py -3 tools/make_inventory_lock.py
"""
from pathlib import Path

import numpy as np
from PIL import Image

import pd2_art
import pd2_table

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "merge/art/ui"
OUT = ROOT / "files/data/hd/global/ui/panel/pd2r/inventory_lock.sprite"
SIZE = 72  # HD frame; write_sprite makes the 36 px lowend twin
OUT_SD = ROOT / "files/data/global/ui/panel/pd2r_inventory_lock.dc6"
SD_SIZE = 26  # legacy frame (#1659)
GOLD = np.array([226, 182, 104], float)  # D2R's UI gold, for the lit metal
BRONZE = np.array([120, 92, 60], float)  # and its bronze, for the mid tones
SHACKLE = (0.24, 0.74, 0.21, 0.48)  # x0, x1, y0, y1 of the shackle band, as fractions of the frame
LIFT = 0.10  # the shackle band is drawn this much taller, growing upward


def alter(img: Image.Image) -> Image.Image:
    a = np.asarray(img.convert("RGBA"), float)
    h, w = a.shape[:2]
    # taller shackle: stretch the band upward, its bottom row fixed
    x0, x1, y0, y1 = (int(SHACKLE[0] * w), int(SHACKLE[1] * w), int(SHACKLE[2] * h), int(SHACKLE[3] * h))
    top = max(0, int(y0 - LIFT * (y1 - y0)))
    rows = np.linspace(y0, y1 - 1, y1 - top).round().astype(int)
    a[top:y1, x0:x1] = a[rows, x0:x1]
    # gold and bronze: bright greys toward gold, mid greys toward bronze, by luminance; dark stone stays
    rgb = a[..., :3]
    lum = rgb.mean(axis=2, keepdims=True) / 255.0
    warm = np.clip((lum - 0.25) / 0.5, 0, 1)
    tone = BRONZE + (GOLD - BRONZE) * np.clip((lum - 0.45) / 0.4, 0, 1)
    rgb[:] = rgb * (1 - 0.55 * warm) + tone * lum * 1.15 * (0.55 * warm)
    # bevel: light from the top-left, shade toward the bottom-right edges
    yy, xx = np.mgrid[0:h, 0:w] / (h - 1)
    edge = np.clip(1 - np.minimum.reduce([xx, yy, 1 - xx, 1 - yy]) / 0.12, 0, 1)
    rgb *= (1 + 0.18 * edge * (1 - (xx + yy)))[..., None]
    a[..., :3] = np.clip(rgb, 0, 255)
    return Image.fromarray(a.astype(np.uint8), "RGBA").resize((SIZE, SIZE), Image.LANCZOS)


def main() -> None:
    sources = [sorted(ART.glob(f"lock{n}-*.png")) for n in (24, 25, 26, 27)]
    if not all(len(s) == 1 for s in sources):
        raise SystemExit(f"need one merge/art/ui/lock<24-27>-*.png each, found {sources}")
    frames = [alter(Image.open(s[0])) for s in sources]
    strip = Image.new("RGBA", (SIZE * len(frames), SIZE))
    for i, f in enumerate(frames):
        strip.paste(f, (i * SIZE, 0))
    dc6, _, images = pd2_art.modkit()
    images.write_sprite(OUT, (strip.width, strip.height, strip.tobytes()), SIZE, len(frames))
    print(f"wrote {OUT} and its lowend twin ({len(frames)} frames)")
    pal = dc6.palette((pd2_table.D2R_DATA / "global/palette/act1/pal.dat").read_bytes())
    sd = [f.resize((SD_SIZE, SD_SIZE), Image.LANCZOS) for f in frames]
    dc6.write(OUT_SD, [dc6.encode((SD_SIZE, SD_SIZE, f.tobytes()), pal) for f in sd])
    print(f"wrote {OUT_SD}")


if __name__ == "__main__":
    main()
