"""#948 (lane A): the small verified native catalogue, its one pool and its deterministic table generation."""
import unittest
import csv
import json
from pathlib import Path

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


def ops(patch):
    return json.loads((ROOT / "patches" / patch).read_text())["ops"]


STRINGS = json.loads((ROOT / "merge/strings.json").read_text("utf-8"))
ALL = ops("070-cartographer-base.json") + ops("071-native-catalogue.json")


class NativeCatalogueTests(unittest.TestCase):
    def test_one_map_per_verified_tileset_and_none_other(self):
        maps = rows("maps.txt")
        assert sorted(m["Tileset"] for m in maps) == sorted(VERIFIED)
        slots = {s["Slot"]: int(s["Level"]) for s in rows("slots.txt")}
        for m in maps:
            assert slots[SLOT[m["Tileset"]]] == VERIFIED[m["Tileset"]], m["Name"]
            assert m["Level"] == "85" and m["Size"] == "Medium", m["Name"]


    def test_every_map_item_is_complete_and_unique(self):
        misc = {o["set"]["code"]: o["set"] for o in ALL if o.get("table") == "misc"}
        names = {k for k, (_, f) in STRINGS["ids"].items() if f == "item-names.json" and k in STRINGS["texts"]}
        hd = {o["code"] for o in ALL if o["op"] == "item_hd"}
        codes = [m["Item"] for m in rows("maps.txt")]
        assert len(set(codes)) == len(codes) == len(misc)
        for c in codes:  # the contracts' frozen consumer item: cmap, pSpell 16, useable, no belt
            s = misc[c]
            assert (s["type"], s["pSpell"], s["useable"], s["belt"], s["compactsave"]) == ("cmap", "16", "1", "0", "0")
            assert c in names and c in hd


    def test_pool_is_one_tierless_row_shared_by_every_normal_source(self):
        (pool,) = [o for o in ALL if o["op"] == "set" and o["set"].get("Treasure Class") == POOL]
        s = pool["set"]
        items = [s[f"Item{i}"] for i in range(1, 11) if s[f"Item{i}"]]
        assert items == [m["Item"] for m in rows("maps.txt")]
        assert all(s[f"Prob{i}"] == "1" for i in range(1, len(items) + 1)) and s["Picks"] == "1"
        good = [o for o in ALL if o["op"] == "set" and o.get("key", "").endswith("(H) Good") and "Item9" in o["set"]]
        assert [o["key"] for o in good[-5:]] == [f"Act {a} (H) Good" for a in range(1, 6)]
        assert all(o["set"] == {"Item9": POOL} for o in good[-5:])  # only Item9: unrelated loot is untouched
        assert not any("Map Tier" in json.dumps(o) for o in ops("071-native-catalogue.json"))


    def test_native_properties_exist(self):
        props = {o["values"]["code"] for o in ALL if o["op"] == "add_row" and o["table"] == "properties"}
        assert {"map-level", "mon-life", "mon-damage", "mon-speed", "map-density"} <= props


    def test_patch_is_deterministic_and_adds_no_ids(self):
        first = (ROOT / "patches/071-native-catalogue.json").read_text()
        assert json.dumps(json.loads(first), indent=1) == first  # regenerating is a no-op
        assert not any(o.get("table") in ("itemstatcost", "magicprefix", "magicsuffix", "rareprefix", "raresuffix") for o in ops("071-native-catalogue.json"))


    def test_no_patch_string_op(self):
        # modkit's Strings reads D2R's pristine file, so one string op replaces the merged copy and drops every PD2 name (#996)
        assert not [p.name for p in (ROOT / "patches").glob("*.json") if any(o["op"] == "string" for o in ops(p.name))]
