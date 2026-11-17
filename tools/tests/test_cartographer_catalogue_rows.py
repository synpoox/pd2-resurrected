"""#952: the native catalogue is m01-m05, one map per tileset, each on a slot the game check opened and returned from."""
import unittest
import csv
from pathlib import Path

CART = Path(__file__).resolve().parents[2] / "files/data/global/excel/d2rloader/cartographer"


def rows(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class CartographerCatalogueRowsTests(unittest.TestCase):
    def test_five_maps_one_per_tileset_on_distinct_items(self):
        maps = rows("maps.txt")
        assert [m["Item"] for m in maps] == ["m01", "m02", "m03", "m04", "m05"]
        assert len({m["Tileset"] for m in maps}) == len(maps) == len({m["Name"] for m in maps})


    def test_maps_are_tierless_medium_level_85(self):
        for m in rows("maps.txt"):
            assert m["Size"] == "Medium" and m["Level"] == "85" and m["MinCharLevel"] == "1", m["Name"]


    def test_every_tileset_family_has_a_slot_level(self):
        """Each map tileset (Cave, Crypt, Ice, Jungle, Snow) must name a Slot whose Level is the game level it opens in."""
        # game check 2026-11-15: Cave 138, Crypt 139, Ice 141, Jungle 142, Snow 143 (202-207 before #1231) opened and were built as planned.
        levels = {s["Slot"]: int(s["Level"]) for s in rows("slots.txt")}
        assert [levels[k] for k in ("Maze", "MazeCrypt", "MazeIce", "Outdoor", "OutdoorSnow")] == [138, 139, 141, 142, 143]
