"""Lane B's generic helpers (tc_roll, corruption_roll, cube_ops, lane-b/) include no map-lane header (#949)."""
import re
import unittest
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "plugin" / "core-events"
LANE_B = ["tc_roll.cpp", "tc_roll.h", "corruption_roll.cpp", "corruption_roll.h", "cube_ops.cpp", "cube_ops.h"]
# lucion.h: cube_ops runs the Temple's Desecration search trigger (lane D's `Desecrate`), by design.
ALLOWED = {"tc_roll.h", "corruption_roll.h", "cube_ops.h", "pd2_engine.h", "log.h", "parts.h", "lucion.h"}


class LaneBSeparation(unittest.TestCase):
    def test_no_map_header_included(self):
        files = [SRC / n for n in LANE_B] + sorted((SRC / "lane-b").glob("*"))
        for f in files:
            for inc in re.findall(r'#include "([^"]+)"', f.read_text(encoding="utf-8")):
                self.assertIn(inc, ALLOWED, f"{f.name} includes {inc}")


if __name__ == "__main__":
    unittest.main()
