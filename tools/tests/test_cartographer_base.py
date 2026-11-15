"""The #946 native-map data base: maps.txt names real items and slots, the ids are gapless (docs/cartographer-contracts.md)."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CART = ROOT / "files/data/global/excel/d2rloader/cartographer"


def table(name):
    with open(CART / name, newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def ids(table_name):
    out = []
    for line in (ROOT / "merge" / f"ids-{table_name}.tsv").read_text().splitlines():
        if line and not line.startswith("# "):
            if line.split("\t")[1] != "-":
                out.append(int(line.split("\t")[1]))
    return out


def patch_ops():
    return json.loads((ROOT / "patches/070-cartographer-base.json").read_text())["ops"]


def test_every_map_names_a_patched_misc_item():
    items = {o["set"]["code"] for o in patch_ops() if o.get("table") == "misc"}
    for m in table("maps.txt"):
        assert m["Item"] in items, m["Name"]


def test_every_slot_is_a_level_and_ids_are_gapless():
    levels = set(ids("levels"))
    for s in table("slots.txt"):
        assert int(s["Level"]) in levels, s["Slot"]
    for t in ("levels", "itemstatcost", "itemtypes"):
        got = ids(t)
        assert len(set(got)) == len(got) and set(range(min(got), max(got) + 1)) >= set(got), t


def test_map_pool_is_tierless():
    sets = [o for o in patch_ops() if o.get("table") == "treasureclassex" and o["op"] == "set"]
    assert len(sets) == 5 and all(o["set"] == {"Item9": "Cartographer Maps"} for o in sets)
