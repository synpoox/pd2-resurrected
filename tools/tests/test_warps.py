"""The `cheat goto` coordinate formula (cheat-commands.md), tools/pd2_warps.py."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pd2_warps  # noqa: E402


class WarpFormula(unittest.TestCase):
    def test_ds1_tile(self):
        # PD2's Kurast map (144): OffsetX 3000, OffsetY 2550, tile (109,75) is `cheat goto 144,15547,13127`
        self.assertEqual(pd2_warps.subtile(3000, 109), 15547)
        self.assertEqual(pd2_warps.subtile(2550, 75), 13127)

    def test_hd_position(self):
        self.assertEqual(pd2_warps.hd_subtile(3000, 100), 15050)
        self.assertEqual(pd2_warps.hd_subtile(2550, 0), 12750)


if __name__ == "__main__":
    unittest.main()
