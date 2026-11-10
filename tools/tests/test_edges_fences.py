"""The fence pass (#736, passes/edges_fences.py): fence tiles stand as low D2R fence pieces along their run, gates at orientations 8 and 9."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_coverage as coverage  # noqa: E402
from presets.layout import Layout  # noqa: E402
from presets.passes import edges_fences as fences  # noqa: E402

FENCE = "expansion/siege/fence.dt1"
STONE = "act4/mesa/chain_walls.dt1"
FOREGATE = "act4/fort/foregate.dt1"
WALL = "act1/barracks/basewall.dt1"


def build(keys: dict, loose: list, rest=None) -> SimpleNamespace:
    deps = []
    return SimpleNamespace(lay=Layout(20, 20, keys), loose=loose, rest=keys if rest is None else rest, seed="s",
                           depend=deps.append, deps=deps)


def stock_piece(x: float, z: float, name: str = "wall_stone_large01") -> dict:
    return {"name": name, "components": [{"type": "TransformDefinitionComponent",
                                          "position": {"x": x, "y": 0.0, "z": z}}]}


def at(e: dict) -> tuple[float, float]:
    p = next(c for c in e["components"] if c["type"] == "TransformDefinitionComponent")["position"]
    return p["x"], p["z"]


class FenceTests(unittest.TestCase):
    def test_a_run_gets_one_low_piece_per_tile_turned_along_it(self):
        keys = {(x, 5): ("w", ((FENCE, 2, 0, 0),)) for x in range(3, 8)}
        b = build(keys, [stock_piece(45, 55)])
        fences.run(b)
        self.assertEqual(len(b.loose), 5)  # the stock pick on (4, 5) is gone
        self.assertNotIn("wall_stone_large01", [e["name"] for e in b.loose])
        for e in b.loose:
            q = next(c for c in e["components"] if c["type"] == "TransformDefinitionComponent")["orientation"]
            self.assertEqual(q["y"], 0.0)  # along x
            model = next(c for c in e["components"] if c["type"] == "ModelDefinitionComponent")["filename"]
            box = fences.kit()[next(k for k, v in fences.kit().items() if v["model"] == model)]["box"]
            self.assertLess(box[4], coverage.TALL)  # a low piece hides no walkable ground

    def test_a_column_of_fence_runs_along_z(self):
        keys = {(5, y): ("w", ((FENCE, 1, 0, 0),)) for y in range(3, 8)}
        b = build(keys, [])
        fences.run(b)
        self.assertEqual({e["components"][0]["orientation"]["y"] for e in b.loose}, {-0.707106769})

    def test_a_tall_foregate_is_no_fence(self):
        keys = {(x, 5): ("w", ((FOREGATE, 2, 0, 0),)) for x in range(3, 8)}
        b = build(keys, [stock_piece(45, 55)])
        fences.run(b)
        self.assertEqual([e["name"] for e in b.loose], ["wall_stone_large01"])  # its stock wall stays

    def test_a_wooden_fence_never_takes_a_rock_or_stone_wall(self):
        keys = {(x, 5): ("w", ((FENCE, 2, 0, 0),)) for x in range(0, 12)}
        b = build(keys, [])
        fences.run(b)
        models = {next(c for c in e["components"] if c["type"] == "ModelDefinitionComponent")["filename"]
                  for e in b.loose}
        self.assertTrue(models and all("spider_fences" in m for m in models), models)

    def test_a_stone_fence_takes_stone_pieces(self):
        keys = {(x, 5): ("w", ((STONE, 2, 0, 0),)) for x in range(0, 12)}
        b = build(keys, [])
        fences.run(b)
        models = {next(c for c in e["components"] if c["type"] == "ModelDefinitionComponent")["filename"]
                  for e in b.loose}
        self.assertTrue(models and all(("stone_fences" in m or "tomb_fences" in m) for m in models), models)

    def test_a_lone_tile_goes_by_its_orientation(self):
        def yaw(o: int) -> float:
            b = build({(5, 5): ("w", ((FENCE, o, 0, 0),))}, [])
            fences.run(b)
            return b.loose[0]["components"][0]["orientation"]["y"]
        self.assertEqual({o: yaw(o) for o in (1, 2, 3, 4, 5, 6, 7)},
                         {1: -0.707106769, 2: 0.0, 3: 0.0, 4: -0.707106769, 5: -0.707106769, 6: 0.0, 7: 0.0})

    def test_gate_orientations_take_the_end_cap(self):
        keys = {(x, 5): ("w", ((STONE, 8 if x == 3 else 2, 0, 0),)) for x in range(3, 6)}
        b = build(keys, [])
        fences.run(b)
        models = [next(c for c in e["components"] if c["type"] == "ModelDefinitionComponent")["filename"]
                  for e in sorted(b.loose, key=at)]
        self.assertEqual(models[0], fences.kit()[fences.GATE["stone"]]["model"])
        self.assertNotIn(fences.kit()[fences.GATE["stone"]]["model"], models[1:])

    def test_a_fence_beside_another_wall_keeps_the_kits_pick(self):
        keys = {(5, 5): ("w", ((FENCE, 2, 0, 0), (WALL, 1, 0, 0)))}
        b = build(keys, [stock_piece(55, 55)])
        fences.run(b)
        self.assertEqual([e["name"] for e in b.loose], ["wall_stone_large01"])

    def test_a_tile_outside_the_kit_fill_and_other_pieces_stay(self):
        keys = {(5, 5): ("w", ((FENCE, 2, 0, 0),)), (6, 5): ("w", ((FENCE, 2, 0, 0),))}
        b = build(keys, [stock_piece(55, 55), stock_piece(25, 25, "chair")], rest={(5, 5): keys[(5, 5)]})
        fences.run(b)
        self.assertEqual(sorted(e["name"] for e in b.loose), ["chair", "pd2r_fence"])

    def test_a_piece_is_centred_on_its_tile(self):
        b = build({(5, 5): ("w", ((FENCE, 2, 0, 0),))}, [])
        fences.run(b)
        k = next(v for v in fences.kit().values() if v["model"] == next(
            c for c in b.loose[0]["components"] if c["type"] == "ModelDefinitionComponent")["filename"])
        x, z = at(b.loose[0])
        self.assertAlmostEqual(x + (k["box"][0] + k["box"][3]) / 2, 55.0, places=2)
        self.assertAlmostEqual(z + (k["box"][2] + k["box"][5]) / 2, 55.0, places=2)

    def test_the_kit_models_are_named_in_the_dependencies(self):
        b = build({(5, 5): ("w", ((FENCE, 2, 0, 0),))}, [])
        fences.run(b)
        paths = {d["path"] for ds in b.deps for v in ds.values() for d in v}
        self.assertTrue(any(p.endswith(".model") for p in paths))


if __name__ == "__main__":
    unittest.main()
