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
        self.assertEqual(self.found([entity(25, 35, "cube")]), [(20.0, 30.0, 30.0, 40.0, 8.0, 0.0)])

    def test_unknown_model_and_terrain_skipped(self):
        terrain = entity(0, 0, "cube") | {"name": "terrain"}
        self.assertEqual(self.found([entity(0, 0, "nope"), terrain]), [])

    def test_rotation_and_scale(self):
        (x0, x1, z0, z1, top, _), = self.found([entity(0, 0, "cube", q=yaw(45), scale={"x": 2, "y": 3, "z": 1})])
        self.assertAlmostEqual(top, 24.0)
        self.assertAlmostEqual(x1 - x0, 15 * math.sqrt(2))  # a 20 x 10 box turned 45 degrees

    def test_prefab_follows_two_levels_composed(self):
        prefabs = {"outer": [entity(10, 0, prefab="inner")], "inner": [entity(10, 0, "cube")]}
        (x0, x1, z0, z1, top, _), = self.found([entity(100, 0, prefab="outer", q=yaw(90))], prefabs)
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


class OnPlanes(unittest.TestCase):
    """#742 sinks and raises the ground planes and the pieces on them: heights are measured from the plane."""
    def preset(self, y: float, *pieces) -> dict:
        plane = entity(85, 85, "plane", y=y) | {"name": "terrain"}
        plane["components"][1]["filename"] = f"data/hd/{P.TERRAIN}/plane.model"
        return {"entities": list(pieces), "terrain": plane}

    def tall(self, preset: dict, models: dict) -> set:
        return C.tall_tiles(C.boxes(preset["entities"], models.get, lambda p: [], ground=C.plane_ground(preset)))

    def test_wall_on_a_sunken_plane_is_tall(self):
        wall = entity(25, 25, "cube", y=-6.0)  # top at +2 in the world
        self.assertEqual(self.tall(self.preset(-6.0, wall), {"cube": CUBE}), {(x, y) for x in (2, 3) for y in (2, 3)})
        self.assertEqual(C.tall_tiles(C.boxes([wall], {"cube": CUBE}.get, lambda p: [])), set())  # no plane: the world top, +2

    def test_low_piece_on_a_raised_plane_is_not_tall(self):
        low = entity(25, 25, "low", y=6.0)  # top at +8 in the world
        models = {"low": (-5.0, 0.0, -5.0, 5.0, 2.0, 5.0)}
        self.assertEqual(self.tall(self.preset(6.0, low), models), set())
        self.assertEqual(C.tall_tiles(C.boxes([low], models.get, lambda p: [])), {(x, y) for x in (2, 3) for y in (2, 3)})

    def test_plane_is_per_block_and_prefab_stands_on_its_plane(self):
        preset = self.preset(-6.0, entity(25, 25, prefab="p", y=-6.0), entity(185, 25, "cube"))  # block 1 has no plane
        found = C.tall_tiles(C.boxes(preset["entities"], {"cube": CUBE}.get, lambda p: [entity(0, 0, "cube")],
                                     ground=C.plane_ground(preset)))
        self.assertEqual(found, {(x, y) for x in (2, 3, 18, 19) for y in (2, 3)})


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

    def test_wall_ok_replaces_the_tall_rule(self):
        s = C.score(lay({(0, 0): WALL, (1, 0): WALL}), {(0, 0)}, "a", wall_ok={(1, 0)})
        self.assertEqual(s.bare, {(0, 0)})

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

    def test_floor_score(self):
        # walkable floor at x 0-3; (2, 0) unwalkable, (3, 0) mostly blocked: neither counts
        keys = {(x, 0): FLOOR for x in range(4)} | {(4, 0): WALL}
        L = P.Layout(5, 1, keys, {c: bytes(25) for c in keys} | {(3, 0): bytes([1] * 13 + [0] * 12)},
                     frozenset({(2, 0)}))
        s = C.score(L, set(), "f", ground={(0, 0), (4, 0)})
        self.assertEqual((s.floors, s.floor_bare), (2, {(1, 0)}))
        self.assertAlmostEqual(s.floor_score, 0.5)
        self.assertEqual(C.score(L, set(), "f").floor_bare, set())  # no ground given: all covered

    def test_ground_tiles(self):
        plane = entity(5, 5, "plane") | {"name": "terrain"}
        room = entity(30, 5, "plane") | {"name": "terrain"}
        preset = {"entities": [room, entity(50, 50, "plane")], "terrain": plane}
        boxes = {"plane": (-5.0, 0.0, -5.0, 4.0, 0.0, 4.0)}.get
        # x 0-9 and 25-34; the entity not named terrain is left out
        self.assertEqual(C.ground_tiles(preset, boxes, lambda p: []), {(0, 0), (2, 0), (3, 0)})

    def test_ground_plane_ends_on_a_tile_edge(self):
        # a 170-unit plane from 0 covers tiles 0-16, not the 18th it ends on; the wall footprint keeps it
        plane = entity(85, 85, "plane") | {"name": "terrain"}
        boxes = {"plane": (-85.00001, 0.0, -85.0, 85.00003, 0.0, 85.0)}.get  # D2R's box: float noise at the edges
        self.assertEqual(C.ground_tiles({"entities": [], "terrain": plane}, boxes, lambda p: []),
                         {(x, y) for x in range(17) for y in range(17)})
        self.assertEqual(len(C.footprint(iter([(0.0, 170.0, 0.0, 10.0, 8.0)]))), 18 * 2)

    def test_wrong_tiles(self):
        # a 28-unit wide, 8 high structure; a one-tile cube and a 2-high slab are props
        models = {"stairs": (-14.0, 0.0, -14.0, 14.0, 8.0, 14.0), "cube": CUBE,
                  "low": (-14.0, 0.0, -14.0, 14.0, 2.0, 14.0)}
        ents = [entity(15, 15, "stairs"), entity(55, 5, "stairs"), entity(5, 35, "cube"), entity(35, 35, "low"),
                entity(35, 35, "low", y=10.0), entity(35, 35, "cube", y=-6.0)]  # a raised decal, a sunk mesh
        at = {(1, 1), (3, 3), (0, 3)}  # (5, 0), the second stairs' anchor, is not flat walkable floor
        wrong = C.wrong_tiles(ents, models.get, lambda p: [], at)
        self.assertEqual(wrong, {(x, y) for x in range(3) for y in range(3)})  # x and z 1-29: tiles 0-2
        L = lay({(x, y): FLOOR for x in range(4) for y in range(4)})
        s = C.score(L, set(), "w", ground={(0, 0)}, wrong=wrong)
        self.assertEqual(s.floor_wrong, wrong)
        self.assertAlmostEqual(s.floor_score, 1 - len(s.floor_bare | wrong) / 16)

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
        self.assertEqual(set(C.images(L, s, set())), {"t_walls.png", "t_floor.png", "t_border.png"})

    def test_report_worst_first(self):
        a = C.Score("a", "Jungle", 10, {(0, 0)})
        b = C.Score("b", "Jungle", 10, set(range(5)), ring=set(range(10)), gaps=set(range(2)))
        c = C.Score("c", "Tomb", 10, set(range(7)))
        d = C.Score("d", "Mesa", 10, set(), ring=set(range(10)), gaps=set(range(6)))
        text = C.report([a, b, c, d], "abc123")
        self.assertIn("(abc123)", text)
        order = [line.split(" | ")[0][2:] for line in text.splitlines() if line.startswith("| ") and "Preset" not in line]
        self.assertEqual(order, ["d", "b", "c", "a"])
        self.assertIn("| d | Mesa | 40.0% | 6 | 0 | 100.0% | 10 | 100.0% | 0 | 0 | 0.0 | 0 | [walls](d_walls.png) [floor](d_floor.png) [border](d_border.png) |", text)
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
        self.assertEqual(text.splitlines()[1:], ["a\t66.7\t-\t100.0", "b\t70.0\t87.5\t100.0"])
        self.assertEqual(C.read_baseline(text), {"a": (66.7, None, 100.0), "b": (70.0, 87.5, 100.0)})

    def test_old_baseline_without_floor(self):
        base = C.read_baseline("a\t66.7\t-\nb\t70.0\t87.5\n")
        self.assertEqual(base["a"], (66.7, None, None))
        self.assertEqual(C.changes(self.scores(), base), [])  # no floor in the baseline: no floor change

    def test_floor_drop(self):
        s = C.Score("a", "A", 3, {(0, 0)}, floors=10, floor_bare={(1, 1), (2, 2)})
        found = C.changes([s], {"a": (66.7, None, 90.0)})
        self.assertEqual([(c.name, c.drop, c.floor) for c in found], [("a", True, (90.0, 80.0))])
        self.assertIn("| a | 66.7% | - | 90.0% -> 80.0% (-10.0) | **drop** |", C.changes_md(found))

    def test_no_change(self):
        base = C.read_baseline(C.baseline_text(self.scores()))
        self.assertEqual(C.changes(self.scores(), base), [])
        self.assertIn("No preset's coverage changed.", C.changes_md([]))

    def test_drop_within_and_beyond_tolerance(self):
        base = {"a": (69.0, None, 100.0), "b": (76.0, 87.5, 100.0)}  # a now 66.7 (-2.3), b wall 70.0 (-6.0)
        found = C.changes(self.scores(), base)
        self.assertEqual([(c.name, c.drop, c.within) for c in found], [("b", True, False), ("a", False, True)])
        md = C.changes_md(found)
        self.assertIn("| b | 76.0% -> 70.0% (-6.0) | 87.5% | 100.0% | **drop** |", md)
        self.assertIn("| a | 69.0% -> 66.7% (-2.3) | - | 100.0% | within tolerance |", md)
        self.assertIn("coverage-drop-ok", md)

    def test_the_limit_is_one_value_with_a_default(self):
        self.assertEqual(C.TOLERANCE, 3.0)
        self.assertFalse(C.changes(self.scores(), {"a": (69.7, None, 100.0)})[0].drop)  # exactly 3.0 down
        self.assertTrue(C.changes(self.scores(), {"a": (69.8, None, 100.0)})[0].drop)  # 3.1 down
        self.assertIn("the 2-point tolerance", C.changes_md(C.changes(self.scores(), {"a": (80.0, None, 100.0)}, 2.0), 2.0))
        self.assertIn("the 2-point tolerance", C.report(self.scores(), "", C.changes(self.scores(), {"a": (80.0, None, 100.0)}, 2.0), 2.0))
        self.assertTrue(C.changes(self.scores(), {"a": (68.0, None, 100.0)}, tolerance=1.0)[0].drop)

    def test_gain_new_and_gone(self):
        base = {"a": (50.0, None, 100.0), "z": (90.0, 80.0, 100.0)}
        found = {c.name: c for c in C.changes(self.scores(), base)}
        self.assertTrue(found["z"].drop)  # no longer scored
        self.assertFalse(found["a"].drop)  # a gain
        self.assertFalse(found["b"].drop)  # new
        md = C.changes_md(list(found.values()))
        self.assertIn("| b | new 70.0% | new 87.5% | new 100.0% |", md)
        self.assertIn("| z | 90.0% -> gone | 80.0% -> gone | 100.0% -> gone | **drop** |", md)

    def test_border_drop_alone(self):
        found = C.changes(self.scores(), {"a": (66.7, None, 100.0), "b": (70.0, 95.0, 100.0)})
        self.assertEqual([(c.name, c.drop) for c in found], [("b", True)])

    def test_border_gone_is_a_drop(self):
        found = C.changes([C.Score("b", "B", 10, set(range(3)))], {"b": (70.0, 87.5, 100.0)})
        self.assertEqual([(c.name, c.drop) for c in found], [("b", True)])
        self.assertIn("| b | 70.0% | 87.5% -> gone | 100.0% | **drop** |", C.changes_md(found))

    def test_report_carries_changes(self):
        found = C.changes(self.scores(), {"a": (66.7, None, 100.0), "b": (70.0, 90.0, 100.0)})
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

    def test_drop_table_lists_map_type_before_after_delta(self):
        found = C.changes(self.scores(), {"a": (99.0, None, 100.0), "b": (70.0, 90.0, 100.0)})
        fd = mock.Mock(kind="wall", score=(90.0, 80.0), spacing=(None, None), small=False, drop=True)
        fd.name = "m"
        rows = C.drop_rows(found, [fd])
        self.assertEqual([r[:2] for r in rows if r[0] == "m"], [("m", "fidelity wall")])
        self.assertIn(("a", "coverage wall", 99.0, 66.7, -32.3, C.DROP), [r[:6] for r in rows])
        self.assertIn(("b", "coverage border", 90.0, 87.5, -2.5, C.WITHIN), [r[:6] for r in rows])  # listed, passes
        text = C.drop_table(rows)
        self.assertIn("before", text.splitlines()[0])
        self.assertTrue(any(l.startswith("a ") and "-32.3" in l for l in text.splitlines()))

    def test_check_passes_a_drop_within_tolerance_and_lists_it(self):
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "base.tsv"
            base.write_text("a\t69.0\t-\nb\t70.0\t87.5\n", "utf-8")  # a: -2.3 (within 3)
            out = Path(d) / "out"
            buf = io.StringIO()
            with mock.patch.object(C, "run", return_value=self.scores()), redirect_stdout(buf):
                self.assertEqual(C.main(["--out", str(out), "--baseline", str(base), "--check"]), 0)
                self.assertIn("| a | coverage wall | 69% | 66.7% | -2.3 | within tolerance |",
                              (out / "drops.md").read_text("utf-8"))
                self.assertEqual(C.main(["--out", str(out), "--baseline", str(base), "--check", "--tolerance", "1"]), 1)
            self.assertIn("within tolerance", buf.getvalue())

    def test_a_rerun_with_no_falls_clears_the_old_drop_table(self):
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "base.tsv"
            base.write_text("a\t80.0\t-\n", "utf-8")
            with mock.patch.object(C, "run", return_value=self.scores()), redirect_stdout(io.StringIO()):
                C.main(["--out", d, "--baseline", str(base)])
                self.assertTrue((Path(d) / "drops.md").is_file())
                base.write_text("a\t66.7\t-\nb\t70.0\t87.5\n", "utf-8")
                C.main(["--out", d, "--baseline", str(base)])
            self.assertFalse((Path(d) / "drops.md").exists())

    def test_labels_follow_the_check_rules(self):
        import pd2_fidelity as F
        # a small map's spacing falling past the tolerance: never a drop, never "within tolerance"
        base = {("m", "fence"): (100.0, 80.0)}
        small = F.changes({"m": {"fence": F.TypeScore(F.MIN_TILES - 1, piled=19)}}, base)
        got = C.drop_rows([], small)
        self.assertEqual([(r.type, r.status) for r in got], [("fidelity fence spacing", C.SMALL)])
        self.assertFalse(small[0].drop)
        self.assertFalse(small[0].within)  # agrees with drops.md: not marked within tolerance
        # a coverage wall or floor gone while the preset is scored is no drop; a border gone is
        wall_none = C.Change("p", (80.0, 70.0), (None, None), (90.0, None), False, False)
        self.assertEqual([(r.type, r.status) for r in C.drop_rows([wall_none], [])],
                         [("coverage wall", C.DROP), ("coverage floor", C.WITHIN)])
        border_none = C.Change("p", (80.0, 80.0), (90.0, None), (None, None), True)
        self.assertEqual([(r.type, r.status) for r in C.drop_rows([border_none], [])], [("coverage border", C.DROP)])

    def test_a_drop_past_the_tolerance_fails_and_the_summary_marks_it(self):
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "base.tsv"
            base.write_text("a\t80.0\t-\n", "utf-8")
            with mock.patch.object(C, "run", return_value=self.scores()), redirect_stdout(io.StringIO()):
                self.assertEqual(C.main(["--out", d, "--baseline", str(base), "--check"]), 1)
                self.assertEqual(C.main(["--out", d, "--baseline", str(base)]), 0)  # no --check: label coverage-drop-ok
            self.assertIn("**drop**", (Path(d) / "drops.md").read_text("utf-8"))

    def test_fidelity_drop_inside_and_past_the_tolerance(self):
        import pd2_fidelity as F
        main = {("m", "wall"): (100.0, 90.0), ("n", "wall"): (100.0, 100.0)}
        rows = {"m": {"wall": F.TypeScore(40, missing={(0, 0)})},
                "n": {"wall": F.TypeScore(40, missing={(i, 0) for i in range(4)})}}
        fch = F.changes(rows, main)
        self.assertEqual({(c.name, c.drop, c.within) for c in fch}, {("m", False, True), ("n", True, False)})
        got = {(r.name, r.type, r.status) for r in C.drop_rows([], fch)}
        self.assertIn(("m", "fidelity wall", C.WITHIN), got)
        self.assertIn(("n", "fidelity wall", C.DROP), got)

    def test_drop_rows_carry_the_kind_of_the_dropped_tiles(self):
        import pd2_fidelity as F
        now = {"m": {"wall": F.TypeScore(40, missing={(5, 5)})}}
        fch = F.changes(now, {("m", "wall"): (100.0, 100.0)})
        tiles = {("m", "wall"): {"bad": set(), "spill": {(5, 5)}}}
        row, = C.drop_rows([], fch, rows=now, tiles=tiles)
        self.assertEqual(row.kinds, "1 " + F.SPILL_KIND)
        self.assertIn(F.SPILL_KIND, C.drop_md([row]))
        # a pair main scored perfectly has no line in the tiles file: its dropped tiles are classified, as "other"
        row, = C.drop_rows([], fch, rows=now, tiles={("other", "wall"): {}})
        self.assertEqual(row.kinds, "1 " + F.OTHER_KIND)

    def test_core_footprint_leaves_out_tiles_a_box_only_grazes(self):
        T = P.TILE
        self.assertEqual(C.core_footprint((0.1 * T, 3.4 * T, 0.0, 0.9 * T, 1, 0)), {(t, 0) for t in range(3)})
        self.assertEqual(C.core_footprint((0.9 * T, 2.1 * T, 0.0, 0.9 * T, 1, 0)), {(1, 0)})  # 0.1 spills each side
        self.assertEqual(C.core_footprint((0.8 * T, 1.3 * T, 0.0, 0.2 * T, 1, 0)), {(1, 0)})  # a small box: its centre

    def test_check_prints_the_drop_table(self):
        with tempfile.TemporaryDirectory() as d:
            base = Path(d) / "base.tsv"
            base.write_text("a\t99.0\t-\n", "utf-8")
            buf = io.StringIO()
            with mock.patch.object(C, "run", return_value=self.scores()), redirect_stdout(buf):
                C.main(["--out", str(Path(d) / "new" / "coverage"), "--baseline", str(base), "--check"])
            self.assertIn("coverage wall", buf.getvalue())
            self.assertTrue((Path(d) / "new" / "coverage").is_dir())  # a missing out folder is created

    def test_baseline_defaults_to_main_artifact_and_tsv_flag_skips_it(self):
        with tempfile.TemporaryDirectory() as d:
            art = Path(d) / "art.tsv"
            art.write_text("a\t66.7\t-\n", "utf-8")
            with mock.patch.object(C, "run", return_value=self.scores()) as run,                     mock.patch.object(C, "main_baseline", return_value=art) as fetch, redirect_stdout(io.StringIO()):
                C.main(["--out", str(Path(d) / "o"), "--check"])
                self.assertEqual(fetch.call_count, 1)
                self.assertEqual(run.call_args.args[4], C.read_baseline(art.read_text("utf-8")))
                C.main(["--out", str(Path(d) / "o"), "--tsv"])
                self.assertEqual(fetch.call_count, 1)  # --tsv: no download

    def test_main_baseline_falls_back_to_the_tsv_offline(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.object(C.subprocess, "run", side_effect=OSError("no gh")),                 mock.patch("sys.stderr", new_callable=io.StringIO) as err:
            self.assertEqual(C.main_baseline(Path(d) / "m"), C.BASELINE)
            self.assertIn("warning", err.getvalue())

    def test_main_baseline_downloads_the_artifact(self):
        with tempfile.TemporaryDirectory() as d:
            into = Path(d) / "m"

            def fake(cmd, **kw):
                if cmd[1] == "run" and cmd[2] == "download":
                    into.mkdir(parents=True, exist_ok=True)
                    (into / "baseline.tsv").write_text("a\t1.0\t-\n", "utf-8")
                return mock.Mock(stdout="123\n")
            with mock.patch.object(C.subprocess, "run", side_effect=fake), redirect_stdout(io.StringIO()):
                self.assertEqual(C.main_baseline(into), into / "baseline.tsv")


if __name__ == "__main__":
    unittest.main()
