"""void_knight_missile.cpp's merged monstats ids name the four void knights (#2231)."""
import re
import unittest
from pathlib import Path

import data_tables as d

SRC = Path(__file__).resolve().parents[2] / "plugin/core-rules/skill/void_knight_missile.cpp"
NAMES = {"KnightPoison": "voidKnightPoison", "KnightFire": "voidKnightFire",
         "KnightCold": "voidKnightCold", "KnightPhys": "voidKnightPhys"}


class VoidKnightIds(unittest.TestCase):
    def test_ids_match_names(self):
        d.need(self)
        ids = [r["Id"] for r in d.rows("monstats") if r["Id"] != "Expansion"]  # D2R skips the Expansion row
        found = dict(re.findall(r"(Knight\w+) = (\d+)", SRC.read_text()))
        for const, name in NAMES.items():
            self.assertEqual(ids[int(found[const])], name, const)


if __name__ == "__main__":
    unittest.main()
