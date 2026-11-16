"""Every playable class row in charstats.txt carries the Force Move (Move Only) skill."""
import csv
import unittest
from pathlib import Path

CHARSTATS = Path(__file__).resolve().parents[2] / "files/data/global/excel/charstats.txt"


class ForceMoveTests(unittest.TestCase):
    def test_every_class_has_force_move(self):
        with open(CHARSTATS, encoding="utf-8-sig", newline="") as f:
            rows = list(csv.DictReader(f, delimiter="\t"))
        classes = [r for r in rows if r["class"] != "Expansion"]
        self.assertIn("Warlock", [r["class"] for r in classes])
        for r in classes:
            skills = [v for k, v in r.items() if k.startswith("Skill ")]
            self.assertIn("Force Move", skills, r["class"])


if __name__ == "__main__":
    unittest.main()
