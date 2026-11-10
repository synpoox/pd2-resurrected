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
TOMB = "data/hd/env/biome/act2_tomb.json"
FLOOR = ("act1/a/floor.dt1", 0, 0, 1)


def build(biome: str, keys: dict, name: str = "darktemple", entities: list | None = None) -> SimpleNamespace:
    deps = []
    return SimpleNamespace(lay=Layout(30, 20, keys), biome=biome, name=name, entities=list(entities or []), deps=deps,
                           depend=lambda of: deps.extend(d["path"] for ds in of.values() for d in ds))


def light(x: float, z: float, radius: float = 25.0, kind: str = "PointLightDefinitionComponent") -> dict:
    return {"components": [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z}},
                           {"type": kind, "radius": radius}]}


def prop(x: float, z: float) -> dict:
    return {"components": [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z}}]}


def stock_box(x: float, z: float, hx: float, hz: float, turned: bool = False) -> dict:
    """A stock-style box: scale 1, min/max the extent."""
    return {"name": "visbox_stock", "components": [
        {"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z},
         "orientation": {"x": 0.0, "y": 0.7 if turned else 0.0, "z": 0.0, "w": 1.0},
         "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
        {"type": "VisualDataBoxDefinitionComponent", "min": {"x": -hx, "y": -10.0, "z": -hz},
         "max": {"x": hx, "y": 10.0, "z": hz}, "priority": 1}]}


def area(r: tuple) -> float:
    return (r[2] - r[0]) * (r[3] - r[1])


def vis_box(e: dict) -> tuple:
    return lighting.rect_of(e)


class PassTests(unittest.TestCase):
    def test_a_lifted_map_gets_one_box_over_the_whole_map(self):
        b = build(DARK, {(0, 0): ("f", (FLOOR,))})
        lighting.run(b)
        (e,) = b.entities
        c = {c["type"]: c for c in e["components"]}
        t, v = c["TransformDefinitionComponent"], c["VisualDataBoxDefinitionComponent"]
        self.assertEqual((t["position"]["x"], t["position"]["z"]), (150.0, 100.0))  # the middle of 30 x 20 tiles
        x0, z0, x1, z1 = lighting.rect_of(e)
        self.assertLessEqual((x0, z0), (0, 0))  # reaches the far edges
        self.assertGreaterEqual((x1, z1), (30 * TILE, 20 * TILE))
        self.assertEqual((v["min"]["x"], v["max"]["x"]), (-1.0, 1.0))  # the stock box convention: scale is the half extent
        self.assertTrue(v["enableIBL"])
        self.assertFalse(v["enableGlobalLights"])  # only the ambient is taken: a map's own key light stays
        self.assertFalse(v["enableInsideLighting"])  # which could switch the key light off inside the box
        self.assertLess(v["priority"], 1)  # under every stock box
        self.assertIn(lighting.VIS, b.deps)

    def test_only_the_checked_biomes_and_maps_are_lifted(self):
        keys = {(0, 0): ("f", (FLOOR,))}
        for biome, name in (("data/hd/env/biome/act4_lava.json", "lava1"), ("data/hd/env/biome/expansion_wildtemple.json", "torturehalls"),
                            ("data/hd/env/biome/act3_sewer.json", "sewermap1"), ("data/hd/env/biome/act2_maggot.json", "x"),
                            ("data/hd/env/biome/act9_unknown.json", "x"), (DARK, "throne1")):
            b = build(biome, keys, name)
            lighting.run(b)
            self.assertEqual(b.entities, [], (biome, name))
        for biome, name in ((TOMB, "Tomb1"), ("data/hd/env/biome/act2_sewer.json", "market2"), (DARK, "DarktempleV2")):
            b = build(biome, keys, name)
            lighting.run(b)
            self.assertEqual(len(b.entities), 1, (biome, name))

    def test_the_lift_leaves_out_the_boxes_pasted_rooms_bring(self):
        room = stock_box(100.0, 100.0, 40.0, 30.0)  # x 60..140, z 70..130, inside the map
        b = build(DARK, {(0, 0): ("f", (FLOOR,))}, entities=[room])
        lighting.run(b)
        mine = b.entities[1:]
        rs = [vis_box(e) for e in mine]
        full = (-lighting.MARGIN, -lighting.MARGIN, 30 * TILE + lighting.MARGIN, 20 * TILE + lighting.MARGIN)
        self.assertAlmostEqual(sum(map(area, rs)), area(full) - 80 * 60)  # everything but the room
        for r in rs:  # none overlaps the room
            self.assertTrue(r[2] <= 60 or r[0] >= 140 or r[3] <= 70 or r[1] >= 130, r)
        for i, r in enumerate(rs):  # and none overlaps another
            for q in rs[i + 1:]:
                self.assertTrue(r[2] <= q[0] or q[2] <= r[0] or r[3] <= q[1] or q[3] <= r[1], (r, q))
        self.assertEqual(len({e["name"] for e in mine}), len(mine))
        ids = {c["targetId"] for e in mine for c in e["components"] if c["type"] == "VisualDataBoxDefinitionComponent"}
        self.assertEqual(len(ids), len(mine))

    def test_a_turned_or_far_box_is_not_cut_out(self):
        for e in (stock_box(100.0, 100.0, 40.0, 30.0, turned=True), stock_box(5000.0, 5000.0, 40.0, 30.0)):
            b = build(DARK, {(0, 0): ("f", (FLOOR,))}, entities=[e])
            lighting.run(b)
            self.assertEqual(len(b.entities), 2)

    def test_minus_splits_a_rectangle_around_a_hole(self):
        r = (0.0, 0.0, 10.0, 10.0)
        self.assertEqual(lighting.minus(r, (20.0, 20.0, 30.0, 30.0)), [r])
        self.assertEqual(lighting.minus(r, (-5.0, -5.0, 15.0, 15.0)), [])
        pieces = lighting.minus(r, (0.0, 0.0, 4.0, 10.0))  # a left strip: the right 6 x 10 stays
        self.assertEqual(sum(map(area, pieces)), 60.0)
        self.assertEqual(sum(map(area, lighting.minus(r, (3.0, 3.0, 5.0, 5.0)))), 96.0)

    def test_the_box_takes_an_ibl_above_every_biome_it_lifts(self):
        self.assertLess(max(lighting.BIOME_IBL.values()), lighting.AMBIENT)
        self.assertEqual(set(lighting.BIOME_IBL), set(lighting.LIFT))
        self.assertTrue(lighting.VIS.endswith("palace_cells_day.json"))  # iblIntensity 2.5 = AMBIENT

    def test_biome_ibl_matches_the_d2r_vis_files(self):
        d2r = measure.d2r_data()
        if not (d2r / "hd" / "env" / "vis").is_dir() or not (d2r / "hd" / "env" / "biome").is_dir():
            self.skipTest("no D2R biome or vis files (modkit fetch --prefix data/hd/env/biome/ --prefix data/hd/env/vis/)")
        self.assertEqual(measure.check(d2r), [])
        self.assertAlmostEqual(measure.vis_of(d2r, lighting.VIS)[0], lighting.AMBIENT)

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
