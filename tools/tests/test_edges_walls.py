"""presets.passes.edges_walls: stock wall pieces along the legacy wall edges."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets.passes import edges_walls as W  # noqa: E402
from presets_fixtures import entity  # noqa: E402

OPT = W.Option(entity("wall_a", 3.0, 4.0), 3.0, 4.0, 0.0, "tall", ((0, 0), (1, 0)), {})
LOW = W.Option(entity("wall_b", 3.0, 4.0), 3.0, 4.0, 0.0, "low", ((0, 0),), {})


class PickTests(unittest.TestCase):
    def test_the_option_of_the_asked_class_is_picked(self):
        self.assertIs(W.pick([OPT, LOW], "low", (5, 5), {(5, 5)}, set(), set(), "s"), LOW)
        self.assertIs(W.pick([OPT, LOW], "tall", (5, 5), {(5, 5)}, set(), set(), "s"), OPT)

    def test_a_piece_over_walkable_floor_is_never_used(self):
        self.assertIsNone(W.pick([OPT], "tall", (5, 5), {(5, 5)}, set(), {(6, 5)}, "s"))

    def test_the_piece_covering_most_bad_tiles_wins(self):
        two = W.Option(entity("wall_c", 3.0, 4.0), 3.0, 4.0, 0.0, "tall", ((0, 0), (1, 0)), {})
        one = W.Option(entity("wall_d", 3.0, 4.0), 3.0, 4.0, 0.0, "tall", ((0, 0),), {})
        self.assertIs(W.pick([one, two], "tall", (5, 5), {(5, 5), (6, 5)}, set(), set(), "s"), two)
        self.assertIs(W.pick([one, two], "tall", (5, 5), {(5, 5), (6, 5)}, {(6, 5)}, set(), "s") in (one, two), True)

    def test_ground_is_flat_until_the_height_pass_lands(self):
        self.assertEqual(W.wall_y(None, 1.0, 2.0), 0.0)

    def test_a_build_with_no_context_runs_no_wall_pass(self):
        class B:
            seed = "no/such.ds1"
            loose = [1]
        b = B()
        W.run(b)
        self.assertEqual(b.loose, [1])


if __name__ == "__main__":
    unittest.main()
