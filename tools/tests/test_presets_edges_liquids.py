"""presets.passes.edges_liquids: bank and lava-edge pieces along the walkable-to-liquid boundary (#738)."""
import math
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_coverage as coverage  # noqa: E402
import pd2_painter as painter  # noqa: E402
import pd2_presets as P  # noqa: E402
from presets import liquids  # noqa: E402
from presets.passes import edges_liquids as E  # noqa: E402

RIVER, LAVA = "act1/outdoors/river.dt1", "act4/lava/floor.dt1"
BANK, ROCK, KIT = E.BANKS[0], E.ROCKS[0], E.LAVA[0]
BOXES = {BANK: (-20.0, -9.0, -6.0, 10.0, 1.0, 18.0), ROCK: (-2.0, 0.0, -2.0, 2.0, 1.5, 2.0),
         KIT: (-10.0, 0.0, -15.0, 11.0, 3.6, 16.0)}
GRASS = ("act1/outdoors/grass.dt1", 0, 1, 0)


def entity(model: str) -> dict:
    return {"type": "Entity", "name": "stock", "components": [
        {"type": "TransformDefinitionComponent", "position": {"x": 1.0, "y": -0.5, "z": 2.0},
         "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}, "scale": {"x": 2.0, "y": 2.0, "z": 2.0}},
        {"type": "ModelDefinitionComponent", "filename": model}]}


def pieces() -> dict:
    return {m: (entity(m), {"models": [{"path": m}]}) for m in BOXES}


def map_(water: set, land: set, dt1: str = RIVER) -> P.Layout:
    keys, flags = {}, {}
    for c in water:
        keys[c], flags[c] = ("f", ((dt1, 0, 1, 0),)), b"\1" * 25
    for c in land:
        keys[c], flags[c] = ("f", (GRASS,)), bytes(25)
    return P.Layout(20, 20, keys, flags)


class Stub:
    def __init__(self, lay, taken=frozenset()):
        self.lay, self.water, self.state, self.taken = lay, {}, {}, set(taken)
        self.entities, self.seed, self.deps = [], "s", {}

    def mine(self, module):
        return self.state.setdefault(module, {})

    def depend(self, of):
        self.deps.update({d["path"]: d for kind in of.values() for d in kind})


def stock():
    return mock.patch.dict(E.STOCK, {"pieces": pieces(), "box": BOXES.get}, clear=True)


def footprint(e: dict) -> set:
    return coverage.footprint(coverage.boxes([e], BOXES.get, lambda p: []))


class EdgeTests(unittest.TestCase):
    def river(self):
        """Land on the left (x < 8), a river of 6 tiles on the right, 12 tiles tall."""
        ys = range(4, 16)
        return map_({(x, y) for x in range(8, 14) for y in ys}, {(x, y) for x in range(0, 8) for y in ys})

    def test_a_bank_stands_along_the_shore_facing_the_water(self):
        b = Stub(self.river())
        with stock():
            E.run(b)
        banks = [e for e in b.entities if e["name"] == E.NAME]
        self.assertTrue(banks)
        for e in banks:
            t = e["components"][0]
            v = painter.rotate(t["orientation"], (0, 0, 1))  # the bank's liquid side
            self.assertGreater(v[0], 0.7)  # the water is to the +x side
            self.assertEqual(t["scale"], {"x": 1.0, "y": 1.0, "z": 1.0})
            self.assertAlmostEqual(t["position"]["y"], -0.5)  # the stock piece's own height over the water plane
        self.assertEqual(set(b.deps), {e["components"][1]["filename"] for e in banks})

    def test_no_piece_covers_walkable_ground(self):
        lay = self.river()
        b = Stub(lay)
        with stock():
            E.run(b)
        ground = P.walkable(lay)
        self.assertTrue(b.entities)
        for e in b.entities:
            self.assertFalse(footprint(e) & ground, e["components"][1]["filename"])

    def test_every_shore_tile_is_covered_and_pieces_are_spaced_by_their_length(self):
        lay = self.river()
        b = Stub(lay)
        with stock():
            E.run(b)
        shore = {c for c, k in lay.keys.items() if c in liquids.of(b) and (c[0] - 1, c[1]) not in liquids.of(b)}
        covered = set().union(*map(footprint, b.entities))
        self.assertLessEqual(shore - {c for c in shore if c in covered}, set())
        self.assertLess(len(b.entities), len(shore))  # one piece covers several shore tiles

    def test_a_shore_with_no_room_gets_a_rock_on_the_tile(self):
        # a one-tile pool in the middle of land: no bank box clears the walkable ground around it
        lay = map_({(5, 5)}, {(x, y) for x in range(3, 8) for y in range(3, 8)} - {(5, 5)})
        b = Stub(lay)
        with stock():
            E.run(b)
        self.assertEqual([e["components"][1]["filename"] for e in b.entities], [ROCK])
        self.assertEqual(footprint(b.entities[0]), {(5, 5)})

    def test_lava_gets_lava_kit_pieces_and_nothing_when_none_clears(self):
        lay = map_({(x, y) for x in range(8, 14) for y in range(4, 16)}, {(x, y) for x in range(0, 8) for y in range(4, 16)}, LAVA)
        b = Stub(lay)
        with stock():
            E.run(b)
        self.assertTrue(b.entities)
        self.assertTrue(all(e["components"][1]["filename"] in E.LAVA for e in b.entities))
        tiny = map_({(5, 5)}, {(x, y) for x in range(3, 8) for y in range(3, 8)} - {(5, 5)}, LAVA)
        b = Stub(tiny)
        with stock():
            E.run(b)
        self.assertEqual(b.entities, [])
        self.assertEqual(b.mine(E.__name__)["missed"], [(5, 5)])

    def test_pasted_rooms_and_existing_water_pieces_are_left_alone(self):
        lay = self.river()
        b = Stub(lay, taken={(x, y) for x in range(0, 20) for y in range(0, 20)})
        with stock():
            E.run(b)
        self.assertEqual(b.entities, [])
        b = Stub(lay)
        with stock():
            E.run(b)
            first = len(b.entities)
            E.run(Stub(lay))  # a fresh build: same result
            b2 = Stub(lay)
            b2.entities = list(b.entities)
            b2.state = {}
            E.run(b2)
        self.assertEqual(len(b2.entities), first)  # the pieces already there cover the shore

    def test_a_wall_layer_of_a_river_dt1_is_a_shore_only_next_to_liquid(self):
        lay = self.river()
        wall = (RIVER, 1, 2, 0)
        lay.keys[(7, 3)] = ("w", (wall,))  # beside the water's corner
        lay.keys[(1, 1)] = ("w", (wall,))  # far from any water
        b = Stub(lay)
        liquid = liquids.of(b)
        found = E.targets(b, liquid)
        self.assertIn((7, 3), found)
        self.assertNotIn((1, 1), found)
        self.assertEqual(found[(7, 3)], "water")

    def test_the_pass_places_nothing_without_the_stock_pieces(self):
        b = Stub(self.river())
        with mock.patch.dict(E.STOCK, {}, clear=True):
            E.run(b)
        self.assertEqual(b.entities, [])

    def test_the_pass_runs_after_the_liquid_planes_and_before_the_ground(self):
        from presets import passes
        names = [m.__name__.rsplit(".", 1)[1] for m in passes.ordered()]
        self.assertLess(names.index("streams"), names.index("edges_liquids"))
        self.assertLess(names.index("edges_liquids"), names.index("ground"))

    def test_the_height_comes_from_liquid_y(self):
        b = Stub(map_({(5, 5)}, {(x, y) for x in range(3, 8) for y in range(3, 8)} - {(5, 5)}))
        with stock(), mock.patch.object(E.liquids, "liquid_y", return_value=E.WATER_Y + 3.0):
            E.run(b)
        self.assertAlmostEqual(b.entities[0]["components"][0]["position"]["y"], 2.5)


if __name__ == "__main__":
    unittest.main()
