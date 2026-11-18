"""Every spawnable monstats Id (levels, superuniques, Cartographer map pools and bosses) has an exact-case key in hd/character/monsters.json (#1399)."""
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tests import data_tables as dt  # noqa: E402


def spawnable():
    ids = set()
    for lv in dt.rows("levels"):
        for k, v in lv.items():
            if v and (k.startswith(("mon", "nmon", "umon")) and k.rstrip("0123456789") in ("mon", "nmon", "umon")):
                ids.add(v)
    ids.update(r["Class"] for r in dt.rows("superuniques") if r["Class"])
    carto = "global/excel/d2rloader/cartographer/"
    for ln in dt.text(carto + "maps.txt").splitlines()[1:]:
        ids.update(i for i in ln.split("	")[-1].strip().split(",") if i)
    ids.update(ln.split("	")[1].strip() for ln in dt.text(carto + "bosses.txt").splitlines()[1:] if "	" in ln)
    return ids


class HdMonsterKeys(unittest.TestCase):
    def test_every_spawnable_id_has_exact_key(self):
        dt.need(self)
        keys = set(json.loads(dt.text("hd/character/monsters.json")))
        known = {r["Id"] for r in dt.rows("monstats")}
        missing = sorted(i for i in spawnable() if i in known and i not in keys)
        self.assertEqual(missing, [], "monsters.json lacks exact-case keys")


if __name__ == "__main__":
    unittest.main()
