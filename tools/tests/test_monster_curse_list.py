"""core-rules' srvdo 112 looks its curses up by name (#2003): a renamed or dropped row would leave the knights silent."""
import re
import unittest
from pathlib import Path

import data_tables

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "skill" / "monster_curse.cpp"


class MonsterCurseList(unittest.TestCase):
    def test_names_are_curse_rows(self):
        data_tables.need(self)
        names = re.findall(r'"(\w+)"', re.search(r"Curses\[\] = \{([^}]*)\};", SOURCE.read_text(encoding="utf-8")).group(1))
        self.assertEqual(len(names), 4)
        rows = {r["skill"]: r for r in data_tables.rows("skills")}
        for name in names:
            self.assertIn(name, rows)
            self.assertTrue(rows[name]["auratargetstate"], name)
            self.assertTrue(rows[name]["aurastat1"], name)


if __name__ == "__main__":
    unittest.main()
