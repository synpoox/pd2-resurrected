"""Each map's icon colour matches its tier (#1344): Level 87 white, 88 gold, 89 red; icons unique, with a cartoicon_white_01 copy."""
import csv
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXCEL = ROOT / "files/data/global/excel"
PD2 = ROOT / "files/data/hd/items/misc/pd2"
COLOUR = {"87": "white", "88": "gold", "89": "red"}


def rows(path):
    with open(path, newline="", encoding="latin-1") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class MapIconTiers(unittest.TestCase):
    def test_icon_colour_matches_tier(self):
        tier = {r["Item"]: COLOUR[r["Level"]] for r in rows(EXCEL / "d2rloader/cartographer/maps.txt")}
        misc = {r["code"]: r["invfile"] for r in rows(EXCEL / "misc.txt") if r["code"] in tier}
        items = {k: v["asset"] for e in json.loads((ROOT / "files/data/hd/items/items.json").read_text("utf-8")) for k, v in e.items() if k in tier}
        ice = (PD2 / "cartoicon_white_01.json").read_bytes()
        self.assertEqual(set(misc), set(tier))
        for m, c in tier.items():
            self.assertRegex(misc[m], rf"^cartoicon_{c}_\d\d$", m)
            self.assertEqual(items[m], "pd2/" + misc[m], m)
            self.assertEqual((PD2 / (misc[m] + ".json")).read_bytes(), ice, m)
        self.assertEqual(len(set(misc.values())), len(misc), "icons must be unique")


if __name__ == "__main__":
    unittest.main()
