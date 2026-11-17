"""Plugin code names a few objects.txt rows by number; each must still be the row of that name."""
import csv
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = [  # (source, constant, objects.txt Class)
    ("plugin/core-events/lucion.cpp", "DarkPortal", "xactdarkportal_grey"),
    ("plugin/core-events/lucion_chests.cpp", "LucionChest", "LucionChest"),
    ("plugin/core-events/map_events.cpp", "SacrificeAltar", "Sacrifice Altar"),
    ("plugin/core-events/map_events.cpp", "ShardAltar", "ForceShardAltar"),
]


class ObjectIds(unittest.TestCase):
    def test_constants_match_rows(self):
        with open(ROOT / "files/data/global/excel/objects.txt", encoding="latin-1", newline="") as f:
            ids = {r["Class"]: r["*ID"] for r in csv.DictReader(f, delimiter="\t")}
        for src, const, cls in CASES:
            m = re.search(rf"\b{const} = (\d+);", (ROOT / src).read_text("utf-8"))
            self.assertIsNotNone(m, const)
            self.assertEqual(m.group(1), ids[cls], f"{const} vs objects.txt {cls}")


if __name__ == "__main__":
    unittest.main()
