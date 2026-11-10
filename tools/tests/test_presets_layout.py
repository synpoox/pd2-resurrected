"""pd2_presets: the Layout, DT1 and waypoint readers."""
import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets_fixtures import A, B, FLOOR, TERRAIN, at, cell, dt1, ds1, entity, stock  # noqa: E402,F401


class LayoutTests(unittest.TestCase):
    def test_tiles_are_named_by_the_first_dt1_holding_them(self):
        data = ds1(2, 1, [{(1, 0): (1, 2, 3)}, {(1, 0): (9, 9, 9)}], [{(0, 0): (0, 1), (1, 0): (0, 1)}],
                   ["\\d2\\data\\global\\tiles\\act1\\a\\wall.tg1", "DS1EDIT_SAVE_COUNT=2"])
        self.assertEqual(P.header_dt1s(data), ["act1/a/wall.dt1"])
        dt1s = {"act1/b/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)])), "act1/a/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)])),
                "act1/a/wall.dt1": P.dt1_tiles(dt1([(1, 2, 3)]))}
        lay = P.layout(data, dt1s)
        self.assertEqual((lay.width, lay.height), (2, 1))
        self.assertEqual(lay.keys, {(0, 0): ("f", (FLOOR,)), (1, 0): ("w", (A, (None, 9, 9, 9)))})

    def test_a_tile_keeps_its_subtile_flags_and_a_cells_unwalkable_bit(self):
        data = ds1(3, 1, [{(1, 0): (1, 2, 3)}], [{(0, 0): (0, 1), (2, 0): (0, 1)}])
        at = len(data) - 4 - 3 * 4 - 3 * 4 + 2 * 4  # the floor cell of (2, 0), before the shadow layer
        data = data[:at] + struct.pack("<I", cell(0, 1) | P.UNWALKABLE) + data[at + 4:]
        lay = P.layout(data, {"act1/a/wall.dt1": P.dt1_tiles(dt1([(1, 2, 3)], {(1, 2, 3)})),
                              "act1/a/floor.dt1": P.dt1_tiles(dt1([(0, 0, 1)]))})
        self.assertEqual(lay.flags, {(0, 0): bytes(25), (1, 0): b"\1" * 25, (2, 0): bytes(25)})
        self.assertEqual(lay.keys[(2, 0)], ("f", (FLOOR,)))
        self.assertEqual(lay.unwalkable, {(2, 0)})
