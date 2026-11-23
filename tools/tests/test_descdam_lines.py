"""PD2's skill tooltip damage lines (#2005): which skilldesc rows use which descdam id, and the ids core-ui's descdam part
fills. 28 moves to D2R's 24 (the same line) as data; 11, 13, 15, 25 and 26 keep PD2's id and core-ui prints PD2's line."""
import re
import unittest
from pathlib import Path

import data_tables as d

ROOT = Path(__file__).resolve().parents[2]

WANT = {
    "vengeance": "11",
    "double throw": "13", "Split Throw": "13",
    "dragon talon": "15", "dragon flight": "15",
    "dragon tail": "16",
    "blade fury": "24", "blade sentinel": "24",
    "tiger strike": "25", "fists of fire": "25", "cobra strike": "25", "claws of thunder": "25",
    "blades of ice": "25", "royal strike": "25",
    "magic arrow": "26", "fire arrow": "26", "cold arrow": "26",
}


class DescDamLines(unittest.TestCase):
    def test_rows_use_the_mapped_ids(self):
        d.need(self)
        desc = {r["skilldesc"]: r["descdam"] for r in d.rows("skilldesc")}
        for row, want in WANT.items():
            self.assertEqual(desc[row], want, row)
        # every row on a filled id is one of PD2's: no D2R row would print a PD2 line
        hooked = {"11", "13", "15", "25", "26"}
        self.assertEqual({k for k, v in desc.items() if v in hooked}, {k for k, v in WANT.items() if v in hooked})

    def test_core_ui_fills_ids_11_13_15_25_26(self):
        text = (ROOT / "plugin" / "core-ui" / "descdam_lines.h").read_text(encoding="utf-8")
        ids = re.search(r"Ids\[\]\s*=\s*\{([^}]*)\}", text).group(1)
        self.assertEqual([int(x) for x in ids.split(",")], [11, 13, 15, 25, 26])
        plugin = (ROOT / "plugin" / "core-ui" / "plugin.cpp").read_text(encoding="utf-8")
        self.assertIn('{"descdam", true, descdam_lines::Install', plugin)
        cpp = (ROOT / "plugin" / "core-ui" / "descdam_lines.cpp").read_text(encoding="utf-8")
        self.assertIn('"pd2 lines on"', cpp)


if __name__ == "__main__":
    unittest.main()
