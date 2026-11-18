"""Every spawnable monstats Id (levels, superuniques, Cartographer maps, bosses, arena spots, minions) has an exact-case key in hd/character/monsters.json (#1399)."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tests import data_tables as dt  # noqa: E402

CARTO = "global/excel/d2rloader/cartographer/"


def carto_rows(name):
    lines = dt.text(CARTO + name + ".txt").splitlines()
    head = lines[0].split("\t")
    return [dict(zip(head, ln.split("\t"))) for ln in lines[1:] if ln.strip()]


def spawnable():
    ids = set()
    for lv in dt.rows("levels"):
        for k, v in lv.items():
            if v and k.rstrip("0123456789") in ("mon", "nmon", "umon"):
                ids.add(v)
    ids.update(r["Class"] for r in dt.rows("superuniques") if r["Class"])
    for r in carto_rows("maps"):
        ids.update(i for i in r["Monsters"].split(",") if i)
    ids.update(r["Base"] for r in carto_rows("bosses") if r["Base"])
    ids.update(r["Monster"] for r in carto_rows("arenaspots") if r["Monster"])
    stats = {r["Id"]: r for r in dt.rows("monstats")}
    todo = list(ids)
    while todo:  # minions spawn with their leader
        r = stats.get(todo.pop())
        for m in (r["minion1"], r["minion2"]) if r else ():
            if m and m not in ids:
                ids.add(m)
                todo.append(m)
    return ids


class HdMonsterKeys(unittest.TestCase):
    def test_every_spawnable_id_has_exact_key(self):
        dt.need(self)
        keys = set(json.loads(dt.text("hd/character/monsters.json")))
        known = {r["Id"] for r in dt.rows("monstats")}
        lower = {k.lower() for k in known}
        ids = spawnable()
        near = sorted(i for i in ids if i not in known and i.lower() in lower)
        self.assertEqual(near, [], "references that differ from a monstats Id only by case")
        missing = sorted(i for i in ids if i in known and i not in keys)
        self.assertEqual(missing, [], "monsters.json lacks exact-case keys")


if __name__ == "__main__":
    unittest.main()
