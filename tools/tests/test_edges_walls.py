"""presets.passes.edges_walls: stock wall pieces along the legacy wall edges."""
import sys
import unittest
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

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
        east = W.Option(entity("wall_e", 3.0, 4.0), 3.0, 4.0, 0.0, "tall", ((1, 0),), {})
        self.assertIs(W.pick([one, east], "tall", (5, 5), {(5, 5), (6, 5)}, {(5, 5)}, set(), "s"), east)

    def test_ground_is_the_stock_ground_floor_height_lifts_from(self):
        self.assertEqual(W.wall_y(None, 1.0, 2.0), 0.0)

    def test_a_build_with_no_context_runs_no_wall_pass(self):
        class B:
            seed = "no/such.ds1"
            loose = [1]
        b = B()
        W.run(b)
        self.assertEqual(b.loose, [1])


TALL_BOX = (0, 0, 0, 15, 6, 4)  # model box: x0, y0, z0, x1, y1, z1: tall, two tiles wide
LOW_BOX = (0, 0, 0, 3, 2, 3)  # low, inside one tile
BOXES = {"data/hd/env/model/wall_tall.model": TALL_BOX, "data/hd/env/model/tree_a.model": TALL_BOX,
         "data/hd/env/model/act2/wall_low.model": LOW_BOX, "data/hd/env/model/act1/wall_low.model": LOW_BOX}
WALL = ("act2/a/wall.dt1", 1, 0, 0)


def ctx(stock=(), legacy_keys=None):
    return W.Context(SimpleNamespace(keys=legacy_keys or {}), defaultdict(dict), BOXES.get, lambda p: [], list(stock))


def edges(**kinds):
    return {k: kinds.get(k, {}) for k in ("wall", "cliff", "fence", "pillar", "water", "treeline")}


class TrimTests(unittest.TestCase):
    def trim(self, loose, legacy):
        b = SimpleNamespace(entities=[], loose=loose)
        with mock.patch("pd2_fidelity.legacy_edges", return_value=legacy):
            return W.trim(b, ctx(), legacy["wall"])

    def test_a_tall_piece_over_only_low_tiles_is_trimmed(self):
        tall = entity("wall_tall", 1.0, 1.0)
        self.assertEqual(self.trim([tall], edges(wall={(0, 0): "low", (1, 0): "low"})), [])

    def test_a_tall_piece_over_a_tall_tile_stays(self):
        tall = entity("wall_tall", 1.0, 1.0)
        self.assertEqual(self.trim([tall], edges(wall={(0, 0): "low", (1, 0): "tall"})), [tall])

    def test_the_only_cover_of_a_treeline_tile_stays(self):
        tall = entity("wall_tall", 1.0, 1.0)
        legacy = edges(wall={(0, 0): "low"}, treeline={(1, 0): None})
        self.assertEqual(self.trim([tall], legacy), [tall])

    def test_a_treeline_tile_another_piece_covers_does_not_keep_it(self):
        tall, tree = entity("wall_tall", 1.0, 1.0), entity("tree_a", 1.0, 1.0)
        legacy = edges(wall={(0, 0): "low"}, treeline={(1, 0): None})
        self.assertEqual(self.trim([tall, tree], legacy), [tree])


class RunTests(unittest.TestCase):
    def build(self, own_folder, stock_folder):
        mine = SimpleNamespace(keys={(2, 2): ("w", (WALL,))})
        low = lambda f: {"dependencies": {}, "entities": [entity(f"{f}/wall_low", 26.0, 26.0)]}
        lay = SimpleNamespace(keys={(0, 0): ("w", (WALL,))})
        b = SimpleNamespace(seed="x.ds1", entities=[], loose=[], stock=[(mine, low(own_folder))], depend=lambda d: None)
        W.CONTEXT["x.ds1"] = ctx([(mine, low(own_folder)), (mine, low(stock_folder))], lay.keys)
        W.CONTEXT["x.ds1"].legacy = lay
        return b

    def run_pass(self, b):
        try:
            with mock.patch("pd2_fidelity.legacy_edges", return_value=edges(wall={(0, 0): "low"})), \
                    mock.patch("pd2_fidelity.tile_type", return_value="wall"), mock.patch.object(W, "walkable", return_value=set()):
                W.run(b)
        finally:
            W.CONTEXT.pop("x.ds1", None)
        return [W.model(e) for e in b.loose]

    def test_a_bare_wall_tile_gets_a_piece_of_the_asked_class_from_the_maps_own_stock(self):
        self.assertEqual(self.run_pass(self.build("act2", "act1")), ["data/hd/env/model/act2/wall_low.model"])

    def test_another_acts_wall_is_never_the_fallback(self):
        b = self.build("act2", "act1")
        b.stock = []  # the tile set's own stock has no piece: only the map's own family may fill, and it has none
        self.assertEqual(self.run_pass(b), [])


class IndexTests(unittest.TestCase):
    def test_a_reused_id_never_returns_a_stale_index(self):
        a = [(SimpleNamespace(keys={}), {"entities": [], "dependencies": {}})]
        W._INDEX[tuple(id(p) for _, p in a)] = ((object(),), {1: ["stale"]})
        self.assertEqual(W.index(a, ctx()), {})


if __name__ == "__main__":
    unittest.main()
