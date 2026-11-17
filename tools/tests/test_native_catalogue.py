"""#948 (lane A): the small verified native catalogue, its one pool and its deterministic table generation."""
import unittest
import csv
import json
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]
CART = ROOT / "files/data/global/excel/d2rloader/cartographer"
# Every tileset Cartographer supports (d2r-tools plugins/cartographer README, OutdoorTilesets and MazeTilesets), with the
# level its slot opens. #1287 added Lava, Durance, Arcane, Siege, Mesa, Desert, Wilderness and Kurast.
VERIFIED = {"Cave": 138, "Crypt": 139, "Ice": 141, "Jungle": 142, "Snow": 143, "Lava": 140, "Siege": 144,
            "Durance": 152, "Arcane": 153, "Mesa": 154, "Desert": 155, "Wilderness": 156, "Kurast": 157,
            "Dungeon": 158, "Jail": 159, "Lair": 160, "KurastSewer": 161, "Barracks": 162, "Harem": 163, "Basement": 164, "Sewer": 165, "Catacombs": 166, "Tombs": 167, "Spider": 168, "Temple": 169}
SLOT = {"Cave": "Maze", "Crypt": "MazeCrypt", "Ice": "MazeIce", "Jungle": "Outdoor", "Snow": "OutdoorSnow",
        "Lava": "MazeLava", "Siege": "OutdoorSiege", "Durance": "MazeDurance", "Arcane": "MazeArcane",
        "Mesa": "OutdoorMesa", "Desert": "OutdoorDesert", "Wilderness": "OutdoorWilderness", "Kurast": "OutdoorKurast",
        "Dungeon": "MazeDungeon", "Jail": "MazeJail", "Lair": "MazeLair", "KurastSewer": "MazeKurastSewer", "Barracks": "MazeBarracks", "Harem": "MazeHarem", "Basement": "MazeBasement", "Sewer": "MazeSewer", "Catacombs": "MazeCatacombs", "Tombs": "MazeTombs", "Spider": "MazeSpider", "Temple": "MazeTemple"}
POOL = "Cartographer Maps"


def rows(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


STRINGS = json.loads((ROOT / "merge/strings.json").read_text("utf-8"))


class NativeCatalogueTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_one_map_per_tileset_and_none_other(self):
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
        tcs = {r["Treasure Class"]: r for r in data_tables.rows("treasureclassex")}

        def flat(name):  # (item, weight): a TC row holds 10 items, so the pool nests "Cartographer Maps B" at the weight of its maps
            r = tcs[name]
            assert r["Picks"] == "1"
            out = []
            for i in range(1, 11):
                if r[f"Item{i}"] in tcs:
                    sub = flat(r[f"Item{i}"])
                    assert int(r[f"Prob{i}"]) == len(sub)
                    out += sub
                elif r[f"Item{i}"]:
                    assert r[f"Prob{i}"] == "1"
                    out.append(r[f"Item{i}"])
            return out
        assert flat(POOL) == [m["Item"] for m in rows("maps.txt")]
        good = tcs
        assert all(good[f"Act {a} (H) Good"]["Item9"] == POOL for a in range(1, 6))
        assert not any("Map Tier" in v for r in good.values() for v in r.values())


    def test_native_properties_exist(self):
        props = {r["code"] for r in data_tables.rows("properties")}
        assert {"map-level", "mon-life", "mon-damage", "mon-speed", "map-density"} <= props
