"""core-events' MaxLevelState is a fixed row (#1838): a renumbered states.txt would put another state on level 99 players."""
import re
import unittest
from pathlib import Path

import data_tables

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-events" / "max_level_aura.cpp"


class MaxLevelState(unittest.TestCase):
    def test_constant_is_the_maxlevelset_row(self):
        data_tables.need(self)
        row = int(re.search(r"MaxLevelState = (\d+);", SOURCE.read_text(encoding="utf-8")).group(1))
        self.assertEqual(data_tables.rows("states")[row]["state"], "maxlevelset")


if __name__ == "__main__":
    unittest.main()
