"""pd2_edge_tour on synthetic layouts and a synthetic levels.txt (no PD2 or D2R data, no game)."""
import sys
import threading
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


class Sweep(unittest.TestCase):
    def test_variant_points_put_references_first_then_one_per_type(self):
        refs = [T.Point("J", "jungle1", 149, "ref-nearest", (1, 1), "cheat goto 149,1,1"),
                T.Point("J", "jungle1", 149, "ref-waypoint", (2, 2), "cheat goto 149,2,2")]
        plan = T.plan(hall(), "Jungle Map", "jungle1", 149, (0, 0), n=3)
        pts = T.variant_points(refs, plan, 6)
        self.assertEqual(len(pts), 6)
        self.assertEqual([p.type for p in pts[:2]], ["ref-nearest", "ref-waypoint"])
        self.assertEqual(len({p.type for p in pts[2:]}), 4)  # four different types before a second of one
        self.assertEqual(T.variant_points([], [], 6), [])

    def test_exact_select_takes_a_stem_or_preset_name_not_a_part(self):
        found = [T.Map("Library Map", 1, "x/library1.ds1", []), T.Map("Zhar Library Map", 2, "x/zharlibrary1.ds1", []),
                 T.Map("Poisoned Well Map", 199, "x/poisonedwell.ds1", []),
                 T.Map("Poisoned Well Map", 199, "x/poisonedwellv2.ds1", [])]
        self.assertEqual([m.level for m in T.select(found, "Library", True)], [1])
        self.assertEqual([m.ds1 for m in T.select(found, "poisonedwellV2", True)], ["x/poisonedwellv2.ds1"])
        self.assertEqual(len(T.select(found, "poisoned well map", True)), 2)
        self.assertEqual(len(T.select(found, "library")), 2)  # substring stays the default for list and shoot

    def test_poisoned_well_skips_legacy_by_level_and_palace_is_never_swept(self):
        self.assertIn(199, T.NO_LEGACY)
        self.assertEqual(T.PALACE, 141)

    def test_nap_raises_once_stopped(self):
        T.STOP.clear()
        T.nap(0)
        T.STOP.set()
        try:
            with self.assertRaises(T.Stopped):
                T.nap(5)
        finally:
            T.STOP.clear()

    def test_free_slots_leave_keep_free_unlocked(self):
        try:
            import game_slots as gs
        except ImportError:
            self.skipTest("no d2rAI-toolkit beside the repo")
        with mock.patch.object(gs, "lock_info", lambda cfg, s: object() if s == 5 else None),                 mock.patch.object(gs, "slot_processes", lambda cfg, s: []):
            self.assertEqual(T.free_slots(None, [4, 5, 6, 7], 1), [4, 6])
            self.assertEqual(T.free_slots(None, [4, 5], 2), [])


class DefectPoints(unittest.TestCase):
    M = T.Map("Jungle Map", 149, "x/jungle1.ds1", [])

    def walk(self, w=40, h=40):
        return {(x, y) for x in range(w) for y in range(h)}

    def test_worst_cluster_first_on_a_walkable_tile_near_its_centre(self):
        sets = {"edge": {(25 + i, 30) for i in range(3)}, "void": {(5, 5)}}
        pts = T.defect_points(sets, set(), self.walk(), self.M, (0, 0), 5)
        self.assertEqual([p.type for p in pts], ["defect-edge", "defect-void"])
        self.assertEqual(pts[0].tile, (26, 30))
        self.assertEqual(pts[0].command, T.goto(149, (0, 0), (26, 30)))

    def test_heavier_tiles_rank_higher_and_the_type_is_the_heaviest_check(self):
        sets = {"clutter": {(2, 2), (3, 2)}, "solid": {(2, 3), (3, 3), (2, 4)}, "floating": {(20, 20)}}
        pts = T.defect_points(sets, set(), self.walk(), self.M, (0, 0), 5)
        self.assertEqual([p.type for p in pts], ["defect-solid", "defect-floating"])
        edge = T.defect_points({"floating": {(20, 20)}, "void": {(2, 2)}}, {(20, 20)}, self.walk(), self.M, (0, 0), 1)
        self.assertEqual(edge[0].type, "defect-floating")  # an edge tile weighs 3 against walkable floor's 2

    def test_points_are_spread_apart_and_capped_at_k(self):
        sets = {"void": {(x, 10) for x in range(0, 40, 2)}}  # cells 8 wide: neighbours are closer than APART
        pts = T.defect_points(sets, set(), self.walk(), self.M, (0, 0), 3)
        self.assertGreaterEqual(len(pts), 2)
        self.assertLessEqual(len(pts), 3)
        for i, a in enumerate(pts):
            for b in pts[i + 1:]:
                self.assertGreaterEqual(max(abs(a.tile[0] - b.tile[0]), abs(a.tile[1] - b.tile[1])), T.APART)

    def test_a_stand_off_the_defect_is_the_nearest_walkable_tile(self):
        walk = {(x, 0) for x in range(10)}
        pts = T.defect_points({"void": {(4, 6)}}, set(), walk, self.M, (0, 0), 1)
        self.assertEqual(pts[0].tile, (4, 0))

    def test_no_defects_or_no_walkable_floor_gives_nothing(self):
        self.assertEqual(T.defect_points({"void": set()}, set(), self.walk(), self.M, (0, 0), 5), [])
        self.assertEqual(T.defect_points({"void": {(1, 1)}}, set(), set(), self.M, (0, 0), 5), [])
        self.assertEqual(T.defect_points({"void": {(1, 1)}}, set(), self.walk(), self.M, (0, 0), 0), [])

    def test_variant_points_put_defects_after_references_and_fall_back_to_the_plan(self):
        refs = [T.Point("J", "jungle1", 149, "ref-nearest", (1, 1), "c"), T.Point("J", "jungle1", 149, "ref-waypoint", (2, 2), "c")]
        plan = T.plan(hall(), "Jungle Map", "jungle1", 149, (0, 0), n=3)
        defects = [T.Point("J", "jungle1", 149, "defect-void", (i, i), "c") for i in range(3)]
        pts = T.variant_points(refs, plan, 6, defects)
        self.assertEqual([p.type for p in pts[:4]], ["ref-nearest", "ref-waypoint"] + ["defect-void"] * 2)  # n=6 keeps 2 plan points
        self.assertEqual(len(pts), 6)
        self.assertEqual(T.variant_points(refs, plan, 6, []), T.variant_points(refs, plan, 6))
        self.assertEqual(len(T.variant_points(refs, plan, 4, defects)), 4)

    def test_checks_take_turns_and_each_is_capped(self):
        sets = {"floating": {(x, 5) for x in range(0, 200, 10)}, "void": {(5, 100)}, "edge": {(100, 100)}}
        pts = T.defect_points(sets, set(), self.walk(200, 200), self.M, (0, 0), 6)
        self.assertEqual({p.type for p in pts}, {"defect-floating", "defect-void", "defect-edge"})
        self.assertEqual(sum(p.type == "defect-floating" for p in pts), T.PER_CHECK)
        self.assertEqual({p.type for p in pts[:3]}, {"defect-floating", "defect-void", "defect-edge"})

    def test_points_keep_off_the_reference_tiles(self):
        sets = {"void": {(5, 5)}, "edge": {(30, 30)}}
        pts = T.defect_points(sets, set(), self.walk(), self.M, (0, 0), 5, [(5, 6)])
        self.assertEqual([p.type for p in pts], ["defect-edge"])

    def test_variant_points_keep_two_plan_points(self):
        refs = [T.Point("J", "j", 149, "ref-nearest", (1, 1), "c"), T.Point("J", "j", 149, "ref-waypoint", (2, 2), "c")]
        plan = T.plan(hall(), "Jungle Map", "j", 149, (0, 0), n=3)
        defects = [T.Point("J", "j", 149, "defect-void", (i, i), "c") for i in range(9)]
        pts = T.variant_points(refs, plan, 10, defects)
        self.assertEqual(len(pts), 10)
        self.assertEqual(sum(p.type.startswith("defect") for p in pts), 6)
        self.assertEqual(len([p for p in pts if p.type not in ("ref-nearest", "ref-waypoint") and not p.type.startswith("defect")]), 2)

    def test_parity_points_keep_only_what_pick_returns(self):
        def fake(data, d2r, out, only, images, on_row=None):
            row = {"name": "jungle1", "sets": {"void": {(1, 1)}}}
            on_row(row)
            del row["sets"]  # as pd2_parity.run does after on_row
        with mock.patch("pd2_parity.run", side_effect=fake):
            got = T.parity_points(Path("."), Path("."), {"jungle1"}, lambda r: sorted(r["sets"]), lambda *_: None)
        self.assertEqual(got, {"jungle1": ["void"]})

    def test_parity_points_survive_a_scorer_failure(self):
        logs = []
        with mock.patch("pd2_parity.run", side_effect=RuntimeError("boom")):
            self.assertEqual(T.parity_points(Path("."), Path("."), {"jungle1"}, dict, logs.append), {})
        self.assertIn("plan points", logs[0])


class SlotLoss(unittest.TestCase):
    def setUp(self):
        T.STOP.clear()
        self.addCleanup(T.STOP.clear)

    def run_worker(self, lock_results):
        calls = []
        held = {"ours": True}

        class Game:
            def __init__(self, *a, **k): pass
            def stop(self): calls.append("stop")
            def start(self): calls.append("start")

        class Slots:
            @staticmethod
            def acquire(cfg, slot, owner):
                ok = lock_results.pop(0) if lock_results else True
                held["ours"] = ok
                calls.append(f"lock:{ok}")
                return ok, "msg"
            @staticmethod
            def release(cfg, slot, owner):
                calls.append("release")
                return True, "released"

        class Force:
            @staticmethod
            def lvlprest_path(slot, owner=None):
                if not held["ours"]:  # the real one goes through check_slot, which refuses a lock that is not ours
                    raise RuntimeError("slot not locked by this owner")
                return "p"
            @staticmethod
            def apply(*a, **k):
                calls.append("apply")
                return ["ok"]
            @staticmethod
            def reset(*a, **k): calls.append("reset")

        gc = mock.Mock(Game=Game)
        tc = mock.Mock()
        jobs = [(T.Map("A Map", 1, "x/a1.ds1", []), []), (T.Map("B Map", 2, "x/b1.ds1", []), [])]
        results = []
        with mock.patch.object(T, "load_game_ctl", lambda: (gc, tc)),                 mock.patch.dict(sys.modules, {"game_slots": Slots, "pd2_force_ds1": Force}),                 mock.patch.object(T, "enter_hell", lambda g: None),                 mock.patch.object(T, "shoot_variant", lambda *a, **k: []),                 mock.patch.object(T, "finish_sheet", lambda *a, **k: {}),                 tempfile.TemporaryDirectory() as d:
            T.run_slot(4, jobs, Path(d), "me", 0, results, log=lambda *a: None)
        return calls, results

    def test_lost_lock_abandons_the_slot_without_stop_or_reset(self):
        calls, results = self.run_worker([True, False])  # initial lock ok, re-take after the first stop fails
        self.assertEqual(calls, ["lock:True", "stop", "lock:False"])  # no second stop, no reset, no release, no 2nd variant
        self.assertEqual(len(results), 2)
        self.assertIn("lock was taken", results[0]["error"])
        self.assertEqual(results[0]["left_file"], "p")
        self.assertEqual(results[1]["error"], "skipped: slot lock lost")  # the second variant is recorded, not dropped
        self.assertEqual(results[1]["ds1"], "b1")
        self.assertEqual(results[1]["left_file"], "p")

    def test_failed_first_lock_records_the_whole_share_as_skipped(self):
        calls, results = self.run_worker([False])
        self.assertEqual(calls, ["lock:False"])
        self.assertEqual([r["error"] for r in results], ["skipped: slot lock lost"] * 2)

    def test_normal_slot_still_stops_resets_and_releases(self):
        calls, results = self.run_worker([])
        self.assertEqual(calls[-3:], ["lock:True", "reset", "release"])
        self.assertEqual(len(results), 2)

    def test_ctrl_c_in_the_loop_test_still_sets_stop(self):
        seen = {}

        def worker(slot, share, out, owner, settle, results, log):
            seen["stopped"] = T.STOP.wait(5)

        class Th(threading.Thread):
            first = True
            def is_alive(self):
                if Th.first:
                    Th.first = False
                    raise KeyboardInterrupt
                return super().is_alive()

        with mock.patch.object(T, "run_slot", worker), mock.patch.object(T.threading, "Thread", Th),                 tempfile.TemporaryDirectory() as d:
            T.sweep([(T.Map("A Map", 1, "x/a1.ds1", []), [])], [4], Path(d), log=lambda *a: None)
            self.assertTrue((Path(d) / "sweep.json").read_text("utf-8").count('"interrupted": true'))
        self.assertTrue(seen["stopped"])


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
