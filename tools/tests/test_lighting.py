"""The lighting pass (#773, passes/lighting.py) and the brightness measure (pd2_lighting.py)."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_lighting as measure  # noqa: E402
from presets.consts import TILE  # noqa: E402
from presets.layout import Layout  # noqa: E402
from presets.passes import floor_height, lighting  # noqa: E402

DARK = "data/hd/env/biome/expansion_baallair.json"
FLOOR = ("act1/a/floor.dt1", 0, 0, 1)


def build(biome: str, keys: dict) -> SimpleNamespace:
    deps = []
    return SimpleNamespace(lay=Layout(30, 20, keys), biome=biome, entities=[], deps=deps,
                           depend=lambda of: deps.extend(d["path"] for ds in of.values() for d in ds))


def light(x: float, z: float, radius: float = 25.0, kind: str = "PointLightDefinitionComponent") -> dict:
    return {"components": [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z}},
                           {"type": kind, "radius": radius}]}


def prop(x: float, z: float) -> dict:
    return {"components": [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z}}]}


class PassTests(unittest.TestCase):
    def test_an_indoor_map_in_a_dark_biome_gets_one_box_over_the_whole_map(self):
        b = build(DARK, {(0, 0): ("f", (FLOOR,))})
        lighting.run(b)
        (e,) = b.entities
        c = {c["type"]: c for c in e["components"]}
        t, v = c["TransformDefinitionComponent"], c["VisualDataBoxDefinitionComponent"]
        self.assertEqual((t["position"]["x"], t["position"]["z"]), (150.0, 100.0))  # the middle of 30 x 20 tiles
        self.assertGreaterEqual(t["position"]["x"] + t["scale"]["x"], 30 * TILE)  # reaches the far edges
        self.assertGreaterEqual(t["position"]["z"] + t["scale"]["z"], 20 * TILE)
        self.assertLessEqual(t["position"]["x"] - t["scale"]["x"], 0)
        self.assertEqual((v["min"]["x"], v["max"]["x"]), (-1.0, 1.0))  # the stock box convention: scale is the half extent
        self.assertTrue(v["enableIBL"])
        self.assertFalse(v["enableGlobalLights"])  # only the ambient is taken: a map's own key light stays
        self.assertIn(lighting.VIS, b.deps)

    def test_a_biome_at_or_above_the_ambient_or_unknown_is_left_alone(self):
        for biome in ("data/hd/env/biome/act2_maggot.json", "data/hd/env/biome/act2_palace_cells.json",
                      "data/hd/env/biome/act9_unknown.json"):
            b = build(biome, {(0, 0): ("f", (FLOOR,))})
            lighting.run(b)
            self.assertEqual(b.entities, [], biome)

    def test_the_box_takes_an_ibl_above_every_biome_it_lifts(self):
        self.assertLess(max(v for v in lighting.BIOME_IBL.values() if v < lighting.AMBIENT), lighting.AMBIENT)
        self.assertTrue(lighting.VIS.endswith("palace_cells_day.json"))  # iblIntensity 2.5 = AMBIENT

    def test_it_runs_after_the_height_pass_which_would_lift_the_box(self):
        self.assertGreater(lighting.ORDER, floor_height.ORDER)


class MeasureTests(unittest.TestCase):
    def test_lit_share_counts_the_occupied_tiles_within_a_light_radius(self):
        ents = [prop(5, 5), prop(15, 5), prop(95, 95), light(5, 5, 12.0)]
        n, share = measure.lit_share(ents)
        self.assertEqual(n, 1)
        self.assertAlmostEqual(share, 2 / 3)

    def test_a_spot_light_counts_and_a_map_with_no_light_is_unlit(self):
        self.assertEqual(measure.lit_share([prop(5, 5), light(5, 5, 20.0, "SpotLightDefinitionComponent")]), (1, 1.0))
        self.assertEqual(measure.lit_share([prop(5, 5), prop(55, 5)]), (0, 0.0))
        self.assertEqual(measure.lit_share([]), (0, 0.0))

    def test_the_score_rises_with_each_source_of_light(self):
        base = measure.score(0.5, 0.0, 0.0)
        self.assertGreater(measure.score(2.5, 0.0, 0.0), base)
        self.assertGreater(measure.score(0.5, 25.0, 0.0), base)
        self.assertGreater(measure.score(0.5, 0.0, 1.0), base)


if __name__ == "__main__":
    unittest.main()
