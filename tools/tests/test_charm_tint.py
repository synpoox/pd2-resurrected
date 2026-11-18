"""HD inventory rows 5-8 (the charm area) carry a tint sprite sized to exactly those cells."""
import json
import re
import struct
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files" / "data"
LAYOUTS = DATA / "global/ui/layouts"
CELL, COLS, ROWS = 98, 10, 4


def load(name):
    """D2R layouts allow comments and trailing commas; strip them, then parse."""
    text = (LAYOUTS / name).read_text("utf-8-sig")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"^\s*//.*$", "", text, flags=re.M)
    text = re.sub(r",(\s*[}\]])", r"\1", text)
    return json.loads(text)


def names(layout):
    return [c["name"] for c in layout["children"]]


class CharmTint(unittest.TestCase):
    def test_sprite_size(self):
        for name, div in (("inventory_charm_tint", 1), ("inventory_charm_tint.lowend", 2)):
            head = (DATA / f"hd/global/ui/panel/pd2r/{name}.sprite").read_bytes()[:16]
            w, h = struct.unpack("<II", head[8:16])
            self.assertEqual((w, h), (COLS * CELL // div, ROWS * CELL // div))

    def test_layouts_place_it(self):
        base = load("playerinventoryoriginallayouthd.json")
        kids = {c["name"]: c for c in base["children"]}
        grid, tint = kids["grid"]["fields"]["rect"], kids["charm_tint"]["fields"]["rect"]
        self.assertEqual(tint["x"], grid["x"])
        self.assertEqual(tint["y"], grid["y"] + 4 * CELL)
        self.assertEqual(kids["charm_tint"]["fields"]["filename"], "PANEL\\pd2r\\inventory_charm_tint")
        for layout in (base, load("playerinventoryexpansionlayouthd.json")):
            n = names(layout)
            self.assertLess(n.index("background"), n.index("charm_tint"))
        self.assertLess(names(base).index("charm_tint"), names(base).index("grid"))
        self.assertLess(names(load("playerinventoryexpansionlayouthd.json")).index("charm_tint"),
                        names(load("playerinventoryexpansionlayouthd.json")).index("grid"))


if __name__ == "__main__":
    unittest.main()
