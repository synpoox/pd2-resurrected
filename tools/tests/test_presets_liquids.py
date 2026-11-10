"""presets.liquids: the liquid mask, and the water, lava and swamp planes of the streams pass (#741)."""
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
SWAMP_MODEL = "data/hd/env/model/act3/docktown/act3_docktown_water/swamp_terrain.model"
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

    def test_a_dt1_draws_lava_swamp_or_water(self):
        self.assertEqual([liquids.kind(d) for d in (LAVA, "act4/lava/floornew.dt1", SWAMP, RIVER, "act2/town/canal.dt1")],
                         ["lava", "lava", "swamp", "water", "water"])

    def test_lava_and_swamp_planes_scale_by_their_own_span(self):
        lay = layout(LAVA, {(0, 0), (1, 0)})
        lay.keys.update(layout(SWAMP, {(0, 2)}).keys)
        lay.flags.update({(0, 2): b"\1" * 25})
        water = {LAVA: (plane(LAVA_MODEL, "wall"), {}), SWAMP: (plane(SWAMP_MODEL), {})}
        out = [e for e, _ in P.streams(lay, water, set())]
        lava = next(e for e in out if e["components"][2]["filename"] == LAVA_MODEL)["components"]
        swamp = next(e for e in out if e["components"][2]["filename"] == SWAMP_MODEL)["components"]
        self.assertEqual(lava[0]["position"], {"x": 10.0, "y": P.WATER_Y, "z": 5.0})
        self.assertEqual(lava[0]["scale"], {"x": 20 / 29.5872, "y": 1.0, "z": 10 / 29.5872})
        self.assertEqual(lava[1]["filter"], "effectfloor")  # a stock lava plane's "wall" would block the mouse
        self.assertEqual(swamp[0]["scale"], {"x": 10 / 650.0, "y": 1.0, "z": 10 / 550.0})

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


if __name__ == "__main__":
    unittest.main()
