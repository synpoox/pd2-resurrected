"""presets.passes.edges_cliffs: stock cliff pieces along the legacy cliff edges."""
import sys
import unittest
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
import pd2_tiles as fid  # noqa: E402
from presets.passes import edges_cliffs as C  # noqa: E402
from presets.passes import edges_walls as W  # noqa: E402
from presets_fixtures import entity  # noqa: E402

TERRACE = "pd2assets/act3/travincal/terraces.dt1"


class OrientationTests(unittest.TestCase):
    def test_a_lone_upper_tile_is_indexed_by_its_orientation(self):
        self.assertEqual(C.orientation(((TERRACE, 3, 0, 0),), fid), 3)

    def test_a_cliff_cell_takes_its_upper_tiles_orientation(self):
        self.assertEqual(C.orientation(((TERRACE, 17, 0, 0), (TERRACE, 2, 0, 0)), fid), 2)

    def test_a_lone_lower_tile_or_floor_is_no_index(self):
        self.assertIsNone(C.orientation(((TERRACE, 17, 0, 0),), fid))
        self.assertIsNone(C.orientation(((TERRACE, 0, 0, 0),), fid))

    def test_a_legacy_cliff_cell_asks_for_its_upper_orientation(self):
        heights = defaultdict(dict)
        self.assertEqual(C.cell_orientation(((TERRACE, 18, 0, 0), (TERRACE, 3, 0, 0)), heights, fid), 3)

    def test_a_lone_lower_cliff_tile_borrows_the_upper_ones(self):
        heights = defaultdict(dict)
        self.assertEqual(C.cell_orientation(((TERRACE, 16, 0, 0),), heights, fid), 1)


class RunTests(unittest.TestCase):
    def test_a_build_with_no_context_runs_no_cliff_pass(self):
        b = SimpleNamespace(seed="no/such.ds1", loose=[1])
        C.run(b)
        self.assertEqual(b.loose, [1])

    def test_the_pass_runs_after_the_wall_pass_and_before_floor_height(self):
        from presets.passes import floor_height
        self.assertGreater(C.ORDER, W.ORDER)
        self.assertLess(C.ORDER, floor_height.ORDER)


if __name__ == "__main__":
    unittest.main()
