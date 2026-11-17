"""#952, #1287: the native catalogue is m01-m25, one map per Cartographer tileset, each on a slot the game check opened."""
import unittest
import csv
from pathlib import Path

import data_tables

CART = Path(__file__).resolve().parents[2] / "files/data/global/excel/d2rloader/cartographer"


def rows(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class CartographerCatalogueRowsTests(unittest.TestCase):
    def test_25_maps_one_per_tileset_on_distinct_items(self):
        maps = rows("maps.txt")
        assert [m["Item"] for m in maps] == [f"m{i:02d}" for i in range(1, 26)]
        assert len({m["Tileset"] for m in maps}) == len(maps) == len({m["Name"] for m in maps})


    def test_maps_are_medium_and_each_in_one_tier(self):
        """#1304: a map's tier type (t1m..t3m) sets its Level (87..89) and its tooltip's tier line; about 8 per tier."""
        data_tables.need(self)
        misc = {r["code"]: r for r in data_tables.rows("misc")}
        counts = {}
        for m in rows("maps.txt"):
            assert m["Size"] == "Medium" and m["MinCharLevel"] == "1", m["Name"]
            s = misc[m["Item"]]
            n = s["type"][1]
            assert s["type"] in ("t1m", "t2m", "t3m") and m["Level"] == str(86 + int(n)), m["Name"]
            assert (s["spelldesc"], s["spelldescstr"], s["spelldesccalc"]) == ("2", "MapTierValue", n), m["Name"]
            # the tooltip reads the tier from a stat calc: spelldesccalc alone showed "Tier: 0" in game
            assert (s["stat1"], s["calc1"]) == ("hpregen", n), m["Name"]
            counts[n] = counts.get(n, 0) + 1
        assert sorted(counts.values()) == [8, 8, 9], counts

    def test_tier_types_are_direct_children_of_cmap_and_no_map_is_always_magic(self):
        data_tables.need(self)
        types = {r["Code"]: r for r in data_tables.rows("itemtypes")}
        for c in ("cmap", "t1m", "t2m", "t3m"):
            r = types[c]
            assert (r["Magic"], r["Rare"], r["Normal"]) == ("0", "1", "0"), c
            assert c == "cmap" or (r["Equiv1"], r["Equiv2"]) == ("cmap", ""), c


    def test_every_tileset_family_has_a_slot_level(self):
        """Each map tileset (Cave, Crypt, Ice, Jungle, Snow) must name a Slot whose Level is the game level it opens in."""
        # game check 2026-11-15: Cave 138, Crypt 139, Ice 141, Jungle 142, Snow 143 (202-207 before #1231) opened and were built as planned.
        levels = {s["Slot"]: int(s["Level"]) for s in rows("slots.txt")}
        assert [levels[k] for k in ("Maze", "MazeCrypt", "MazeIce", "Outdoor", "OutdoorSnow")] == [138, 139, 141, 142, 143]
        # #1287: the other 20 tilesets, each with a map of its own (Lava 140, Siege 144, then the ids the installer gave the new levels)
        assert [levels[k] for k in ("MazeLava", "OutdoorSiege", "MazeDurance", "MazeArcane", "OutdoorMesa", "OutdoorDesert", "OutdoorWilderness", "OutdoorKurast") + tuple("Maze" + n for n in ("Dungeon", "Jail", "Lair", "KurastSewer", "Barracks", "Harem", "Basement", "Sewer", "Catacombs", "Tombs", "Spider", "Temple"))] == [140, 144, 152, 153, 154, 155, 156, 157, *range(158, 170)]
