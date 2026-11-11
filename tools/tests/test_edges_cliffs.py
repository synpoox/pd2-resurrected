"""presets.passes.edges_cliffs: stock cliff pieces along the legacy cliff edges."""
import sys
import unittest
from collections import defaultdict
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

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


SMALL = (0, 0, 0, 3, 6, 3)  # model box: tall, inside one tile, on the ground
BOXES = {f"data/hd/env/model/{f}/{m}.model": b for f in ("act3_kurast_walls", "act4_mesa_cliff_supports")
         for m, b in (("cliff_a", SMALL), ("cliff_hang", (0, 3, 0, 3, 9, 3)))}
LEGACY = ("w", (("act3/travincal/terraces.dt1", 1, 0, 0),))
STOCK_TILE = ("w", (("act3/kurast/walls.dt1", 1, 0, 0),))


class RunPassTests(unittest.TestCase):
    """run(): which legacy cliff cells get a piece, and from what."""
    def run_pass(self, folder="act3_kurast_walls", model="cliff_a", seed="Expansion/Map/RiverOfBlood2.ds1",
                 legacy=LEGACY, present=None):
        stock_lay = SimpleNamespace(keys={(2, 2): STOCK_TILE})
        preset = {"dependencies": {}, "entities": [entity(f"{folder}/{model}", 26.0, 26.0)]}
        lay = SimpleNamespace(keys={(0, 0): legacy})
        b = SimpleNamespace(seed=seed, entities=present or [], loose=[], stock=[(stock_lay, preset)],
                            depend=lambda d: None)
        W.CONTEXT[seed] = W.Context(lay, defaultdict(dict), BOXES.get, lambda p: [], [(stock_lay, preset)])
        try:
            with mock.patch("pd2_tiles.legacy_edges", return_value={**{k: {} for k in fid.EDGES}, "cliff": {(0, 0): "tall"}}),                     mock.patch.object(C.walls, "walkable", return_value=set()), mock.patch.object(C, "walkable", return_value=set()):
                C.run(b)
        finally:
            W.CONTEXT.pop(seed, None)
        return [W.model(e).split("/")[-2] + "/" + W.model(e).split("/")[-1] for e in b.loose]

    def test_a_bare_terrace_cell_gets_an_act_3_stone_piece(self):
        self.assertEqual(self.run_pass(), ["act3_kurast_walls/cliff_a.model"])

    def test_a_map_outside_the_opt_in_changes_nothing(self):
        self.assertEqual(self.run_pass(seed="Expansion/Map/Throne1.ds1"), [])
        self.assertEqual(self.run_pass(seed="Expansion/Map/ZharKurast.ds1"), [])

    def test_a_dt1_outside_the_opt_in_changes_nothing(self):
        self.assertEqual(self.run_pass(legacy=("w", (("act1/outdoors/cliff1.dt1", 1, 0, 0),))), [])

    def test_only_act_3_stone_pieces_are_used_and_the_cell_stays_bare(self):
        self.assertEqual(self.run_pass(folder="act4_mesa_cliff_supports"), [])
        self.assertEqual(C.LEFT["riverofblood2"], {(0, 0)})

    def test_a_piece_that_hangs_off_its_ground_is_never_used(self):
        self.assertEqual(self.run_pass(model="cliff_hang"), [])

    def test_a_cell_a_voted_cliff_piece_covers_gets_no_second_piece(self):
        here = entity("act3_kurast_walls/cliff_a", 5.0, 5.0)  # the stock vote counts act3_kurast_walls as a cliff
        self.assertEqual(self.run_pass(present=[here]), [])


class VoteTests(unittest.TestCase):
    def test_the_generators_vote_reading_equals_the_scorers(self):
        import pd2_fidelity as scorer
        text = C.VOTES.read_text("utf-8")
        self.assertEqual(C.stock_types(), scorer.stock_types(scorer.read_votes(text)))
        self.assertEqual((C.STOCK_MIN, C.STOCK_SHARE, C.STOCK_EDGE), (scorer.STOCK_MIN, scorer.STOCK_SHARE, scorer.STOCK_EDGE))

    def test_the_stone_folders_are_cliffs_by_the_vote(self):
        self.assertEqual({C.stock_types().get(f) for f in C.STONE}, {"cliff"})


if __name__ == "__main__":
    unittest.main()
