"""The controller Maps and Bossing stash art (#1686): the pad Mats art with our page plan's cells drawn on it.

The pad stash draws the RotW Materials tab's art (the mod's Controller/Panel/Stash/V2/AdditionalStash/AdvancedStash_Consumables_Console,
1304 x 1298, at layout (-2, 100)). Maps and Bossing ride the stash's Shared 10 x 10 grid (plugin/common/stash_home.h), so
their cells sit on grid cells, not on the Materials slots. This takes the Materials art, keeps its frame, edges, cube
surround and lower panel, fills the Materials slots (inside the inner gold line, outside the cube surround) with stone made
from a clean patch of the same art, and copies each cell recess the mouse art (stash_<name>_background) draws, translated
into the pad art's space (pad x = mouse x + 69, y = mouse y - 180: the mouse grid is at (89, 219) and the pad grid at (156, 139),
the pad art at (-2, 100)). The mouse art is read, never written.
Needs the toolkit's modkit. Run: py -3 tools/make_pad_stash_art.py
"""
from pathlib import Path

import numpy as np
from PIL import Image

import pd2_art

ROOT = Path(__file__).resolve().parents[1]
PANEL = ROOT / "files/data/hd/global/ui/panel/pd2r"
MATS = ROOT / "files/data/hd/global/ui/controller/panel/stash/v2/additionalstash/advancedstash_consumables_console.sprite"
GOLD_BOX = ROOT / "tools/pad_gold_box.png"  # the Shared art's gold box (x 455-845, y 1160-1256), cut from StashPanelBGShared
GOLD_AT = (455, 1160)
DX, DY = 69, -180  # mouse art pixel -> pad art pixel
CELL = 98  # grid pitch (ItemCellSize)
X0, Y0 = 90, 214  # mouse art: top-left of grid cell (0, 0)
# The Mats art's inside, below which its frame and the cube surround stay: (x0, y0, x1, y1) inclusive, in the pad art
INSIDE = [(154, 32, 1139, 532), (154, 533, 424, 1021), (868, 533, 1139, 1021)]
PATCH_L = (170, 545, 350, 925)  # clean stone of the Mats art, left of the cube surround
PATCH_R = (905, 545, 1120, 925)  # and right of it


def load(path) -> np.ndarray:
    _, _, images = pd2_art.modkit()
    (w, h, rgba), _, _ = images.read_sprite(path)
    return np.frombuffer(rgba, np.uint8).reshape(h, w, 4).copy()


def stone(patch: np.ndarray, w: int, h: int) -> np.ndarray:
    """The patch tiled over w x h, every other tile mirrored so no seam shows."""
    ph, pw = patch.shape[:2]
    ys = np.arange(h) % (2 * ph)
    xs = np.arange(w) % (2 * pw)
    ys = np.where(ys >= ph, 2 * ph - 1 - ys, ys)
    xs = np.where(xs >= pw, 2 * pw - 1 - xs, xs)
    return patch[ys][:, xs]


def plan(name: str) -> list[tuple[int, int]]:
    """Grid cells (col, row) the mouse art draws a recess in; read off that art, the cube's cells apart."""
    def sides(r0):
        return [(c, r) for r in range(r0, 10) for c in (0, 1, 8, 9)]

    if name == "maps":  # the shown base's two rows, and the map currency each side of the cube
        return [(c, r) for r in (2, 3) for c in range(10)] + sides(4)
    return ([(c, r) for r in (0, 2) for c in (0, 1, 2, 3, 6, 7, 8, 9)] + [(4, 4), (5, 4)] + sides(5))


def build(name: str, mouse: np.ndarray, mats: np.ndarray) -> np.ndarray:
    art = mats.copy()
    pl = mats[PATCH_L[1]:PATCH_L[3], PATCH_L[0]:PATCH_L[2]]
    pr = mats[PATCH_R[1]:PATCH_R[3], PATCH_R[0]:PATCH_R[2]]
    for x0, y0, x1, y1 in INSIDE:
        w, h = x1 - x0 + 1, y1 - y0 + 1
        art[y0:y1 + 1, x0:x1 + 1] = stone(pr if x0 > 600 else pl, w, h)
    for c, r in plan(name):
        x, y = X0 + c * CELL, Y0 + r * CELL
        art[y + DY:y + DY + CELL, x + DX:x + DX + CELL] = mouse[y:y + CELL, x:x + CELL]
    gx, gy = GOLD_AT  # the lower plate's gold box, as Personal and Shared draw it (the gold text sits in it)
    box = np.asarray(Image.open(GOLD_BOX).convert("RGBA"))
    art[gy:gy + box.shape[0], gx:gx + box.shape[1]] = box
    return art


def main() -> None:
    _, _, images = pd2_art.modkit()
    mats = load(MATS)
    for name in ("maps", "bossing"):
        mouse = load(PANEL / f"stash_{name}_background.sprite")
        art = build(name, mouse, mats)
        h, w = art.shape[:2]
        images.write_sprite(PANEL / f"stash_{name}_pad_background.sprite", (w, h, art.tobytes()), w, 1)
        print(f"wrote stash_{name}_pad_background ({w}x{h}, {len(plan(name))} cells) and its lowend twin")


if __name__ == "__main__":
    main()
