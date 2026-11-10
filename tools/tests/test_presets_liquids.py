"""presets.liquids: the liquid mask, and the water and lava planes of the streams pass (#741)."""
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import liquids  # noqa: E402
from presets_fixtures import FLOOR, TERRAIN  # noqa: E402

LAVA_MODEL = "data/hd/env/model/act4/lava/act4_lava_plane/diab_lavaplane01.model"
LAVA, SWAMP, RIVER = "act4/lava/floor.dt1", "act1/outdoors/swamp.dt1", "act1/outdoors/river.dt1"


def plane(model: str, filter_: str = "effectfloor") -> dict:
    return {"type": "Entity", "name": "p", "components": [
        {"type": "TransformDefinitionComponent", "position": {"x": 3.0, "y": 3.5, "z": 4.0},
         "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}, "scale": {"x": 0.3, "y": 0.2, "z": 0.3}},
        {"type": "PhysicsBodyDefinitionComponent", "filter": filter_},
        {"type": "ModelDefinitionComponent", "filename": model}]}


def layout(dt1: str, cells: set, walkable: set = frozenset()) -> P.Layout:
    keys = {c: ("f", ((dt1, 0, 1, 0),)) for c in cells | walkable}
    return P.Layout(4, 4, keys, {c: bytes(25) if c in walkable else b"\1" * 25 for c in keys})


class Stub:
    def __init__(self, lay, water=()):
        self.lay, self.water, self.state = lay, dict.fromkeys(water), {}

    def mine(self, module):
        return self.state.setdefault(module, {})


class LiquidTests(unittest.TestCase):
    def test_the_mask_is_the_blocked_floor_of_liquid_dt1(self):
        lay = layout(LAVA, {(0, 0), (1, 0)}, {(2, 0)})  # the third tile is lava floor you can walk on
        lay.keys[(0, 1)] = ("f", (FLOOR,))  # blocked floor of a DT1 that is no liquid
        lay.flags[(0, 1)] = b"\1" * 25
        self.assertEqual(liquids.mask(lay), {(0, 0): (LAVA,), (1, 0): (LAVA,)})

    def test_unwalkable_lava_is_liquid_but_not_under_a_platform(self):
        lay = layout(LAVA, set(), {(0, 0), (1, 0), (2, 0)})  # lava floor with walkable flags
        lay.keys[(2, 0)] = ("f", ((LAVA, 0, 1, 0), ("act4/diab/floor.dt1", 0, 1, 0)))
        lay.unwalkable = frozenset({(0, 0), (2, 0)})
        self.assertEqual(liquids.mask(lay), {(0, 0): (LAVA,)})

    def test_a_crust_overlay_on_lava_is_liquid_with_zero_flags(self):
        # lava1: the base lava tile (0,20,k) carries a crust overlay (0,22,k) of the same DT1, flags all zero and no
        # unwalkable bit of its own: still lava, where the same base tile is lava elsewhere (a blocked tile)
        lay = layout(LAVA, set(), {(0, 0), (1, 0), (2, 0)})
        lay.keys[(0, 0)] = ("f", ((LAVA, 0, 20, 1),))
        lay.keys[(1, 0)] = ("f", ((LAVA, 0, 20, 1), (LAVA, 0, 22, 1)))
        lay.keys[(2, 0)] = ("f", ((LAVA, 0, 20, 1), ("act4/diab/floor.dt1", 0, 1, 0)))
        lay.unwalkable = frozenset({(0, 0)})
        self.assertEqual(liquids.mask(lay), {(0, 0): (LAVA,), (1, 0): (LAVA,)})  # not the platform (2, 0)

    def test_a_lava_decal_on_walkable_road_is_no_liquid(self):
        # demonroad1: the road's base tile (0,22,0) is walkable single-layer floor 231 times and carries lava decals on
        # 15 tiles: no lava. A lava sea's base (lava1's (0,20,3)) is blocked on far more tiles than it is walkable alone
        road, sea, decal = (LAVA, 0, 22, 0), (LAVA, 0, 20, 3), (LAVA, 0, 22, 1)
        lay = layout(LAVA, set(), {(x, 0) for x in range(6)})
        lay.keys[(0, 0)] = lay.keys[(1, 0)] = ("f", (road,))  # plain road
        lay.keys[(2, 0)] = ("f", (road, decal))  # road with a decal
        lay.keys[(3, 0)] = ("f", (sea,))  # lava sea, unwalkable
        lay.keys[(4, 0)] = ("f", (sea, decal))  # its crust
        lay.keys[(5, 0)] = ("f", (sea,))  # one stray walkable tile of the sea base
        lay.unwalkable = frozenset({(3, 0)})
        lay.keys[(6, 0)], lay.flags[(6, 0)] = ("f", (sea,)), b"" * 25
        lay.unwalkable |= {(7, 0)}
        lay.keys[(7, 0)], lay.flags[(7, 0)] = ("f", (sea,)), bytes(25)
        lay.keys[(8, 0)], lay.flags[(8, 0)] = ("f", (road,)), b"" * 25  # the road base blocked once somewhere
        self.assertEqual(sorted(liquids.mask(lay)), [(3, 0), (4, 0), (6, 0), (7, 0), (8, 0)])  # not (2, 0)

    def test_a_rule_dt1_is_liquid_whatever_its_name(self):
        lay = layout("act1/a/wall.dt1", {(0, 0)})
        self.assertEqual(liquids.mask(lay), {})
        self.assertEqual(liquids.mask(lay, frozenset({"act1/a/wall.dt1"})), {(0, 0): ("act1/a/wall.dt1",)})

    def test_the_mask_is_made_once_and_shared(self):
        b = Stub(layout(SWAMP, {(0, 0)}))
        with mock.patch.object(liquids, "mask", wraps=liquids.mask) as m:
            self.assertEqual(liquids.of(b), {(0, 0): (SWAMP,)})
            self.assertIs(liquids.of(b), liquids.of(b))
        self.assertEqual(m.call_count, 1)

    def test_lava_planes_scale_by_their_own_span(self):
        lay = layout(LAVA, {(0, 0), (1, 0)})
        water = {LAVA: (plane(LAVA_MODEL, "wall"), {})}
        out = [e for e, _ in P.streams(lay, water, set())]
        lava = next(e for e in out if e["components"][2]["filename"] == LAVA_MODEL)["components"]
        self.assertEqual(lava[0]["position"], {"x": 10.0, "y": P.WATER_Y, "z": 5.0})
        self.assertEqual(lava[0]["scale"], {"x": 20 / 29.5872, "y": 1.0, "z": 10 / 29.5872})  # no liquid beside: no RIM
        self.assertEqual(lava[1]["filter"], "wall")  # as every stock lava plane

    def test_a_big_pool_is_split_into_planes_no_larger_than_stock_scale(self):
        lay = layout(LAVA, {(x, y) for x in range(9) for y in range(5)})
        out = P.streams(lay, {LAVA: (plane(LAVA_MODEL), {})}, set())
        self.assertTrue(all(e["components"][0]["scale"][a] <= 1.5 for e, _ in out for a in "xz"))
        self.assertGreater(len(out), 1)
        # the planes reach exactly the pool's edge, 9 x 5 tiles: no rim past the mask
        spans = [(p["position"][a] - p["scale"][a] * 29.5872 / 2, p["position"][a] + p["scale"][a] * 29.5872 / 2)
                 for e, _ in out for p in [e["components"][0]] for a in "xz"]
        self.assertAlmostEqual(min(s[0] for s in spans[0::2]), 0.0)
        self.assertAlmostEqual(max(s[1] for s in spans[0::2]), 90.0)
        self.assertAlmostEqual(min(s[0] for s in spans[1::2]), 0.0)
        self.assertAlmostEqual(max(s[1] for s in spans[1::2]), 50.0)

    def test_the_rim_overlaps_neighbouring_planes_only(self):
        # a 2x1 and a 1x1 lava rectangle, one under the other: the rim joins them along z; nowhere else
        lay = layout(LAVA, {(0, 0), (1, 0), (0, 1)}, {(1, 1), (2, 0)})
        out = [e["components"][0] for e, _ in P.streams(lay, {LAVA: (plane(LAVA_MODEL), {})}, set())]
        rx, rz = P.plane_rim(LAVA_MODEL)

        def ext(axis):
            return sorted((round(c["position"][axis] - c["scale"][axis] * 29.5872 / 2, 6),
                           round(c["position"][axis] + c["scale"][axis] * 29.5872 / 2, 6)) for c in out)
        self.assertEqual(len(out), 2)
        self.assertEqual(ext("x"), [(0.0, 10.0), (0.0, 20.0)])  # no spill onto the walkable tiles
        self.assertEqual(ext("z"), [(0.0, 10.0 + rz / 2), (10.0 - rz / 2, 20.0)])

    def test_only_stream_dt1_are_wet(self):
        self.assertEqual(P.wet_dt1s({"act3/river/rivbank.dt1": 1, SWAMP: 1, LAVA: 1}), {"act3/river/rivbank.dt1"})

    def test_the_plane_height_comes_from_one_function(self):
        lay = layout(LAVA, {(1, 1), (2, 1)})
        with mock.patch("presets.water.liquid_y", return_value=7.5) as y:
            (e, _), = P.streams(lay, {LAVA: (plane(LAVA_MODEL), {})}, set())
        self.assertEqual(e["components"][0]["position"]["y"], 7.5)
        y.assert_called_once_with((2, 1))  # the rectangle's centre tile

    def test_tiles_of_pasted_rooms_get_no_plane_but_stay_in_the_mask(self):
        lay = layout(LAVA, {(0, 0), (1, 0)})
        mask = liquids.mask(lay)
        out = P.streams(lay, {LAVA: (plane(LAVA_MODEL), {})}, {(0, 0)}, mask)
        self.assertEqual(len(out), 1)
        self.assertEqual(set(mask), {(0, 0), (1, 0)})

    def test_oasis_floor_gets_no_plane(self):
        # ruinedcisternv3's sand fill is all (39,50) oasis tiles: no stock room loading oasis.dt1 draws water
        oasis = "act2/outdoors/oasis.dt1"
        lay = layout(oasis, {(x, y) for x in range(4) for y in range(4)})
        self.assertEqual(P.streams(lay, self.rule_water(), set()), [])

    def test_swamp_floor_gets_no_plane(self):
        lay = layout(SWAMP, {(x, y) for x in range(4) for y in range(4)})
        self.assertEqual(P.streams(lay, self.rule_water(), set()), [])

    @staticmethod
    def rule_water() -> dict:
        """The real `water:` rules as streams() takes them (a bare plane per DT1)."""
        return {d: (plane(m), {}) for d, m in water_rules().items()}

    def test_every_water_rule_is_an_expected_liquid(self):
        allowed = ("river", "rivbank", "pool", "pond", "canal", "lava/floor")
        rules = water_rules()
        self.assertTrue(rules)
        for dt1 in rules:
            self.assertTrue(any(a in dt1 for a in allowed), dt1)
        self.assertFalse([d for d in rules if "oasis" in d or "swamp" in d])


def water_rules() -> dict:
    tsv = Path(__file__).resolve().parent.parent.parent / "merge" / "presets-hd.tsv"
    rules = {}
    for line in tsv.read_text(encoding="utf-8").splitlines():
        if line.startswith("water:"):
            key, _, model = line.partition("	")
            rules[key[len("water:"):]] = model.strip()
    return rules


if __name__ == "__main__":
    unittest.main()
