"""Stash affinity's code lists (plugin/common/stash_home.h) match misc.txt (#1553)."""
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402

HEADER = Path(__file__).resolve().parents[2] / "plugin" / "common" / "stash_home.h"


def lists() -> dict[str, list[str]]:
    text = HEADER.read_text(encoding="utf-8")
    found = re.findall(r"const char\* (\w+)\[\] = \{([^}]*)\}", text)
    return {name: re.findall(r'"(\w+)"', body) for name, body in found}


class StashAffinity(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.misc = {r["code"]: r for r in D.rows("misc") if r.get("code")}
        self.lists = lists()

    def test_tiers_are_the_tables_maps(self):
        for tier in (1, 2, 3):
            want = sorted(c for c, r in self.misc.items() if r["type"] == f"t{tier}m")
            self.assertEqual(self.lists[f"Tier{tier}"], want)
            self.assertLessEqual(len(want), 10)  # one row per base on a 10 x 10 page

    def test_codes_exist_once(self):
        codes = [c for name in ("Tier1", "Tier2", "Tier3", "MapMaterials", "Bossing") for c in self.lists[name]]
        self.assertEqual(len(codes), len(set(codes)))
        for code in codes:
            self.assertIn(code, self.misc)

    def test_bossing_leaves_the_rotw_materials_tab_alone(self):
        for code in self.lists["MapMaterials"] + self.lists["Bossing"]:
            self.assertIn(self.misc[code]["AdvancedStashStackable"], ("", "0"), code)


if __name__ == "__main__":
    unittest.main()
