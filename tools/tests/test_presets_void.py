"""presets.void and the void passes: where legacy draws nothing, HD lays no ground (#783)."""
import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import void  # noqa: E402
from presets.build import Build  # noqa: E402
from presets.passes import void as cover, void_kit  # noqa: E402
from presets_fixtures import FLOOR, TERRAIN, dt1, entity  # noqa: E402

SPAN = P.TERRAIN_SIZE // P.TILE
PIT = ("act1/a/pit.dt1", 0, 0, 2)  # a floor tile with no blocks
MODEL = TERRAIN["components"][1]["filename"]


def plane(bx: int, bz: int) -> dict:
    e = copy.deepcopy(TERRAIN)
    e["components"][0]["position"].update({"x": float(bx * P.TERRAIN_SIZE), "z": float(bz * P.TERRAIN_SIZE)})
    return e


def black() -> tuple[dict, dict]:
    e = entity("act4/fort/act4_fort_stairs/black_square01", 0.0, 0.0)
    return e, {"models": [{"path": void.BLACK}]}


class Stub(Build):
    def __init__(self, lay, **kw):
        super().__init__(lay, None, "t", "t.ds1", "", TERRAIN, [], None, None, {}, [])
        for k, v in kw.items():
            setattr(self, k, v)


class DrawnTests(unittest.TestCase):
    def test_a_dt1_tile_is_drawn_when_it_has_blocks(self):
        raw = bytearray(dt1([(0, 0, 1), (0, 0, 2)]))
        raw[276 + 80:276 + 84] = (3).to_bytes(4, "little")  # the first tile's block count
        self.assertEqual(void.drawn_tiles(bytes(raw)), {(0, 0, 1)})

    def test_the_blank_dt1_filler_draws_nothing_though_it_has_blocks(self):
        import tempfile
        raw = bytearray(dt1([(0, 0, 1)]))
        raw[276 + 80:276 + 84] = (25).to_bytes(4, "little")
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "act1/outdoors/blank.dt1"
            f.parent.mkdir(parents=True)
            f.write_bytes(bytes(raw))
            (f.parent / "other.dt1").write_bytes(bytes(raw))
            drawn = void.Drawn(Path(d))
            self.assertEqual(drawn["act1/outdoors/blank.dt1"], frozenset())
            self.assertEqual(drawn["act1/outdoors/other.dt1"], {(0, 0, 1)})

    def test_empty_cells_are_no_layer_and_floor_that_draws_nothing_but_never_walkable_or_wall(self):
        drawn = {"act1/a/floor.dt1": frozenset({FLOOR[1:]}), "act1/a/pit.dt1": frozenset()}
        keys = {(0, 0): ("f", (FLOOR,)), (1, 0): ("f", (PIT,)), (2, 0): ("f", ((None, 0, 0, 3),)),
                (3, 0): ("w", (PIT[:1] + (1, 1, 1),)), (4, 0): ("f", (FLOOR, PIT)), (5, 0): ("f", (PIT,))}
        lay = P.Layout(7, 1, keys, {c: bytes(25) for c in keys})
        lay.unwalkable = frozenset({(0, 0), (1, 0), (2, 0), (3, 0), (4, 0)})  # (5, 0) alone is walkable
        # (0, 0) is drawn; (1, 0) and (2, 0) draw nothing; (3, 0) is a wall; (4, 0) has one drawn layer; (5, 0) walks
        self.assertEqual(void.empty(lay, drawn), {(1, 0), (2, 0), (6, 0)})


class PassTests(unittest.TestCase):
    def setUp(self):
        void.STOCK[:] = [black()]
        self.addCleanup(void.STOCK.clear)
        self.addCleanup(void.CELLS.clear)

    def build(self, cells, walk, planes=((0, 0),), size=SPAN * 2, **kw):
        lay = P.Layout(size, size, {c: ("f", (FLOOR,)) for c in walk}, {c: bytes(25) for c in walk})
        b = Stub(lay, **kw)
        b.entities = [plane(*p) for p in planes if p != (0, 0)]
        void.configure("t.ds1", frozenset(cells))
        cover.run(b)
        return b

    def covers(self, b):
        return [e for e in b.entities if e["name"] == void.BLACK_PIECE]

    def test_empty_tiles_by_walkable_ground_are_covered_in_one_rectangle(self):
        b = self.build({(x, y) for x in range(3, 6) for y in range(2, 4)}, {(2, 2), (2, 3)})
        (e,) = self.covers(b)
        t = e["components"][0]
        self.assertAlmostEqual(t["scale"]["x"], (30 + cover.RIM) / 100)
        self.assertAlmostEqual(t["scale"]["z"], (20 + cover.RIM) / 100)
        self.assertEqual(t["position"]["y"], cover.LIFT)
        self.assertFalse([c for c in e["components"] if c["type"] == "PhysicsBodyDefinitionComponent"])
        self.assertIn(void.BLACK, b.deps["models"])

    def test_the_cover_centres_the_model_on_its_tiles(self):
        b = self.build({(3, 2)}, {(2, 2)})
        t = self.covers(b)[0]["components"][0]
        sx = (P.TILE + cover.RIM) / 100
        self.assertAlmostEqual(t["position"]["x"] + sx * cover.CENTRE[0], 3.5 * P.TILE)

    def test_empty_tiles_far_from_walkable_ground_are_not_covered(self):
        b = self.build({(2 + void.SIGHT + 1, 2)}, {(2, 2)})
        self.assertEqual(self.covers(b), [])

    def test_pasted_room_tiles_are_not_covered(self):
        b = self.build({(3, 2)}, {(2, 2)}, taken={(3, 2)})
        self.assertEqual(self.covers(b), [])

    def test_a_plane_over_a_wholly_empty_block_is_dropped_unless_something_stands_on_it(self):
        block = {(x, y) for x in range(SPAN, 2 * SPAN) for y in range(SPAN)}  # block (1, 0)
        b = self.build(block, {(2, 2)}, planes=((0, 0), (1, 0)))
        self.assertFalse([e for e in b.entities if e["name"] == "terrain"])
        self.assertEqual(self.covers(b), [])  # nothing is drawn under: no cover
        b = self.build(block, {(2, 2)}, planes=((0, 0), (1, 0)))
        b2 = Stub(b.lay)
        b2.entities = [plane(1, 0), entity("rock", SPAN * 10 + 5.0, 5.0)]
        cover.run(b2)
        self.assertTrue([e for e in b2.entities if e["name"] == "terrain"])

    def test_a_block_with_one_drawn_tile_keeps_its_plane_and_covers_the_rest_near_walkable(self):
        block = {(x, y) for x in range(SPAN, 2 * SPAN) for y in range(SPAN)} - {(SPAN, 0)}
        b = self.build(block, {(SPAN - 1, 3)}, planes=((0, 0), (1, 0)))
        self.assertTrue([e for e in b.entities if e["name"] == "terrain"])
        self.assertTrue(self.covers(b))

    def test_no_cells_no_change(self):
        b = self.build(set(), {(2, 2)}, planes=((0, 0), (1, 0)))
        self.assertEqual(len(b.entities), 1)

    def test_the_kit_leaves_empty_tiles_bare_but_not_a_rooms(self):
        keys = {(0, 0): ("f", (FLOOR,)), (1, 0): ("f", (FLOOR,)), (2, 0): ("f", (FLOOR,))}
        b = Stub(P.Layout(3, 1, keys, {}), rest=dict(keys), taken={(2, 0)})
        void.configure("t.ds1", frozenset({(1, 0), (2, 0)}))
        void_kit.run(b)
        self.assertEqual(set(b.rest), {(0, 0), (2, 0)})


if __name__ == "__main__":
    unittest.main()
