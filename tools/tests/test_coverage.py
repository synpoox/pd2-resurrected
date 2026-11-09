"""pd2_coverage on synthetic layouts, entities and a model_lod_desc.bin (no PD2 or D2R data)."""
import io
import json
import math
import struct
import sys
import tempfile
import unittest
import zlib
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_coverage as C  # noqa: E402
import pd2_presets as P  # noqa: E402

WALL = ("w", (("act1/outdoors/cliff.dt1", 1, 0, 0),))
TREE = ("w", (("act1/outdoors/trees.dt1", 1, 0, 0),))
FLOOR = ("f", (("act1/outdoors/floor.dt1", 0, 0, 0),))
CUBE = (-5.0, 0.0, -5.0, 5.0, 8.0, 5.0)  # 10 x 8 x 10: one tile, tall
FLAT = (-5.0, 0.0, -5.0, 5.0, 1.0, 5.0)


def entity(x: float, z: float, model: str = "", prefab: str = "", q=None, scale=None, y: float = 0.0) -> dict:
    t = {"type": "TransformDefinitionComponent", "position": {"x": x, "y": y, "z": z}}
    if q:
        t["orientation"] = q
    if scale:
        t["scale"] = scale
    comps = [t]
    if model:
        comps.append({"type": "ModelDefinitionComponent", "filename": model})
    if prefab:
        comps.append({"type": "PrefabPlacementDefinitionComponent", "prefab": prefab})
    return {"name": "e", "components": comps}


def yaw(deg: float) -> dict:
    r = math.radians(deg) / 2
    return {"x": 0.0, "y": math.sin(r), "z": 0.0, "w": math.cos(r)}


def lay(keys: dict, unwalkable=()) -> P.Layout:
    w = max(x for x, _ in keys) + 1
    h = max(y for _, y in keys) + 1
    return P.Layout(w, h, keys, {c: bytes(25) for c in keys}, frozenset(unwalkable))


class Models(unittest.TestCase):
    def test_fnv_is_case_blind(self):
        self.assertEqual(C.fnv("Data/HD/a.model"), C.fnv("data/hd/a.model"))
        self.assertEqual(C.fnv(""), 0x811C9DC5)

    def test_model_boxes(self):
        recs = [(C.fnv("a"), *CUBE), (C.fnv("b"), *FLAT)]
        raw = struct.pack("<Ii", len(recs), 8) + bytes(4) + b"".join(struct.pack("<I6f", *r) + bytes(16) for r in recs)
        boxes = C.model_boxes(raw)
        self.assertEqual(boxes[C.fnv("a")], CUBE)
        self.assertEqual(boxes[C.fnv("b")], FLAT)


class Boxes(unittest.TestCase):
    def found(self, entities, prefabs=None):
        models = {"cube": CUBE, "flat": FLAT}
        return list(C.boxes(entities, models.get, lambda p: (prefabs or {}).get(p, [])))

    def test_model_placed(self):
        self.assertEqual(self.found([entity(25, 35, "cube")]), [(20.0, 30.0, 30.0, 40.0, 8.0)])

    def test_unknown_model_and_terrain_skipped(self):
        terrain = entity(0, 0, "cube") | {"name": "terrain"}
        self.assertEqual(self.found([entity(0, 0, "nope"), terrain]), [])

    def test_rotation_and_scale(self):
        (x0, x1, z0, z1, top), = self.found([entity(0, 0, "cube", q=yaw(45), scale={"x": 2, "y": 3, "z": 1})])
        self.assertAlmostEqual(top, 24.0)
        self.assertAlmostEqual(x1 - x0, 15 * math.sqrt(2))  # a 20 x 10 box turned 45 degrees

    def test_prefab_follows_two_levels_composed(self):
        prefabs = {"outer": [entity(10, 0, prefab="inner")], "inner": [entity(10, 0, "cube")]}
        (x0, x1, z0, z1, top), = self.found([entity(100, 0, prefab="outer", q=yaw(90))], prefabs)
        # each offset of 10 turned 90 degrees about y: +x becomes -z
        self.assertAlmostEqual((x0 + x1) / 2, 100.0, places=4)
        self.assertAlmostEqual((z0 + z1) / 2, -20.0, places=4)
        self.assertAlmostEqual(top, 8.0)

    def test_third_prefab_level_not_followed(self):
        prefabs = {"a": [entity(0, 0, prefab="b")], "b": [entity(0, 0, prefab="c")], "c": [entity(0, 0, "cube")]}
        self.assertEqual(self.found([entity(0, 0, prefab="a")], prefabs), [])

    def test_prefab_scale_carries(self):
        prefabs = {"p": [entity(10, 0, "cube")]}
        (x0, x1, *_), = self.found([entity(0, 0, prefab="p", scale={"x": 2, "y": 1, "z": 1})], prefabs)
        self.assertAlmostEqual((x0 + x1) / 2, 20.0)
        self.assertAlmostEqual(x1 - x0, 20.0)


class Tall(unittest.TestCase):
    def test_footprint_and_height(self):
        self.assertEqual(C.tall_tiles(iter([(0.0, 19.0, 0.0, 9.0, 5.0), (50, 59, 50, 59, 3.0)])), {(0, 0), (1, 0)})

    def test_room_sized_left_out(self):
        self.assertEqual(C.tall_tiles(iter([(0.0, 500.0, 0.0, 9.0, 50.0)])), set())

    def test_a_low_piece_covers_a_fence_or_shore_tile_only(self):
        fence = ("w", (("act1/outdoors/stonewall.dt1", 1, 0, 0),))
        shore = ("f", (("act3/river/rivbank.dt1", 0, 0, 0),))
        L = lay({(0, 0): fence, (1, 0): WALL, (2, 0): shore})
        low = [(0.0, 29.0, 0.0, 9.0, 2.0)]  # a 2-unit edge piece over all three tiles
        self.assertEqual(C.covered(L, low, low), {(0, 0), (2, 0)})
        self.assertEqual(C.covered(L, [(10.0, 19.0, 0.0, 9.0, 5.0)]), {(1, 0)})

    def test_grass_or_rocks_on_a_fence_tile_cover_nothing(self):
        fence = ("w", (("act1/outdoors/stonewall.dt1", 1, 0, 0),))
        mixed = ("w", (("act1/outdoors/stonewall.dt1", 1, 0, 0), ("act1/outdoors/treegroups.dt1", 1, 0, 0)))
        L = lay({(0, 0): fence, (1, 0): mixed})
        low = [(0.0, 19.0, 0.0, 9.0, 2.0)]  # a 2-unit piece over both tiles
        self.assertEqual(C.covered(L, low), set())  # not an edge piece: grass, rocks
        self.assertEqual(C.covered(L, low, low), {(0, 0)})  # a tile with a non-fence layer needs a tall piece
        self.assertTrue(C.edge_piece("data/hd/env/model/act1/outdoors/fence_wood01.model"))
        self.assertFalse(C.edge_piece("data/hd/env/model/act3/river/riverrocks02.model"))
        self.assertFalse(C.edge_piece("data/hd/env/model/act1/outdoors/grass_tall01.model"))


class Scores(unittest.TestCase):
    def test_wall_score(self):
        s = C.score(lay({(0, 0): WALL, (1, 0): WALL, (2, 0): FLOOR, (3, 0): WALL}), {(0, 0), (2, 0)}, "a")
        self.assertEqual((s.walls, s.bare, s.ring), (3, {(1, 0), (3, 0)}, None))
        self.assertAlmostEqual(s.wall_score, 1 / 3)
        self.assertIsNone(s.border_score)

    def test_border_ring_gaps_leaks(self):
        # a 3 x 3 walkable floor framed by tree tiles, all unwalkable but (0, 2)
        keys = {(x, y): FLOOR for x in range(1, 4) for y in range(1, 4)}
        frame = {(x, y) for x in range(5) for y in range(5)} - set(keys)
        keys |= {c: TREE for c in frame}
        L = lay(keys, unwalkable=frame - {(0, 2)})
        tall = {c for c in frame if c != (4, 2)}
        s = C.score(L, tall, "b")
        # the frame is a tree line (pd2_painter.LINE), never playable even where walkable: a leak in the ring
        self.assertEqual(s.ring, frame - {(0, 0), (4, 0), (0, 4), (4, 4)})
        self.assertEqual(s.gaps, {(4, 2)})
        self.assertEqual(s.leaks, {(0, 2)})
        self.assertAlmostEqual(s.border_score, 1 - 1 / len(s.ring))

    def test_leak(self):
        keys = {(1, 1): FLOOR, (0, 1): TREE, (2, 1): TREE, (1, 0): TREE, (1, 2): TREE}
        L = P.Layout(3, 3, keys, {c: bytes(25) for c in keys}, frozenset({(0, 1), (1, 0), (1, 2)}))
        with mock.patch.object(C.painter, "playable", return_value={(1, 1)}):
            s = C.score(L, set(), "c")
        self.assertEqual(s.leaks, {(2, 1)})
        self.assertEqual(len(s.gaps), 4)


class Output(unittest.TestCase):
    def test_png_decodes(self):
        data = C.png(2, 1, {(0, 0): (255, 0, 0)}, zoom=2)
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        w, h = struct.unpack(">2I", data[16:24])
        self.assertEqual((w, h), (4, 2))
        n = struct.unpack(">I", data[33:37])[0]
        rows = zlib.decompress(data[41:41 + n])
        self.assertEqual(rows[:13], b"\0" + bytes((255, 0, 0)) * 2 + bytes((20, 20, 20)) * 2)

    def test_images(self):
        keys = {(0, 0): TREE, (1, 0): FLOOR}
        L = lay(keys, unwalkable={(0, 0)})
        s = C.score(L, set(), "t")
        self.assertEqual(set(C.images(L, s, set())), {"t_walls.png", "t_border.png"})

    def test_report_worst_first(self):
        a = C.Score("a", "Jungle", 10, {(0, 0)})
        b = C.Score("b", "Jungle", 10, set(range(5)), ring=set(range(10)), gaps=set(range(2)))
        c = C.Score("c", "Tomb", 10, set(range(7)))
        d = C.Score("d", "Mesa", 10, set(), ring=set(range(10)), gaps=set(range(6)))
        text = C.report([a, b, c, d], "abc123")
        self.assertIn("(abc123)", text)
        order = [line.split(" | ")[0][2:] for line in text.splitlines() if line.startswith("| ") and "Preset" not in line]
        self.assertEqual(order, ["d", "b", "c", "a"])
        self.assertIn("| d | Mesa | 40.0% | 6 | 0 | 100.0% | 10 | 0.0 | 0 | [walls](d_walls.png) [border](d_border.png) |", text)
        self.assertIn("All borders: 60.0% of 20 ring tiles.", text)

    def test_run_skips_without_models(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertIsNone(C.run(Path(d), Path(d), Path(d) / "out"))
            self.assertFalse((Path(d) / "out").exists())


class Budget(unittest.TestCase):
    def test_over_budget_listed_largest_first(self):
        small = C.Score("s", "S", 1, set(), size=1_000_000, entities=100)
        big = C.Score("b", "B", 1, set(), size=59_200_000, entities=60854, tiles=20000)
        many = C.Score("m", "M", 1, set(), size=2_000_000, entities=6000, tiles=1000)
        self.assertEqual([x.over_budget for x in (small, big, many)], [False, True, True])
        self.assertEqual([x.dense for x in (small, big, many)], [False, False, True])  # m: 6 entities a tile
        md = C.budget_md([small, many, big])
        self.assertIn("2 of 3 presets are above", md)
        self.assertLess(md.index("| b | B | 59.2 | 9.7 | 60854 | 10.8 | 3.0 | 3.0 |  |"),
                        md.index("| m | M | 2.0 | 0.3 | 6000 | 1.1 | 6.0 | 2.0 | **dense** |"))
        self.assertIn("## Size budget", C.report([small, many, big]))


class Baseline(unittest.TestCase):
    def scores(self):
        return [C.Score("b", "B", 10, set(range(3)), ring=set(range(8)), gaps={1}),
                C.Score("a", "A", 3, {(0, 0)})]

    def test_round_trip(self):
        text = C.baseline_text(self.scores())
        self.assertEqual(text.splitlines()[1:], ["a\t66.7\t-", "b\t70.0\t87.5"])
        self.assertEqual(C.read_baseline(text), {"a": (66.7, None), "b": (70.0, 87.5)})

    def test_no_change(self):
        base = C.read_baseline(C.baseline_text(self.scores()))
        self.assertEqual(C.changes(self.scores(), base), [])
        self.assertIn("No preset's coverage changed.", C.changes_md([]))

    def test_drop_within_and_beyond_tolerance(self):
        base = {"a": (67.5, None), "b": (71.5, 87.5)}  # a now 66.7 (-0.8), b wall 70.0 (-1.5)
        found = C.changes(self.scores(), base)
        self.assertEqual([(c.name, c.drop) for c in found], [("b", True), ("a", False)])
        md = C.changes_md(found)
        self.assertIn("| b | 71.5% -> 70.0% (-1.5) | 87.5% | **drop** |", md)
        self.assertIn("--write-baseline", md)

    def test_gain_new_and_gone(self):
        base = {"a": (50.0, None), "z": (90.0, 80.0)}
        found = {c.name: c for c in C.changes(self.scores(), base)}
        self.assertTrue(found["z"].drop)  # no longer scored
        self.assertFalse(found["a"].drop)  # a gain
        self.assertFalse(found["b"].drop)  # new
        md = C.changes_md(list(found.values()))
        self.assertIn("| b | new 70.0% | new 87.5% |", md)
        self.assertIn("| z | 90.0% -> gone | 80.0% -> gone | **drop** |", md)

    def test_border_drop_alone(self):
        found = C.changes(self.scores(), {"a": (66.7, None), "b": (70.0, 90.0)})
        self.assertEqual([(c.name, c.drop) for c in found], [("b", True)])

    def test_border_gone_is_a_drop(self):
        found = C.changes([C.Score("b", "B", 10, set(range(3)))], {"b": (70.0, 87.5)})
        self.assertEqual([(c.name, c.drop) for c in found], [("b", True)])
        self.assertIn("| b | 70.0% | 87.5% -> gone | **drop** |", C.changes_md(found))

    def test_report_carries_changes(self):
        found = C.changes(self.scores(), {"a": (66.7, None), "b": (70.0, 90.0)})
        text = C.report(self.scores(), "", found)
        self.assertLess(text.index("## Changes against the baseline"), text.index("| Preset | Area |"))

    def test_main_check_exit(self):
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "base.tsv"
            base.write_text("a\t99.0\t-\n", "utf-8")
            with mock.patch.object(C, "run", return_value=self.scores()), redirect_stdout(io.StringIO()):
                self.assertEqual(C.main(["--baseline", str(base), "--check"]), 1)
                self.assertEqual(C.main(["--baseline", str(base)]), 0)
                self.assertEqual(C.main(["--baseline", str(base), "--write-baseline"]), 0)
                self.assertEqual(C.main(["--baseline", str(base), "--check"]), 0)
            with mock.patch.object(C, "run", return_value=None):
                self.assertEqual(C.main(["--baseline", str(base), "--check"]), 0)  # skipped: no model file


if __name__ == "__main__":
    unittest.main()
