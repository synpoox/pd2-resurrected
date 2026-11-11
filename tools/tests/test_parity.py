"""pd2_parity on hand-made layouts and pieces (no PD2 or D2R data)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_parity as P  # noqa: E402
import pd2_presets as presets  # noqa: E402

FLOORS = "act1/outdoors/floor.dt1"
WALLS = "act1/cathedrl/walls.dt1"
SIZE = 14  # tiles each way


def layout(keys: dict) -> presets.Layout:
    return presets.Layout(SIZE, SIZE, keys, {c: bytes(25) for c in keys}, frozenset())


def floor_keys() -> dict:
    return {(x, y): ("f", ((FLOORS, 0, 0, 0),)) for x in range(SIZE) for y in range(SIZE)}


def piece(kind_dir: str, x0: float, x1: float, top: float, z: float = 55.0, y: float = 0.0, name: str = "e") -> dict:
    """An entity standing a model of folder `kind_dir`; the fake box is (x1 - x0) wide, `top` high."""
    return {"name": name, "components": [
        {"type": "TransformDefinitionComponent", "position": {"x": (x0 + x1) / 2, "y": y, "z": z}},
        {"type": "ModelDefinitionComponent", "filename": f"data/hd/{kind_dir}/{int(x1 - x0)}x{int(top * 10)}.model"}]}


def box(path: str):
    w_, h = (int(n) for n in path.rsplit("/", 1)[-1].removesuffix(".model").split("x"))
    return -w_ / 2, 0.0, -4.9, w_ / 2, h / 10, 4.9


def score(*entities: dict, keys: dict | None = None, legacy: dict | None = None, heights: dict | None = None) -> dict:
    keys = keys if keys is not None else floor_keys()
    return P.score("t", layout(keys), layout(legacy if legacy is not None else keys), {"entities": list(entities)},
                   box, lambda p: [], heights or {})


class Checks(unittest.TestCase):
    def test_empty_open_floor_is_perfect(self):
        r = score()
        self.assertEqual(r["parity"], 1.0)
        self.assertEqual(sum(r["defects"].values()), 0)
        self.assertEqual(P.worst_check(r), "-")

    def test_tall_grounded_piece_on_open_floor_is_solid(self):
        r = score(piece("act1/rocks", 40, 60, 5.0))
        self.assertGreater(r["defects"]["solid"], 0)
        self.assertLess(r["parity"], 1.0)
        self.assertEqual(P.worst_check(r), "solid")

    def test_raised_tall_piece_is_occlusion_not_solid(self):
        r = score(piece("act1/rocks", 40, 60, 5.0, y=2.0))
        self.assertGreater(r["defects"]["occlusion"], 0)
        self.assertEqual(r["defects"]["solid"], 0)

    def test_low_prop_is_clutter_unless_ground_cover(self):
        self.assertGreater(score(piece("act1/rocks", 40, 60, 2.0))["defects"]["clutter"], 0)
        self.assertEqual(score(piece("act1/grass_clump", 40, 60, 2.0))["defects"]["clutter"], 0)

    def test_floating_piece_but_not_a_light_or_a_pasted_room_piece(self):
        high = dict(y=3.0)
        self.assertGreater(score(piece("act1/rocks", 40, 60, 2.0, **high))["defects"]["floating"], 0)
        self.assertEqual(score(piece("act1/candle_lights", 40, 60, 2.0, **high))["defects"]["floating"], 0)
        room = piece("act1/rocks", 40, 60, 2.0, name=presets.ROOM_PIECE + "x", **high)
        self.assertEqual(score(room)["defects"]["floating"], 0)

    def test_void_where_legacy_draws_nothing_but_hd_draws_a_piece(self):
        legacy = floor_keys()
        for x in range(4, 8):
            for y in range(4, 8):
                del legacy[(x, y)]
        r = score(piece("act1/rocks", 50, 70, 2.0, y=0.0), keys=floor_keys(), legacy=legacy)
        # stand-in layout has the floor; legacy has none there: void is about the layout HD builds from
        self.assertEqual(r["defects"]["void"], 0)
        hole = {c: k for c, k in floor_keys().items() if c not in {(5, 5), (6, 5)}}
        r = score(piece("act1/rocks", 50, 70, 2.0), keys=hole)
        self.assertGreater(r["defects"]["void"], 0)

    def test_invisible_wall_cell_with_nothing_drawn(self):
        keys = floor_keys()
        keys[(5, 5)] = ("w", ((WALLS, 1, 0, 0),))
        h = {WALLS: {(1, 0, 0): 288}}
        self.assertEqual(score(keys=keys, heights=h)["defects"]["invisible"], 1)
        # a drawn piece covers it
        self.assertEqual(score(piece("act1/cathedrl_walls", 52, 58, 3.0, z=55.0), keys=keys, heights=h)["defects"]["invisible"], 0)

    def test_parity_counts_a_tile_once(self):
        r = score(piece("act1/rocks", 40, 60, 5.0), piece("act1/rocks", 40, 60, 2.0))
        flagged = len(r["sets"]["solid"] | r["sets"]["clutter"])
        self.assertEqual(r["parity"], 1 - (P.WEIGHT["walk"] * flagged) / (P.WEIGHT["walk"] * r["tiles"]))


class Baseline(unittest.TestCase):
    ROWS = [{"name": "a", "parity": 0.5, "defects": dict.fromkeys(P.CHECKS, 3)},
            {"name": "b", "parity": 0.9, "defects": dict.fromkeys(P.CHECKS, 0)}]

    def test_baseline_round_trip(self):
        self.assertEqual(P.read_baseline(P.baseline_text(self.ROWS)), {"a": 50.0, "b": 90.0})

    def test_drop_past_tolerance_fails_a_small_one_does_not(self):
        base = {"a": 50.0, "b": 90.0, "c": 70.0}
        rows = [{"name": "a", "parity": 0.485, "defects": {}}, {"name": "b", "parity": 0.895, "defects": {}},
                {"name": "d", "parity": 0.6, "defects": {}}]
        found = {c.name: c for c in P.changes(rows, base, 1.0)}
        self.assertTrue(found["a"].drop)       # -1.5
        self.assertFalse(found["b"].drop)      # -0.5 (89.5)
        self.assertTrue(found["c"].drop)       # no longer scored
        self.assertFalse(found["d"].drop)      # new map
        self.assertEqual(P.changes(rows[:0], {}, 1.0), [])

    def test_a_rise_never_fails(self):
        found = P.changes([{"name": "a", "parity": 0.9, "defects": {}}], {"a": 50.0})
        self.assertFalse(found[0].drop)

    def test_report_names_mean_worst_and_the_override_label(self):
        base = {"a": 80.0, "b": 90.0}
        md = P.report_md([dict(r, tiles=1, walkable=1, edges=0) for r in self.ROWS], "abcdef0123", P.changes(self.ROWS, base))
        self.assertIn("Mean parity **70.0%**", md)
        self.assertIn("abcdef012", md)
        self.assertIn("parity-drop-ok", md)
        self.assertIn("| a | 50.0% |", md)


class Run(unittest.TestCase):
    def test_skips_without_model_boxes(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(P.main(["--data", d, "--d2r", d, "--out", d, "--baseline", str(Path(d) / "none.tsv")]), 0)


if __name__ == "__main__":
    unittest.main()
