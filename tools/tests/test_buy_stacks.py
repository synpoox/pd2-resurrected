"""Vendor buy merge (#1916): core-rules' MergeTypes rows are the itemtypes PD2's list names."""
import re
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]


class BuyStacks(unittest.TestCase):
    def test_merge_type_rows(self):
        data_tables.need(self)
        source = (ROOT / "plugin/core-rules/buy_stacks.cpp").read_text(encoding="utf-8")
        rows = [int(n) for n in re.search(r"MergeTypes\[\] = \{([^}]*)\}", source).group(1).split(",")]
        scroll = int(re.search(r"ScrollType = (\d+)", source).group(1))
        # the game's type number skips the "Expansion" divider row
        types = [t for t in data_tables.rows("itemtypes") if t["ItemType"] != "Expansion"]
        self.assertEqual(types[scroll]["Code"], "scro")
        self.assertEqual([types[r]["Code"] for r in rows],
                         ["box", "imma", "imrn", "imra", "rera", "scou", "upma", "ubor", "mgen", "rng"])


if __name__ == "__main__":
    unittest.main()
