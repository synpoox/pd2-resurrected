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

    def test_void_is_where_legacy_draws_nothing(self):
        hole = {c: k for c, k in floor_keys().items() if c not in {(5, 5), (6, 5)}}
        r = score(piece("act1/rocks", 50, 70, 2.0), keys=floor_keys(), legacy=hole)
        self.assertGreater(r["defects"]["void"], 0)
        # a stand-in layout without the tile but legacy drawing it (a PD2 DT1 with no stand-in) is not void
        r = score(piece("act1/rocks", 50, 70, 2.0), keys=hole, legacy=floor_keys())
        self.assertEqual(r["defects"]["void"], 0)

    def test_a_black_cover_over_the_empty_tiles_is_no_void(self):
        hole = {c: k for c, k in floor_keys().items() if c not in {(5, 5), (6, 5)}}
        cover = {"name": "pd2r_void", "components": [
            {"type": "TransformDefinitionComponent", "position": {"x": 60.0, "y": 0.1, "z": 55.0}},
            {"type": "ModelDefinitionComponent", "filename": presets.void.BLACK}]}

        def fits(path: str):
            return (-10.0, 0.0, -5.0, 10.0, 0.0, 5.0) if path == presets.void.BLACK else box(path)
        r = P.score("t", layout(floor_keys()), layout(hole), {"entities": [cover]}, fits, lambda p: [], {})
        self.assertEqual(r["defects"]["void"], 0)
        # a plane the stand-in lays over those tiles is void again without the cover
        r = P.score("t", layout(floor_keys()), layout(hole), {"entities": [piece("act1/rocks", 50, 70, 2.0)]}, box,
                    lambda p: [], {})
        self.assertGreater(r["defects"]["void"], 0)

    def test_floor_tiles_that_draw_nothing_are_void_when_the_drawn_tiles_are_known(self):
        pit = {c: ("f", ((FLOORS, 0, 0, 1),)) if c != (5, 5) else ("f", ((FLOORS, 0, 0, 2),)) for c in floor_keys()}
        drawn = {FLOORS: frozenset({(0, 0, 1)})}
        rock = piece("act1/rocks", 50, 60, 2.0)
        lay = layout(pit)
        lay.flags[(5, 5)] = b"" * 25  # blocked, so no player stands on it
        args = ("t", lay, lay, {"entities": [rock]}, box, lambda p: [], {})
        self.assertEqual(P.score(*args)["defects"]["void"], 0)
        self.assertGreater(P.score(*args, drawn=drawn)["defects"]["void"], 0)

    def test_edge_wall_missing_then_drawn(self):
        keys = floor_keys()
        keys[(5, 5)] = ("w", ((WALLS, 1, 0, 0),))
        h = {WALLS: {(1, 0, 0): 288}}
        self.assertEqual(score(keys=keys, heights=h)["defects"]["edge"], 1)
        wall = piece("act1/cathedrl/act1_cathedrl_walls", 51, 59, 5.0)
        self.assertEqual(score(wall, keys=keys, heights=h)["defects"]["edge"], 0)

    def test_liquid_floor_needs_a_water_model(self):
        keys = floor_keys()
        keys[(5, 5)] = ("f", (("act1/outdoors/water.dt1", 0, 0, 0),))
        flags = {c: bytes(25) for c in keys}
        flags[(5, 5)] = bytes([1] * 25)
        lay = presets.Layout(SIZE, SIZE, keys, flags, frozenset())

        def liquid(*entities):
            return P.score("t", lay, lay, {"entities": list(entities)}, box, lambda p: [], {})["defects"]["liquid"]
        self.assertEqual(liquid(), 1)
        self.assertEqual(liquid(piece("act1/water", 50, 60, 0.5)), 0)

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
        got = P.read_baseline(P.baseline_text(self.ROWS))
        self.assertEqual(got["a"], (50.0, dict.fromkeys(P.CHECKS, 3)))
        self.assertEqual(got["b"][0], 90.0)

    def test_drop_past_tolerance_fails_a_small_one_does_not(self):
        base = {"a": (50.0, {"void": 5}), "b": (90.0, {}), "c": (70.0, {})}
        rows = [{"name": "a", "parity": 0.485, "defects": {"void": 9}}, {"name": "b", "parity": 0.895, "defects": {}},
                {"name": "d", "parity": 0.6, "defects": {}}]
        found = {c.name: c for c in P.changes(rows, base, 1.0)}
        self.assertTrue(found["a"].drop)       # -1.5
        self.assertEqual(found["a"].deltas, {"void": 4})
        self.assertFalse(found["b"].drop)      # -0.5 (89.5)
        self.assertTrue(found["c"].drop)       # no longer scored
        self.assertFalse(found["d"].drop)      # new map
        self.assertEqual(P.changes(rows[:0], {}, 1.0), [])

    def test_a_rise_never_fails(self):
        found = P.changes([{"name": "a", "parity": 0.9, "defects": {}}], {"a": (50.0, {})})
        self.assertFalse(found[0].drop)

    def test_report_names_mean_worst_and_the_override_label(self):
        base = {"a": (80.0, {"edge": 1}), "b": (90.0, {})}
        md = P.report_md([dict(r, tiles=1, walkable=1, edges=0) for r in self.ROWS], "abcdef0123", P.changes(self.ROWS, base))
        self.assertIn("Mean parity **70.0%**", md)
        self.assertIn("abcdef012", md)
        self.assertIn("parity-drop-ok", md)
        self.assertIn("| a | 50.0% |", md)
        self.assertIn("edge +2", md)  # per-check deltas in the changes table
        self.assertIn("Not gated", P.report_md(self.ROWS, note="Not gated: no baseline"))


class Run(unittest.TestCase):
    def test_palace_is_not_scored(self):
        self.assertFalse(P.wanted("palace3"))
        self.assertTrue(P.wanted("jungle1"))
        self.assertFalse(P.wanted("jungle1", {"tomb1"}))
        self.assertTrue(P.wanted("tomb1", {"tomb1"}))

    def test_skips_without_model_boxes_and_says_so(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(P.main(["--data", d, "--d2r", d, "--out", d, "--baseline", str(Path(d) / "none.tsv")]), 0)
            self.assertIn("Not scored", (Path(d) / "parity.md").read_text("utf-8"))

    def gate(self, d: str, base_text: str | None, now: float, *extra: str) -> int:
        """main(--check) over one scored map at `now` parity, against a baseline file (or none)."""
        rows = [{"name": "a", "parity": now, "defects": dict.fromkeys(P.CHECKS, 0), "tiles": 1, "walkable": 1, "edges": 0}]
        args = ["--out", d, "--check", "--baseline", str(Path(d) / "base.tsv"), *extra]
        if base_text is not None:
            (Path(d) / "base.tsv").write_text(base_text, "utf-8")
        orig, P.run = P.run, lambda *a, **k: rows
        try:
            return P.main(args)
        finally:
            P.run = orig

    def test_check_fails_on_a_drop_passes_otherwise(self):
        base = P.baseline_text([{"name": "a", "parity": 0.9, "defects": dict.fromkeys(P.CHECKS, 0)}])
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self.gate(d, base, 0.85), 1)
            self.assertIn("FAIL", (Path(d) / "parity.md").read_text("utf-8"))
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self.gate(d, base, 0.89), 0)
            self.assertEqual(self.gate(d, base, 0.95), 0)

    def test_check_passes_without_a_baseline_and_says_so(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self.gate(d, None, 0.1), 0)
            self.assertIn("Not gated", (Path(d) / "parity.md").read_text("utf-8"))


if __name__ == "__main__":
    unittest.main()
