"""Item Quantity (#1596): the stat id core-events reads, its property and its "Item Quantity +N%" string."""
import json
import re
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]


class ItemQuantityStat(unittest.TestCase):
    def test_stat_property_and_string(self):
        data_tables.need(self)
        header = (ROOT / "plugin/common/pd2_engine.h").read_text(encoding="utf-8")
        stat_id = re.search(r"StatItemQuantity\s*=\s*(\d+)", header).group(1)
        stat = [r for r in data_tables.rows("itemstatcost") if r["Stat"] == "item_quantity"]
        self.assertEqual([r["*ID"] for r in stat], [stat_id])
        self.assertEqual((stat[0]["descfunc"], stat[0]["descstrpos"]), ("19", "ModItemQuantity"))
        props = [r for r in data_tables.rows("properties") if r["code"] == "item-quantity"]
        self.assertEqual([(r["func1"], r["stat1"]) for r in props], [("1", "item_quantity")])
        strings = json.loads((data_tables.DATA / "local/lng/strings/item-modifiers.json").read_text(encoding="utf-8-sig"))
        self.assertEqual([s["enUS"] for s in strings if s["Key"] == "ModItemQuantity"], ["Item Quantity %+d%%"])


if __name__ == "__main__":
    unittest.main()
