"""pd2_edge_tour on synthetic layouts and a synthetic levels.txt (no PD2 or D2R data, no game)."""
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_edge_tour as T  # noqa: E402
sys.path.insert(0, str(T.toolkit(T.ROOT) / "testing"))
import pd2_presets as P  # noqa: E402

WALL = ("w", (("act1/outdoors/ruins.dt1", 1, 0, 0),))
FENCE = ("w", (("act1/outdoors/fence.dt1", 1, 0, 0),))
SHORE = ("w", (("act1/outdoors/riverbank.dt1", 1, 0, 0),))
GRASS = ("f", (("act1/outdoors/grass.dt1", 0, 0, 0),))
WATER = ("f", (("act1/outdoors/water.dt1", 0, 0, 0),))
DIRT = ("f", (("act1/outdoors/dirt.dt1", 0, 0, 0),))
OPEN, SHUT = bytes(25), bytes([1] * 25)


def lay(keys: dict, flags: dict | None = None) -> P.Layout:
    w, h = max(x for x, _ in keys) + 1, max(y for _, y in keys) + 1
    return P.Layout(w, h, keys, {c: (flags or {}).get(c, OPEN) for c in keys})


def hall(w: int = 12, h: int = 8) -> P.Layout:
    """A w x h room of grass ringed by wall, with a fence on the left side and dirt in the right third."""
    keys = {(x, y): GRASS for x in range(w) for y in range(h)}
    keys.update({(x, y): DIRT for x in range(2 * w // 3, w - 1) for y in range(1, h - 1)})
    for x in range(w):
        keys[(x, 0)] = keys[(x, h - 1)] = WALL
    for y in range(h):
        keys[(w - 1, y)] = WALL
        keys[(0, y)] = FENCE
    return lay(keys)


class Warp(unittest.TestCase):
    # Torture Halls (levels.txt Id 189): OffsetX/OffsetY, as PR #725's game check used for tiles (48,16) and (19,32)
    OFFSET = (4000, 2000)

    def test_formula(self):
        self.assertEqual((T.subtile(10, 0), T.subtile(10, 3)), (52, 67))

    def test_torture_halls_spots(self):
        self.assertEqual(T.goto(189, self.OFFSET, (48, 16)), "cheat goto 189,20242,10082")
        self.assertEqual(T.goto(189, self.OFFSET, (19, 32)), "cheat goto 189,20097,10162")

    def test_offsets_read_from_levels_txt(self):
        with tempfile.TemporaryDirectory() as d:
            excel = Path(d) / "global" / "excel"
            excel.mkdir(parents=True)
            (excel / "levels.txt").write_text("Name\tId\tOffsetX\tOffsetY\nTorture Halls\t189\t4000\t2000\nBlank\t\t\t\n",
                                              "latin-1", newline="")
            self.assertEqual(T.offsets(Path(d)), {189: ("Torture Halls", 4000, 2000)})


class Geometry(unittest.TestCase):
    def test_components_are_8_connected_and_longest_first(self):
        runs = T.components({(0, 0), (1, 1), (2, 2), (9, 9), (5, 5), (5, 6)})
        self.assertEqual([len(r) for r in runs], [3, 2, 1])
        self.assertEqual(runs[0], [(0, 0), (1, 1), (2, 2)])

    def test_spread_starts_in_the_middle_then_goes_far(self):
        line = [(x, 0) for x in range(11)]
        self.assertEqual(T.spread(line, 1), [(5, 0)])
        self.assertEqual(T.spread(line, 3), [(5, 0), (10, 0), (0, 0)])
        self.assertEqual(T.spread(line, 20), T.spread(line, 11))
        self.assertEqual(T.spread([], 3), [])

    def test_pick_takes_each_group_before_a_second_tile_of_one(self):
        groups = [[(x, 0) for x in range(11)], [(0, 5), (1, 5)]]
        self.assertEqual(T.pick(groups, 3)[:2], [(5, 0), (0, 5)])
        self.assertEqual(len(T.pick(groups, 3)), 3)
        self.assertEqual(T.pick(groups, 3), T.pick(groups, 3))


class Edges(unittest.TestCase):
    def test_edge_types_and_their_standing_tiles(self):
        e = T.edge_tiles(hall())
        self.assertEqual(e["wall"], {(x, y) for x in range(1, 11) for y in (0, 7)} | {(11, y) for y in range(1, 7)})
        self.assertEqual(e["fence"], {(0, y) for y in range(1, 7)})
        self.assertEqual(e["cliff"], set())

    def test_fence_and_water_edge_are_split_by_their_dt1(self):
        keys = {(x, y): GRASS for x in range(6) for y in range(6)}
        keys.update({(0, y): FENCE for y in range(6)} | {(5, y): SHORE for y in range(6)})
        e = T.edge_tiles(lay(keys))
        self.assertEqual(e["fence"], {(0, y) for y in range(6)})
        self.assertEqual(e["water"], {(5, y) for y in range(6)})
        self.assertEqual(e["wall"], set())

    def test_water_is_blocked_liquid_floor_next_to_walkable_floor(self):
        keys = {(x, y): GRASS for x in range(6) for y in range(1, 4)}
        keys.update({(x, 0): WATER for x in range(6)})
        e = T.edge_tiles(lay(keys, {(x, 0): SHUT for x in range(6)}))
        self.assertEqual(e["water"], {(x, 0) for x in range(6)})
        self.assertEqual(T.edge_tiles(lay({c: GRASS for c in keys}, {(x, 0): SHUT for x in range(6)}))["water"], set())

    def test_a_stand_is_walkable_and_next_to_the_run(self):
        h = hall()
        for run in T.components(T.edge_tiles(h)["wall"]):
            walk = P.walkable(h)
            for s in T.stands(h, run):
                self.assertIn(s, walk)
                self.assertTrue(any((s[0] + dx, s[1] + dy) in run for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))))

    def test_floor_types_prefer_interior_tiles(self):
        floors = T.floor_types(hall())
        self.assertEqual(set(floors), {"floor:grass", "floor:dirt"})
        self.assertTrue(all(c in set(floors["floor:grass"]) for c in floors["floor:grass"]))
        for c in floors["floor:dirt"]:
            self.assertTrue(all(hall().keys[(c[0] + dx, c[1] + dy)] == DIRT for dx, dy in T.AROUND))


WARPS = """## Act 3

### Jungle Map (LevelId 149, lvlprest `Jungle Map`)

- **jungle1.ds1** (pin: `pd2_force_ds1.py --slot <N> "jungle1.ds1"`), 255x255 tiles
  - centre (tile 127,127): `cheat goto 149,16902,9997`
  - nearest walkable to centre (tile 133,162): `cheat goto 149,17167,10062`
  - waypoint (tile 236,237): `cheat goto 149,17682,10437`
  - monster preset x (tile 1,2): `cheat goto 149,1,2`
  - entry: `cheat goto 149`

- **Tomb1.ds1**, 10x10 tiles
  - centre (tile 5,5): `cheat goto 152,5,5`
  - entry: `cheat goto 152`
"""


class Sweep(unittest.TestCase):
    def test_warp_doc_keeps_the_walkable_references(self):
        w = T.parse_warps(WARPS)
        self.assertEqual([r[0] for r in w["jungle1"]], ["nearest walkable to centre", "waypoint", "entry"])
        self.assertEqual(w["jungle1"][0][1:], ((133, 162), "cheat goto 149,17167,10062"))
        self.assertEqual([r[0] for r in w["tomb1"]], ["centre", "entry"])  # no 'nearest' line: the centre is walkable

    def test_variant_points_put_references_first_then_one_per_type(self):
        m = T.Map("Jungle Map", 149, "expansion/map/jungle1.ds1", [])
        plan = T.plan(hall(), "Jungle Map", "jungle1", 149, (0, 0), n=3)
        pts = T.variant_points(T.parse_warps(WARPS)["jungle1"], plan, m, 6)
        self.assertEqual(len(pts), 6)
        self.assertEqual([p.type for p in pts[:2]], ["ref-nearest-walkable-to-centre", "ref-waypoint"])
        self.assertEqual(len({p.type for p in pts[2:]}), 4)  # four different types before a second of one
        self.assertEqual(T.variant_points([("entry", None, "cheat goto 149")], [], m, 6)[0].command, "cheat goto 149")

    def test_free_slots_leave_keep_free_unlocked(self):
        try:
            import game_slots as gs
        except ImportError:
            self.skipTest("no d2rAI-toolkit beside the repo")
        with mock.patch.object(gs, "lock_info", lambda cfg, s: object() if s == 5 else None),                 mock.patch.object(gs, "slot_processes", lambda cfg, s: []):
            self.assertEqual(T.free_slots(None, [4, 5, 6, 7], 1), [4, 6])
            self.assertEqual(T.free_slots(None, [4, 5], 2), [])


class Plan(unittest.TestCase):
    def test_plan_gives_each_type_up_to_n_points_with_commands(self):
        pts = T.plan(hall(), "Test Map", "hall1", 189, (4000, 2000), n=3)
        by = {}
        for p in pts:
            by.setdefault(p.type, []).append(p)
        self.assertEqual(set(by), {"wall", "fence", "floor:grass", "floor:dirt"})  # edges by pd2_fidelity.classify
        self.assertTrue(all(len(v) <= 3 for v in by.values()))
        p = by["fence"][0]
        self.assertEqual(p.command, T.goto(189, (4000, 2000), p.tile))
        self.assertEqual(p.name, f"hall1-fence_{p.tile[0]}-{p.tile[1]}")

    def test_a_type_filter_keeps_the_edge_or_the_whole_floor_group(self):
        h = hall()
        self.assertEqual({p.type for p in T.plan(h, "m", "d", 1, (0, 0), ["wall"])}, {"wall"})
        self.assertEqual({p.type for p in T.plan(h, "m", "d", 1, (0, 0), ["floor"])}, {"floor:grass", "floor:dirt"})
        self.assertEqual({p.type for p in T.plan(h, "m", "d", 1, (0, 0), ["floor:dirt"])}, {"floor:dirt"})

    def test_select_matches_preset_name_or_ds1_stem(self):
        found = [T.Map("Torture Halls Map", 189, "pd2assets/torture/halls1.ds1", []),
                 T.Map("Nemyr Monastery Map", 192, "pd2assets/nemyr/nemyr.ds1", [])]
        self.assertEqual([m.level for m in T.select(found, "torture")], [189])
        self.assertEqual([m.level for m in T.select(found, "NEMYR")], [192])
        self.assertEqual(len(T.select(found, "")), 2)

    def test_side_by_side_puts_the_first_shot_on_the_left(self):
        sys.path.insert(0, str(T.toolkit(T.ROOT) / "testing"))
        try:
            from game_ctl import Image
        except ImportError:
            self.skipTest("no d2rAI-toolkit beside the repo")
        a, b = Image(2, 1, bytes([1, 2, 3, 4, 5, 6])), Image(2, 1, bytes([7, 8, 9, 10, 11, 12]))
        both = T.side_by_side(a, b)
        self.assertEqual((both.width, both.height, list(both.rgb)), (4, 1, list(range(1, 13))))
        with self.assertRaises(ValueError):
            T.side_by_side(a, Image(1, 1, bytes(3)))


if __name__ == "__main__":
    unittest.main()
