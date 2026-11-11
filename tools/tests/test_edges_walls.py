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
         "data/hd/env/model/act2/wall_low.model": LOW_BOX, "data/hd/env/model/act2/wall_low2.model": LOW_BOX, "data/hd/env/model/act1/wall_low.model": LOW_BOX}
WALL = ("act2/a/wall.dt1", 1, 0, 0)


def ctx(stock=(), legacy_keys=None):
    return W.Context(SimpleNamespace(keys=legacy_keys or {}), defaultdict(dict), BOXES.get, lambda p: [], list(stock))


def edges(**kinds):
    return {k: kinds.get(k, {}) for k in ("wall", "cliff", "fence", "pillar", "water", "treeline")}


class TrimTests(unittest.TestCase):
    def trim(self, loose, legacy):
        b = SimpleNamespace(entities=[], loose=loose)
        with mock.patch("pd2_tiles.legacy_edges", return_value=legacy):
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
            with mock.patch("pd2_tiles.legacy_edges", return_value=edges(wall={(0, 0): "low"})), \
                    mock.patch("pd2_tiles.tile_type", return_value="wall"), mock.patch.object(W, "walkable", return_value=set()):
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


class WallsRuleTests(unittest.TestCase):
    """A `walls:<PD2 DT1>` line (#826): the PD2 DT1's cells take pieces only off stock wall tiles of the D2R DT1."""
    PD2 = ("pd2assets/x.dt1", 1, 0, 0)

    def piece(self, folder, m):
        return {"dependencies": {}, "entities": [entity(f"{folder}/{m}", 26.0, 26.0)]}

    def tile(self, dt1):
        return SimpleNamespace(keys={(2, 2): ("w", ((dt1, 1, 0, 0),))})

    def run_pass(self, walls, kind="wall", own=None, extra=()):
        a, c = self.tile("act2/a/wall.dt1"), self.tile("act2/b/wall.dt1")
        lay = SimpleNamespace(keys={(0, 0): ("w", (self.PD2,))})
        mine = own if own is not None else [(a, self.piece("act2", "wall_low")), (c, self.piece("act2", "wall_low2"))]
        b = SimpleNamespace(seed="x.ds1", entities=[], loose=[], stock=mine, depend=lambda d: None)
        W.CONTEXT["x.ds1"] = ctx(mine + list(extra), lay.keys)
        W.CONTEXT["x.ds1"].legacy, W.CONTEXT["x.ds1"].walls = lay, walls
        try:
            with mock.patch("pd2_fidelity.legacy_edges", return_value=edges(**{kind: {(0, 0): "low"}})),                     mock.patch("pd2_fidelity.tile_type", return_value=kind), mock.patch.object(W, "walkable", return_value=set()):
                W.run(b)
        finally:
            W.CONTEXT.pop("x.ds1", None)
        return [W.model(e).split("/", 5)[-2:] and "/".join(W.model(e).split("/")[-2:]) for e in b.loose]

    def test_the_line_picks_the_piece_off_its_d2r_dt1(self):
        self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act2/b/wall.dt1"}), ["act2/wall_low2.model"])
        self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act2/a/wall.dt1"}), ["act2/wall_low.model"])

    def test_a_d2r_dt1_with_no_wall_piece_leaves_the_tile_bare(self):
        self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act2/c/wall.dt1"}), [])

    def test_a_line_for_another_dt1_changes_nothing(self):
        self.assertEqual(self.run_pass({"pd2assets/y.dt1": "act2/c/wall.dt1"}), self.run_pass({}))

    def test_a_line_fills_cliff_and_pillar_cells_on_a_map_with_no_wall_edges(self):
        for kind in ("cliff", "pillar"):
            self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act2/b/wall.dt1"}, kind), ["act2/wall_low2.model"], kind)
            self.assertEqual(self.run_pass({}, kind), [], kind)  # without the line the pass has no wall edges to fill

    def test_the_maps_own_stock_comes_before_other_families(self):
        other = (self.tile("act2/b/wall.dt1"), self.piece("act1", "wall_low"))
        self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act2/b/wall.dt1"}, extra=[other]), ["act2/wall_low2.model"])

    def test_a_line_takes_a_piece_of_another_family_when_the_own_stock_has_none(self):
        other = (self.tile("act1/z/wall.dt1"), self.piece("act1", "wall_low"))
        self.assertEqual(self.run_pass({"pd2assets/x.dt1": "act1/z/wall.dt1"}, extra=[other]), ["act1/wall_low.model"])


class CheckWallsTests(unittest.TestCase):
    """check_walls: a `walls:` line must have cells to fill and a wall piece to fill them with."""
    def context(self, tile_dt1="pd2assets/x.dt1", stock_dt1="act2/b/wall.dt1"):
        lay = SimpleNamespace(keys={(0, 0): ("w", ((tile_dt1, 1, 0, 0),))})
        stock = [(SimpleNamespace(keys={(2, 2): ("w", ((stock_dt1, 1, 0, 0),))}),
                  {"dependencies": {}, "entities": [entity("act2/wall_low", 26.0, 26.0)]})]
        return W.Context(lay, defaultdict(dict), BOXES.get, lambda p: [], stock)

    def check(self, walls, c):
        with mock.patch("pd2_fidelity.tile_type", return_value="cliff"),                 mock.patch("pd2_fidelity.model_type", return_value="wall"),                 mock.patch("pd2_fidelity.hd_class", return_value="low"),                 mock.patch("pd2_coverage.boxes", return_value=[(0, 3, 0, 3, 2, 3)]),                 mock.patch("pd2_coverage.footprint", return_value={(2, 2)}):
            W._INDEX.clear()
            W.check_walls(walls, [c])

    def test_a_line_with_cells_and_a_piece_passes(self):
        self.check({"pd2assets/x.dt1": "act2/b/wall.dt1"}, self.context())

    def test_a_pd2_dt1_with_no_cell_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "pd2assets/void.dt1"):
            self.check({"pd2assets/void.dt1": "act2/b/wall.dt1"}, self.context())

    def test_a_d2r_dt1_with_no_harvested_piece_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "act2/c/wall.dt1"):
            self.check({"pd2assets/x.dt1": "act2/c/wall.dt1"}, self.context())


class IndexTests(unittest.TestCase):
    def test_a_reused_id_never_returns_a_stale_index(self):
        a = [(SimpleNamespace(keys={}), {"entities": [], "dependencies": {}})]
        W._INDEX[tuple(id(p) for _, p in a)] = ((object(),), {1: ["stale"]})
        self.assertEqual(W.index(a, ctx()), {})


if __name__ == "__main__":
    unittest.main()
