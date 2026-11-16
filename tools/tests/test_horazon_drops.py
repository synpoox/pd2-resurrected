"""Horazon drops six native maps and has no tier pool, map-stat or unique-map path (#959)."""
import unittest
from pathlib import Path

TEXT = (Path(__file__).resolve().parents[2] / "plugin" / "core-events" / "horazon.cpp").read_text(encoding="utf-8")


class HorazonDrops(unittest.TestCase):
    def test_six_native_rolls(self):
        self.assertIn('MapPool = "Cartographer Maps"', TEXT)
        self.assertRegex(TEXT, r"MapDrops = 6;")

    def test_no_old_paths(self):
        for old in ("Map Tier", "ApplyMapArea", "DropUniqueMap", "OpenPortal", "map_stats.h"):
            self.assertNotIn(old, TEXT)

    def test_portal_uses_open_extra_service(self):
        self.assertIn('#include "cartographer_open_extra_v1.h"', TEXT)
        self.assertIn("carto::OpenExtraServiceName", TEXT)
        self.assertIn("openExtra(", TEXT)
        self.assertIn("result.structSize = sizeof(result);", TEXT)


if __name__ == "__main__":
    unittest.main()
