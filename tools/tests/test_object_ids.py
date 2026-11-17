"""Plugin code names objects.txt rows by number; each must still be the row of that name."""
import csv
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASES = [  # (source, constant, objects.txt Class), one entry per file that names the object
    ("plugin/core-events/lucion.cpp", "DarkPortal", "xactdarkportal_grey"),
    ("plugin/core-events/lucion_chests.cpp", "LucionChest", "LucionChest"),
    ("plugin/core-events/map_events.cpp", "SacrificeAltar", "Sacrifice Altar"),
    ("plugin/core-events/map_events.cpp", "ShardAltar", "ForceShardAltar"),
    ("plugin/core-events/map_events.cpp", "EventMarker", "empty_eventmarker"),
    ("plugin/core-events/map_events.cpp", "SpireAltar", "TaintedSunMapEventAltar"),
    ("plugin/core-events/map_events.cpp", "Fog", "Fog"),
    ("plugin/core-events/event_portals.h", "RedPortal", "PortalPermanent"),
    ("plugin/core-events/rathma.cpp", "GoldPortal", "xactportal_gold"),
    ("plugin/core-events/rathma.cpp", "GhostlyPortal", "xactportal_ghostly"),
    ("plugin/core-events/rathma_ai.cpp", "GhostlyPortal", "xactportal_ghostly"),
    ("plugin/core-events/uber_entry.cpp", "GreyPortal", "xactportal_grey"),
    ("plugin/core-events/uber_tristram.cpp", "RedPortal", "xactportal_red"),
]


class ObjectIds(unittest.TestCase):
    def rows(self):
        with open(ROOT / "files/data/global/excel/objects.txt", encoding="latin-1", newline="") as f:
            return list(csv.DictReader(f, delimiter="\t"))

    def test_id_is_row_index(self):  # D2R reads the row position; the Expansion line is no row
        rows = [r for r in self.rows() if r["Class"] != "Expansion"]
        for i, r in enumerate(rows):
            self.assertEqual(r["*ID"], str(i), r["Class"])

    def test_constants_match_rows(self):
        ids = {r["Class"]: r["*ID"] for r in self.rows()}
        for src, const, cls in CASES:
            m = re.search(rf"\b{const} = (\d+);", (ROOT / src).read_text("utf-8"))
            self.assertIsNotNone(m, f"{src} {const}")
            self.assertEqual(m.group(1), ids[cls], f"{src} {const} vs objects.txt {cls}")


if __name__ == "__main__":
    unittest.main()
