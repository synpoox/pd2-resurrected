"""core-rules' srvdo 112 casts fixed skills.txt ids (#2003): PD2's raw 424, 420, 585, 427, resolved by name."""
import re
import unittest
from pathlib import Path

import data_tables

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "skill_functions.cpp"
NAMES = ["MonAmplifyDamage", "MonWeaken", "MonLowerRes", "MonDecrepify"]


class MonsterCurseList(unittest.TestCase):
    def test_rows_are_pd2_mon_curses(self):
        data_tables.need(self)
        ids = re.search(r"MonCurses\[\] = \{([\d, ]+)\};", SOURCE.read_text(encoding="utf-8")).group(1)
        rows = data_tables.rows("skills")
        self.assertEqual([rows[int(i)]["skill"] for i in ids.split(",")], NAMES)


if __name__ == "__main__":
    unittest.main()
