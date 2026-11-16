"""#948 (lane A): the small verified native catalogue, its one pool and its deterministic table generation."""
import unittest
import csv
import json
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]
CART = ROOT / "files/data/global/excel/d2rloader/cartographer"
# Tilesets seen opening, reaching the boss and returning in game (d2r-tools docs/cartographer/testing.md). Lava and
# Siege are deferred: Lava's edges and boss were not settled, Siege was not run in game.
VERIFIED = {"Cave": 202, "Crypt": 203, "Ice": 205, "Jungle": 206, "Snow": 207}
SLOT = {"Cave": "Maze", "Crypt": "MazeCrypt", "Ice": "MazeIce", "Jungle": "Outdoor", "Snow": "OutdoorSnow"}
POOL = "Cartographer Maps"


def rows(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


STRINGS = json.loads((ROOT / "merge/strings.json").read_text("utf-8"))


class NativeCatalogueTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_one_map_per_verified_tileset_and_none_other(self):
        maps = rows("maps.txt")
        assert sorted(m["Tileset"] for m in maps) == sorted(VERIFIED)
        slots = {s["Slot"]: int(s["Level"]) for s in rows("slots.txt")}
        for m in maps:
            assert slots[SLOT[m["Tileset"]]] == VERIFIED[m["Tileset"]], m["Name"]
            assert m["Level"] == "85" and m["Size"] == "Medium", m["Name"]


    def test_every_map_item_is_complete_and_unique(self):
        misc = {r["code"]: r for r in data_tables.rows("misc") if r["type"] == "cmap"}
        names = {k for k, (_, f) in STRINGS["ids"].items() if f == "item-names.json" and k in STRINGS["texts"]}
        hd = {k for e in json.loads(data_tables.text("hd/items/items.json")) for k in e}
        codes = [m["Item"] for m in rows("maps.txt")]
        assert len(set(codes)) == len(codes) == len(misc)
        for c in codes:  # the contracts' frozen consumer item: cmap, pSpell 16, useable, no belt
            s = misc[c]
            assert (s["type"], s["pSpell"], s["useable"], s["belt"], s["compactsave"]) == ("cmap", "16", "1", "0", "0")
            assert c in names and c in hd


    def test_pool_is_one_tierless_row_shared_by_every_normal_source(self):
        (s,) = [r for r in data_tables.rows("treasureclassex") if r["Treasure Class"] == POOL]
        items = [s[f"Item{i}"] for i in range(1, 11) if s[f"Item{i}"]]
        assert items == [m["Item"] for m in rows("maps.txt")]
        assert all(s[f"Prob{i}"] == "1" for i in range(1, len(items) + 1)) and s["Picks"] == "1"
        good = {r["Treasure Class"]: r for r in data_tables.rows("treasureclassex")}
        assert all(good[f"Act {a} (H) Good"]["Item9"] == POOL for a in range(1, 6))
        assert not any("Map Tier" in v for r in good.values() for v in r.values())


    def test_native_properties_exist(self):
        props = {r["code"] for r in data_tables.rows("properties")}
        assert {"map-level", "mon-life", "mon-damage", "mon-speed", "map-density"} <= props
