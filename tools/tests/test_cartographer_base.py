"""The #946 native-map data base: maps.txt names real items and slots, the ids are gapless (docs/cartographer/contracts.md)."""
import unittest
import csv
from pathlib import Path

import data_tables

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


class CartographerBaseTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_every_map_names_a_cmap_misc_item(self):
        items = {r["code"] for r in data_tables.rows("misc") if r["type"] in ("t1m", "t2m", "t3m")}
        for m in table("maps.txt"):
            assert m["Item"] in items, m["Name"]


    def test_every_slot_is_a_level_and_ids_are_gapless(self):
        levels = set(ids("levels"))
        for s in table("slots.txt"):
            assert int(s["Level"]) in levels, s["Slot"]
        for t in ("levels", "itemstatcost", "itemtypes", "magicprefix", "magicsuffix", "rareprefix", "raresuffix"):
            added = ids(t, plus=True)
            first = 138 if t == "levels" else max(ids(t, plus=False)) + 1  # levels: D2R's own rows end at 137, which the list does not name (#1231)
            assert added == list(range(first, first + len(added))), t



    def test_map_pool_is_tierless(self):
        sets = [r for r in data_tables.rows("treasureclassex") if r["Item9"] in ("m01", "Cartographer Maps")]
        assert [r["Treasure Class"] for r in sets] == [f"Act {a} (H) Good" for a in range(1, 6)] and all(r["Item9"] == "Cartographer Maps" for r in sets)  # 070 set m01, 071 superseded it with the pool
        assert not any("Map Tier" in v for r in data_tables.rows("treasureclassex") for v in r.values())
