"""The #946 native-map data base: maps.txt names real items and slots, the ids are gapless (docs/cartographer-contracts.md)."""
import unittest
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CART = ROOT / "files/data/global/excel/d2rloader/cartographer"


def table(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def ids(table_name, plus=None):
    """Ids of the list's lines: all, or only the port's `+` lines (plus=True) or only PD2/D2R rows (plus=False)."""
    out = []
    for line in (ROOT / "merge" / f"ids-{table_name}.tsv").read_text().splitlines():
        if line and not line.startswith("# ") and (plus is None or line.startswith("+") == plus):
            n = line.split("	")[1]
            if n != "-":
                out.append(int(n))
    return out


def patch_ops():
    """The base patch plus lane A's catalogue patch (#948), which supersedes its pool."""
    return [o for f in ("070-cartographer-base.json", "071-native-catalogue.json") for o in json.loads((ROOT / "patches" / f).read_text())["ops"]]


class CartographerBaseTests(unittest.TestCase):
    def test_every_map_names_a_patched_misc_item(self):
        items = {o["set"]["code"] for o in patch_ops() if o.get("table") == "misc"}
        for m in table("maps.txt"):
            assert m["Item"] in items, m["Name"]


    def test_every_slot_is_a_level_and_ids_are_gapless(self):
        levels = set(ids("levels"))
        for s in table("slots.txt"):
            assert int(s["Level"]) in levels, s["Slot"]
        for t in ("levels", "itemstatcost", "itemtypes", "magicprefix", "magicsuffix", "rareprefix", "raresuffix"):
            added = ids(t, plus=True)
            assert added == list(range(max(ids(t, plus=False)) + 1, max(ids(t, plus=False)) + 1 + len(added))), t



    def test_map_pool_is_tierless(self):
        sets = [o for o in patch_ops() if o.get("table") == "treasureclassex" and o["op"] == "set" and o["set"].get("Item9")]
        assert len(sets) == 10 and all(o["set"]["Item9"] in ("m01", "Cartographer Maps") for o in sets)
        assert not any("Map Tier" in json.dumps(o) for o in sets)
