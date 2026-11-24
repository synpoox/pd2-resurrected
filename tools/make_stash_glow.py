"""The Maps tab's tier and base glow (#2116): D2R's tab notification (PANEL\\Stash\\AdditionalStash\\StashNotification,
a flat 255,236,160 fill at alpha 100) as a 94 px square, the base slot's size; the tier buttons draw it scaled to 88.
Needs Pillow. Run: py -3 tools/make_stash_glow.py
"""
from pathlib import Path

from PIL import Image

import pd2_art

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "files/data/hd/global/ui/panel/pd2r/stash_glow.sprite"
SIZE = 94
FILL = (255, 236, 160, 100)


def main() -> None:
    _, _, images = pd2_art.modkit()
    img = Image.new("RGBA", (SIZE, SIZE), FILL)
    images.write_sprite(OUT, (SIZE, SIZE, img.tobytes()), SIZE, 1)
    print(f"wrote {OUT} and its lowend twin")


if __name__ == "__main__":
    main()
