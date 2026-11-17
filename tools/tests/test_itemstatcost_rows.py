"""The plugins' ItemStatCostRows (common/pd2_engine.h) is the row count of itemstatcost.txt: the map stack compare reads stats 0..rows-1 (#1326)."""
import re
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]


class ItemStatCostRows(unittest.TestCase):
    def test_constant_matches_table(self):
        data_tables.need(self)
        header = (ROOT / "plugin/common/pd2_engine.h").read_text(encoding="utf-8")
        constant = int(re.search(r"ItemStatCostRows\s*=\s*(\d+)", header).group(1))
        self.assertEqual(constant, len(data_tables.rows("itemstatcost")))


if __name__ == "__main__":
    unittest.main()
