"""HD inventory rows 5-8 (the charm area) carry a tint sprite sized to exactly those cells."""
import struct
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files" / "data"
CELL, COLS, ROWS = 98, 10, 4


class CharmTint(unittest.TestCase):
    def test_sprite_size(self):
        for name, div in (("inventory_charm_tint", 1), ("inventory_charm_tint.lowend", 2)):
            head = (DATA / f"hd/global/ui/panel/pd2r/{name}.sprite").read_bytes()[:16]
            w, h = struct.unpack("<II", head[8:16])
            self.assertEqual((w, h), (COLS * CELL // div, ROWS * CELL // div))

    def test_layouts_place_it(self):
        base = (DATA / "global/ui/layouts/playerinventoryoriginallayouthd.json").read_text("utf-8-sig")
        self.assertIn('"rect": { "x": 93, "y": 1289 }', base)  # grid y 897 + 4 rows x 98
        self.assertIn("inventory_charm_tint", base)
        exp = (DATA / "global/ui/layouts/playerinventoryexpansionlayouthd.json").read_text("utf-8-sig")
        self.assertIn('"name": "charm_tint"', exp)


if __name__ == "__main__":
    unittest.main()
