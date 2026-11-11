"""presets: the hung cliff pass (#831)."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from presets.consts import ROOM_PIECE  # noqa: E402
from presets.passes import cliff_rise, edges_walls  # noqa: E402
from presets_fixtures import entity  # noqa: E402

BOXES = {"data/hd/env/model/act4/mesa/cliff_small01.model": (-7, -25.3, -3, 7, 0.6, 5),
         "data/hd/env/model/act4/mesa/wall_stone01.model": (-7, -2.0, -3, 7, 67.0, 5),
         "data/hd/env/model/act4/mesa/cliff_tall01.model": (-7, -25.3, -3, 7, 30.0, 5),
         "data/hd/env/model/act4/mesa/cliff_flat01.model": (-7, -0.5, -3, 7, 0.6, 5)}


def build(name="realmoftorment1", ents=(), taken=frozenset()):
    return SimpleNamespace(name=name, seed=name + ".ds1", entities=list(ents), taken=set(taken))


def run(b, boxed=True):
    edges_walls.CONTEXT.clear()
    if boxed:
        edges_walls.configure(b.seed, edges_walls.Context(None, None, lambda m: BOXES.get(m), None, []))
    try:
        cliff_rise.run(b)
    finally:
        edges_walls.CONTEXT.clear()


def y(e):
    return e["components"][0]["position"]["y"]


class CliffRiseTests(unittest.TestCase):
    def test_a_hung_cliff_piece_stands_on_its_ground(self):
        e = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        run(build(ents=[e]))
        self.assertAlmostEqual(y(e), 25.3)

    def test_the_lift_adds_to_the_plane_height(self):
        e = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        e["components"][0]["position"]["y"] = 6.0
        run(build(ents=[e]))
        self.assertAlmostEqual(y(e), 31.3)

    def test_the_scale_scales_the_lift(self):
        e = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        e["components"][0]["scale"]["y"] = 2.0
        run(build(ents=[e]))
        self.assertAlmostEqual(y(e), 50.6)

    def test_walls_tall_cliffs_and_flat_pieces_stay(self):
        ents = [entity(n, 15.0, 15.0) for n in ("act4/mesa/wall_stone01", "act4/mesa/cliff_tall01", "act4/mesa/cliff_flat01")]
        run(build(ents=ents))
        self.assertEqual([y(e) for e in ents], [0.0, 0.0, 0.0])

    def test_a_pasted_room_keeps_its_pieces(self):
        room = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        room["name"] = ROOM_PIECE + "x"
        taken = entity("act4/mesa/cliff_small01", 25.0, 25.0)
        run(build(ents=[room, taken], taken={(2, 2)}))
        self.assertEqual((y(room), y(taken)), (0.0, 0.0))

    def test_other_maps_and_builds_without_boxes_stay(self):
        other = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        run(build("mesa1", [other]))
        self.assertEqual(y(other), 0.0)
        bare = entity("act4/mesa/cliff_small01", 15.0, 15.0)
        run(build(ents=[bare]), boxed=False)
        self.assertEqual(y(bare), 0.0)


if __name__ == "__main__":
    unittest.main()
