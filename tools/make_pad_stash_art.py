"""The controller Maps and Bossing stash art (#1686): the mouse art (stash_<name>_background) narrowed to the pad panel.

The pad panel's frame is 1085 px wide (stock Controller StashPanelBG, opaque x 111..1195 of 1305, drawn at layout x -2);
the mouse art is 1162 px, so its frame hung 42 px out on the left and 36 px on the right. A band of the plain stone
between the outer rim and the inner rim is cut from each side and the two halves joined, so the grid area (columns
42..1126 of the cut art, 89 px in from the edge on the mouse art) keeps its columns and only the frame gets narrower.
Needs the toolkit's modkit. Run: py -3 tools/make_pad_stash_art.py
"""
from pathlib import Path

import pd2_art

ROOT = Path(__file__).resolve().parents[1]
PANEL = ROOT / "files/data/hd/global/ui/panel/pd2r"
CUT_LEFT = (22, 64)  # columns of the mouse art removed on the left (42 px), clear of the grid at x=89
CUT_RIGHT = (1104, 1140)  # and on the right (36 px)


def narrow(img, images):
    w, h, _ = img
    out = images.blank(w - 42 - 36, h)
    x = 0
    for a, b in ((0, CUT_LEFT[0]), (CUT_LEFT[1], CUT_RIGHT[0]), (CUT_RIGHT[1], w)):
        out = images.paste(out, images.crop(img, a, 0, b - a, h), x, 0)
        x += b - a
    return out


def main() -> None:
    _, _, images = pd2_art.modkit()
    for name in ("maps", "bossing"):
        img, _, _ = images.read_sprite(PANEL / f"stash_{name}_background.sprite")
        out = narrow(img, images)
        images.write_sprite(PANEL / f"stash_{name}_pad_background.sprite", out, out[0], 1)
        print(f"wrote stash_{name}_pad_background ({out[0]}x{out[1]}) and its lowend twin")


if __name__ == "__main__":
    main()
