"""pd2_terrain on a synthetic terrain.physics, and the kit keeping a piece's height above its stock ground."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_presets as P  # noqa: E402
import pd2_terrain as T  # noqa: E402
from presets_fixtures import entity  # noqa: E402


def physics(verts: list[tuple[float, float, float]], tris: list[tuple[int, int, int]]) -> bytes:
    """A terrain.physics with no BVH nodes: the header, the vertex table (4 f32 each), the triangle table (8 u32)."""
    head = 0x30
    vt = head
    tt = vt + 16 * len(verts)
    out = bytearray(tt + 32 * len(tris))
    struct.pack_into("<3Q", out, 0, head, vt, tt)
    struct.pack_into("<3I", out, 0x18, 0, len(verts), len(tris))
    for i, v in enumerate(verts):
        struct.pack_into("<4f", out, vt + 16 * i, *v, 0.0)
    for i, t in enumerate(tris):
        struct.pack_into("<8I", out, tt + 32 * i, *t, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0, 0xFFFF)
    return bytes(out)


# a 20 x 10 ground: x 0-10 flat at y 0, x 10-20 a terrace at y 5 (two triangles each)
STEP = physics([(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10), (10, 5, 0), (20, 5, 0), (20, 5, 10), (10, 5, 10)],
               [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)])


class GroundTests(unittest.TestCase):
    def test_the_height_is_the_triangle_over_the_point_and_none_off_the_mesh(self):
        g = T.Ground(STEP)
        self.assertAlmostEqual(g.at(3, 4), 0.0)
        self.assertAlmostEqual(g.at(15, 4), 5.0)
        self.assertIsNone(g.at(25, 4))

    def test_a_piece_stands_on_the_surface_under_it_not_an_overhead_layer(self):
        # x 0-10: ground at 0 and a rampart deck at 70 over it
        g = T.Ground(physics([(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10),
                              (0, 70, 0), (10, 70, 0), (10, 70, 10), (0, 70, 10)],
                             [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)]))
        self.assertAlmostEqual(g.at(3, 4), 70.0)
        self.assertAlmostEqual(g.at(3, 4, 6.0), 0.0)  # a wall's foot
        self.assertAlmostEqual(g.at(3, 4, 70.5), 70.0)  # a piece on the deck
        self.assertAlmostEqual(g.at(3, 4, -3.0), 0.0)  # sunk below every surface: the lowest

    def test_a_presets_ground_comes_from_its_terrains_physics_file_at_the_terrains_position(self):
        with tempfile.TemporaryDirectory() as tmp:
            d2r = Path(tmp)
            (d2r / "hd/env/preset/x").mkdir(parents=True)
            (d2r / "hd/env/preset/x/terrain.physics").write_bytes(STEP)
            preset = {"terrain": {"components": [
                {"type": "TransformDefinitionComponent", "position": {"x": 0.0, "y": -2.0, "z": 0.0}},
                {"type": "PhysicsBodyDefinitionComponent", "fixturedefs": [
                    {"shapetype": {"filename": "data/hd/env/preset/x/terrain.physics"}}]}]}}
            self.assertAlmostEqual(T.of_preset(preset, d2r).at(15, 4), 3.0)
            self.assertIsNone(T.of_preset({"terrain": {"components": []}}, d2r))


class KitTests(unittest.TestCase):
    def test_a_piece_on_a_stock_terrace_keeps_its_height_above_the_terrace_not_above_zero(self):
        lay = P.Layout(2, 1, {(0, 0): ("f", (("a.dt1", 0, 0, 0),)), (1, 0): ("f", (("a.dt1", 0, 0, 1),))})
        low, high = entity("fern", 4.0, 4.0), entity("wall", 14.0, 4.0)
        high["components"][0]["position"]["y"] = 6.0  # one unit above the terrace
        preset = {"dependencies": {}, "entities": [low, high], P.GROUND: T.Ground(STEP)}
        kit = P.Kit()
        kit.add(lay, preset, set(lay.keys.values()))
        ys = {k[1][0][3]: [P._transform(e)["position"]["y"] for e, _, _ in v[0][1]] for k, v in kit.instances.items()}
        self.assertEqual(ys, {0: [0.0], 1: [1.0]})
        self.assertEqual(high["components"][0]["position"]["y"], 6.0)  # the stock preset itself is untouched
        json.dumps({k: v for k, v in preset.items() if k != P.GROUND})

    def test_a_floor_tiles_piece_far_above_its_ground_is_dropped_but_a_wall_tiles_kept(self):
        lay = P.Layout(2, 1, {(0, 0): ("f", (("a.dt1", 0, 0, 0),)), (1, 0): ("w", (("a.dt1", 1, 0, 0),))})
        roof, beam, post = entity("roof", 4.0, 4.0), entity("beam", 14.0, 4.0), entity("post", 15.0, 4.0)
        roof["components"][0]["position"]["y"] = P.FLOOR_MAX + 1  # over flat ground at 0
        beam["components"][0]["position"]["y"] = 5 + P.FLOOR_MAX + 1  # over the terrace at 5
        kit = P.Kit(top=lambda e: 30.0 if e["name"] == "post" else None)  # the post holds the beam up (hanging.py)
        kit.add(lay, {"dependencies": {}, "entities": [roof, beam, post], P.GROUND: T.Ground(STEP)},
                set(lay.keys.values()))
        self.assertEqual({k[0]: [e["name"] for e, _, _ in v[0][1]] for k, v in kit.instances.items()},
                         {"f": [], "w": ["beam", "post"]})
        patch = ("sq", (None,) * 4 + (lay.keys[(0, 0)],) + (None,) * 4)
        self.assertTrue(P._floor(patch))
        self.assertFalse(P._floor(("cross", (lay.keys[(1, 0)],) + (None,) * 4)))


if __name__ == "__main__":
    unittest.main()
