"""The liquid edge pass (#738, passes/edges_liquids.py): shore tiles of lava and Act 1 rivers get low D2R edge pieces."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_coverage as coverage  # noqa: E402
from presets.layout import Layout  # noqa: E402
from presets.passes import edges_liquids as liquid_edges  # noqa: E402
from presets.passes import edges_walls  # noqa: E402

LAVA, RIVER, CANAL, GRASS = "act4/lava/floor.dt1", "act1/outdoors/river.dt1", "act2/town/canal.dt1", "act1/outdoors/floor.dt1"


def box_of(model: str):
    """Boxes for the kit's models only, as the wall pass's context gives them."""
    for v in liquid_edges.kit().values():
        m = next(c for c in v["entity"]["components"] if c["type"] == "ModelDefinitionComponent")["filename"]
        if m.lower() == model.lower():
            b = v["box"]
            return tuple(b)
    return None


def build(liquid: str, wet: set, land: set) -> SimpleNamespace:
    keys = {c: ("f", ((liquid, 0, 1, 0),)) for c in wet}
    keys.update({c: ("f", ((GRASS, 0, 1, 0),)) for c in land})
    flags = {c: b"\1" * 25 for c in wet}
    flags.update({c: bytes(25) for c in land})
    deps = []
    lay = Layout(30, 30, keys, flags)
    b = SimpleNamespace(lay=lay, loose=[], entities=[], rest={c: None for c in keys}, taken=set(), seed="s",
                        water={}, state={}, depend=deps.append, deps=deps)
    b.mine = lambda module: b.state.setdefault(module, {})
    return b


def pool(liquid: str, shore: str = "x") -> SimpleNamespace:
    """A 6x6 liquid pool with land all round it (a 10x10 map)."""
    wet = {(x, y) for x in range(5, 11) for y in range(5, 11)}
    land = {(x, y) for x in range(3, 13) for y in range(3, 13)} - wet
    return build(liquid, wet, land)


def run(b) -> None:
    ctx = SimpleNamespace(box=box_of, prefab=lambda p: [], legacy=b.lay, heights={})
    with mock.patch.dict(edges_walls.CONTEXT, {"s": ctx}, clear=True):
        liquid_edges.run(b)


def tile(e: dict) -> tuple[int, int]:
    p = next(c for c in e["components"] if c["type"] == "TransformDefinitionComponent")["position"]
    return int(p["x"] // 10), int(p["z"] // 10)


def model(e: dict) -> str:
    return next(c for c in e["components"] if c["type"] == "ModelDefinitionComponent")["filename"]


class LiquidEdgeTests(unittest.TestCase):
    def test_a_lava_pool_gets_lava_rims_all_round_it(self):
        b = pool(LAVA)
        run(b)
        self.assertTrue(b.loose)
        self.assertTrue(all("act4_lava_edge" in model(e) for e in b.loose), [model(e) for e in b.loose])
        self.assertTrue(b.deps)

    def test_the_rims_are_low_pieces_that_hide_no_walkable_ground(self):
        for name, k in liquid_edges.kit().items():
            self.assertLess(k["box"][4], coverage.TALL, name)
            self.assertFalse(any(c["type"] == "PhysicsBodyDefinitionComponent" for c in k["entity"]["components"]), name)

    def test_an_act1_river_gets_river_rocks(self):
        b = pool(RIVER)
        run(b)
        self.assertTrue(b.loose)
        self.assertTrue(all("rock_river" in model(e) for e in b.loose))

    def test_an_edge_already_stood_over_by_a_water_piece_is_left_alone(self):
        b = pool(LAVA)
        run(b)
        first = len(b.loose)
        again = pool(LAVA)
        again.loose = list(b.loose)
        run(again)
        self.assertEqual(len(again.loose), first)  # every shore tile is covered now: nothing is added

    def test_liquids_the_kit_has_no_piece_for_are_left_alone(self):
        b = pool(CANAL)
        run(b)
        self.assertEqual(b.loose, [])

    def test_tiles_outside_the_kit_fill_and_pasted_rooms_are_left_alone(self):
        b = pool(LAVA)
        b.rest = {}
        run(b)
        self.assertEqual(b.loose, [])
        b = pool(LAVA)
        b.taken = set(b.rest)
        run(b)
        self.assertEqual(b.loose, [])

    def test_the_inside_of_a_pool_gets_nothing(self):
        b = pool(LAVA)
        run(b)
        for e in b.loose:
            x, z = tile(e)
            self.assertTrue(2 <= x <= 13 and 2 <= z <= 13, (x, z))

    def test_without_model_boxes_it_places_nothing(self):
        b = pool(LAVA)
        with mock.patch.dict(edges_walls.CONTEXT, {}, clear=True):
            liquid_edges.run(b)
        self.assertEqual(b.loose, [])

    def test_a_straight_rim_runs_along_the_shore(self):
        wet = {(x, y) for x in range(2, 20) for y in range(5, 12)}
        land = {(x, y) for x in range(2, 20) for y in range(12, 16)}  # land only to the south: the shore runs along x
        b = build(LAVA, wet, land)
        run(b)
        for e in b.loose:
            k = next(v for v in liquid_edges.kit().values() if v["entity"]["components"][1] == next(
                c for c in e["components"] if c["type"] == "ModelDefinitionComponent"))
            q = next(c for c in e["components"] if c["type"] == "TransformDefinitionComponent")["orientation"]
            long_z = (k["box"][5] - k["box"][2]) >= (k["box"][3] - k["box"][0])
            self.assertEqual(abs(q["y"]) > 0.5, long_z)  # a rim long in z is turned a quarter; one long in x is not


if __name__ == "__main__":
    unittest.main()
