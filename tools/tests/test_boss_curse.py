"""core-rules' boss curse casts a fixed skills.txt id (#1918): a renumbered skills.txt would curse with another skill."""
import re
import unittest
from pathlib import Path

import data_tables

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "item" / "curses.cpp"


class BossCurseSkill(unittest.TestCase):
    def test_row_is_mon_amplify_damage(self):
        data_tables.need(self)
        row = int(re.search(r"SkillMonAmplifyDamage = (\d+);", SOURCE.read_text(encoding="utf-8")).group(1))
        self.assertEqual(data_tables.rows("skills")[row]["skill"], "MonAmplifyDamage")


if __name__ == "__main__":
    unittest.main()
