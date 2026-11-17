"""#952, #1287: the native catalogue is m01-m25, one map per Cartographer tileset, each on a slot the game check opened."""
import unittest
import csv
from pathlib import Path

CART = Path(__file__).resolve().parents[2] / "files/data/global/excel/d2rloader/cartographer"


def rows(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class CartographerCatalogueRowsTests(unittest.TestCase):
    def test_25_maps_one_per_tileset_on_distinct_items(self):
        maps = rows("maps.txt")
        assert [m["Item"] for m in maps] == [f"m{i:02d}" for i in range(1, 26)]
        assert len({m["Tileset"] for m in maps}) == len(maps) == len({m["Name"] for m in maps})


    def test_maps_are_tierless_medium_level_85(self):
        for m in rows("maps.txt"):
            assert m["Size"] == "Medium" and m["Level"] == "85" and m["MinCharLevel"] == "1", m["Name"]


    def test_every_tileset_family_has_a_slot_level(self):
        """Each map tileset (Cave, Crypt, Ice, Jungle, Snow) must name a Slot whose Level is the game level it opens in."""
        # game check 2026-11-15: Cave 138, Crypt 139, Ice 141, Jungle 142, Snow 143 (202-207 before #1231) opened and were built as planned.
        levels = {s["Slot"]: int(s["Level"]) for s in rows("slots.txt")}
        assert [levels[k] for k in ("Maze", "MazeCrypt", "MazeIce", "Outdoor", "OutdoorSnow")] == [138, 139, 141, 142, 143]
        # #1287: the other 20 tilesets, each with a map of its own (Lava 140, Siege 144, then the ids the installer gave the new levels)
        assert [levels[k] for k in ("MazeLava", "OutdoorSiege", "MazeDurance", "MazeArcane", "OutdoorMesa", "OutdoorDesert", "OutdoorWilderness", "OutdoorKurast") + tuple("Maze" + n for n in ("Dungeon", "Jail", "Lair", "KurastSewer", "Barracks", "Harem", "Basement", "Sewer", "Catacombs", "Tombs", "Spider", "Temple"))] == [140, 144, 152, 153, 154, 155, 156, 157, *range(158, 170)]
