"""pd2_overrides on synthetic presets (no PD2 or D2R data): each op, the selectors, stale detection, the rule and lint."""
import copy
import io
import json
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_overrides as O  # noqa: E402
from presets.consts import PRESETS, TERRAIN, TILE  # noqa: E402

PLANE = 3.0  # y of the flat ground plane
FERN = "data/hd/env/preset/act3/jungle/act3_ferns/fern01.model"
ROCK = "data/hd/env/preset/act3/jungle/act3_jungle_rocks/rock_pile01.model"


def ent(name: str, x: float, z: float, model: str = "", prefab: str = "", y: float = 9.0, id_: int = 1) -> dict:
    comps = [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": y, "z": z}}]
    if model:
        comps.append({"type": "ModelDefinitionComponent", "filename": model})
    if prefab:
        comps.append({"type": "PrefabPlacementDefinitionComponent", "prefab": prefab})
    return {"id": id_, "name": name, "components": comps}


def preset() -> dict:
    terrain = ent("terrain", 0.0, 0.0, model=f"data/hd/env/preset/{TERRAIN}/terrain.model", y=PLANE, id_=1)
    return {"dependencies": {"models": [{"path": FERN}]}, "entities": [
        terrain,
        ent("fern", 5 * TILE + 3, 7 * TILE + 3, FERN, id_=2),  # tile (5, 7)
        ent("fern", 6 * TILE + 3, 7 * TILE + 3, FERN, id_=3),  # tile (6, 7)
        ent("pillar", 20 * TILE + 1, 20 * TILE + 1, "x/stone_pillar_a.model", y=PLANE, id_=4),
        ent("tree", 30 * TILE + 5, 31 * TILE + 5, prefab="x/pf_tree.json", id_=5),
    ]}


def by_id(p: dict, i: int) -> dict | None:
    return next((e for e in p["entities"] if e["id"] == i), None)


def pos(e: dict) -> dict:
    return O._transform(e)["position"]


class OpTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d2r = Path(self.tmp.name)
        src = self.d2r / "hd/env/preset/act3/jungle"
        src.mkdir(parents=True)
        (src / "boge.json").write_text(json.dumps({
            "dependencies": {"models": [{"path": ROCK}], "empty": []},
            "entities": [ent("rock", 1.0, 2.0, ROCK, y=0.0, id_=77)]}))
        self.p = preset()

    def tearDown(self):
        self.tmp.cleanup()

    def run_ops(self, *ops):
        return O.apply_ops(self.p, list(ops), self.d2r)

    def test_remove_by_tile_and_model(self):
        self.assertEqual(self.run_ops({"op": "remove", "at": [5, 7], "model": "*/act3_ferns/*"}), [])
        self.assertIsNone(by_id(self.p, 2))
        self.assertIsNotNone(by_id(self.p, 3))

    def test_area_is_inclusive(self):
        self.run_ops({"op": "remove", "area": [5, 7, 6, 7]})
        self.assertEqual([e["id"] for e in self.p["entities"]], [1, 4, 5])

    def test_ground_sets_y_to_the_plane(self):
        self.assertEqual(self.run_ops({"op": "ground", "area": [0, 0, 9, 9], "model": "*/act3_ferns/*"}), [])
        self.assertEqual({pos(by_id(self.p, i))["y"] for i in (2, 3)}, {PLANE})
        self.assertEqual(pos(by_id(self.p, 5))["y"], 9.0)

    def test_move_by_and_to_tile(self):
        self.run_ops({"op": "move", "at": [20, 20], "by": [5, 1, -1]})
        p = pos(by_id(self.p, 4))
        self.assertEqual((p["x"], p["y"], p["z"]), (20 * TILE + 6, PLANE + 1, 20 * TILE))
        self.run_ops({"op": "move", "name": "tree", "to": [10, 12]})
        p = pos(by_id(self.p, 5))
        self.assertEqual((p["x"], p["z"], p["y"]), (10 * TILE + 5, 12 * TILE + 5, PLANE))

    def test_swap_model_and_prefab_adds_the_dependency(self):
        self.run_ops({"op": "swap", "at": [20, 20], "to": "x/other.model"},
                     {"op": "swap", "at": [30, 31], "to": "x/pf_other.json"})
        self.assertEqual(by_id(self.p, 4)["components"][1]["filename"], "x/other.model")
        self.assertEqual(by_id(self.p, 5)["components"][1]["prefab"], "x/pf_other.json")
        self.assertIn("x/other.model", [m["path"] for m in self.p["dependencies"]["models"]])

    def test_add_copies_the_pick_onto_the_ground_with_a_fresh_id(self):
        self.assertEqual(self.run_ops({"op": "add", "at": [8, 9], "from": "data/hd/env/preset/act3/jungle/boge.json",
                                       "pick": "*/act3_jungle_rocks/*"}), [])
        e = self.p["entities"][-1]
        self.assertEqual(e["id"], 6)
        self.assertEqual((pos(e)["x"], pos(e)["y"], pos(e)["z"]), (8 * TILE + TILE / 2, PLANE, 9 * TILE + TILE / 2))
        self.assertTrue(e["name"].startswith("pd2r_override:"))
        self.assertIn(ROCK, [m["path"] for m in self.p["dependencies"]["models"]])
        self.assertNotIn("empty", self.p["dependencies"])

    def test_add_keeps_the_picked_entitys_scale(self):
        src = json.loads((self.d2r / "hd/env/preset/act3/jungle/boge.json").read_text())
        src["entities"][0]["components"][0]["scale"] = {"x": 1.45, "y": 1.0, "z": 1.45}
        (self.d2r / "hd/env/preset/act3/jungle/boge.json").write_text(json.dumps(src))
        self.run_ops({"op": "add", "at": [8, 9], "from": "data/hd/env/preset/act3/jungle/boge.json",
                      "pick": "*/act3_jungle_rocks/*"})
        self.assertEqual(O._transform(self.p["entities"][-1])["scale"], {"x": 1.45, "y": 1.0, "z": 1.45})

    def test_add_with_no_matching_pick_is_stale(self):
        stale = self.run_ops({"op": "add", "at": [8, 9], "from": "data/hd/env/preset/act3/jungle/boge.json",
                              "pick": "*/nothing/*"})
        self.assertEqual(len(stale), 1)
        self.assertEqual(len(self.p["entities"]), 5)

    def test_pos_selector_picks_one_entity_by_generated_position(self):
        self.run_ops({"op": "remove", "pos": [6 * TILE + 3, 7 * TILE + 3], "expect": 1})
        self.assertIsNone(by_id(self.p, 3))
        self.assertIsNotNone(by_id(self.p, 2))

    def test_terrain_is_never_selected(self):
        self.assertEqual(len(self.run_ops({"op": "remove", "area": [0, 0, 1, 1]})), 1)
        self.assertIsNotNone(by_id(self.p, 1))

    def test_ids_and_order_do_not_matter(self):
        self.p["entities"].reverse()
        for i, e in enumerate(self.p["entities"]):
            e["id"] = 100 + i
        self.assertEqual(self.run_ops({"op": "remove", "at": [20, 20], "model": "*/stone_pillar*"}), [])
        self.assertEqual(len(self.p["entities"]), 4)

    def test_stale_op_matching_nothing(self):
        before = copy.deepcopy(self.p)
        stale = self.run_ops({"op": "remove", "at": [0, 0], "model": "*/no_such_piece.model"})
        self.assertEqual(len(stale), 1)
        self.assertIn("matched 0", stale[0])
        self.assertEqual(self.p, before)

    def test_stale_when_expect_differs(self):
        stale = self.run_ops({"op": "ground", "area": [0, 0, 9, 9], "model": "*/act3_ferns/*", "expect": 1})
        self.assertEqual(len(stale), 1)
        self.assertIn("expected 1", stale[0])
        self.assertEqual(pos(by_id(self.p, 2))["y"], 9.0)  # a stale op changes nothing

    def test_a_stale_op_does_not_stop_the_next_ones(self):
        stale = self.run_ops({"op": "remove", "at": [0, 0], "model": "*/nope*"},
                             {"op": "remove", "at": [20, 20]})
        self.assertEqual(len(stale), 1)
        self.assertIsNone(by_id(self.p, 4))

    def test_malformed_ops_raise(self):
        for op in ({"op": "frob", "at": [1, 1]}, {"op": "remove"}, {"op": "move", "at": [1, 1]},
                   {"op": "move", "at": [1, 1], "by": [0, 0, 0], "to": [2, 2]}, {"op": "swap", "at": [1, 1]},
                   {"op": "add", "at": [1, 1]}):
            with self.assertRaises(ValueError, msg=op):
                self.run_ops(op)


class RuleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.rule, self.out, self.d2r = t / "merge" / "presets-overrides.tsv", t / "generated" / "data", t / "d2r"
        (t / "merge").mkdir()
        self.rule.write_text("# anchor\n")
        (self.out / PRESETS / "act3").mkdir(parents=True)
        (self.out / PRESETS / "act3" / "jungle1.json").write_text(json.dumps(preset()))
        self.dir = t / "merge" / "presets-overrides.d" / "jungle"
        self.dir.mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def spec(self, ops, ds1="jungle1", name="jungle1"):
        (self.dir / f"{name}.json").write_text(json.dumps({"ds1": ds1, "ops": ops}))

    def apply(self):
        with redirect_stdout(io.StringIO()):
            O.apply(self.rule, self.rule.parent, self.d2r, self.out)

    def test_apply_edits_the_generated_preset_and_writes_a_clean_report(self):
        self.spec([{"op": "remove", "at": [20, 20]}])
        self.apply()
        p = json.loads((self.out / PRESETS / "act3" / "jungle1.json").read_text())
        self.assertEqual(len(p["entities"]), 4)
        self.assertEqual(O.lint(self.out), 0)

    def test_lint_fails_on_a_stale_op(self):
        self.spec([{"op": "remove", "at": [0, 0], "model": "*/gone*"}])
        self.apply()
        with redirect_stdout(io.StringIO()) as o:
            self.assertEqual(O.lint(self.out), 1)
        self.assertIn("STALE", o.getvalue())
        self.assertIn("jungle1", o.getvalue())

    def test_lint_without_a_report_fails(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(O.lint(self.out), 1)

    def test_a_file_for_a_missing_preset_fails_the_merge(self):
        self.spec([{"op": "remove", "at": [1, 1]}], ds1="nowhere", name="nowhere")
        with self.assertRaises(ValueError):
            self.apply()

    def test_a_file_whose_ds1_is_not_its_name_fails(self):
        self.spec([{"op": "remove", "at": [1, 1]}], ds1="jungle1", name="jungle2")
        with self.assertRaises(ValueError):
            self.apply()

    def test_a_skip_replaces_an_old_report(self):
        self.spec([{"op": "remove", "at": [0, 0], "model": "*/gone*"}])
        self.apply()
        shutil.rmtree(self.out / PRESETS)
        self.apply()
        self.assertEqual(O.lint(self.out), 0)

    def test_same_stem_in_two_folders_needs_a_path(self):
        other = self.out / PRESETS / "guild"
        other.mkdir()
        (other / "jungle1.json").write_text(json.dumps(preset()))
        self.spec([{"op": "remove", "at": [20, 20]}])
        with self.assertRaises(ValueError) as c:
            self.apply()
        self.assertIn("ambiguous", str(c.exception))
        self.spec([{"op": "remove", "at": [20, 20]}], ds1="guild/jungle1")
        self.apply()
        n = lambda d: len(json.loads((self.out / PRESETS / d / "jungle1.json").read_text())["entities"])
        self.assertEqual((n("act3"), n("guild")), (5, 4))

    def test_no_generated_presets_skips(self):
        shutil.rmtree(self.out / PRESETS)
        self.apply()  # no error


class DiffTests(unittest.TestCase):
    def test_roundtrip_of_remove_move_and_add(self):
        gen = preset()
        edited = copy.deepcopy(gen)
        edited["entities"] = [e for e in edited["entities"] if e["id"] != 2]  # removed fern
        pos(by_id(edited, 4))["x"] += 4.0  # moved pillar
        edited["entities"].append(ent("new", 41.0, 42.0, "x/new.model", id_=99))  # added
        spec = O.diff(gen, edited, "jungle1")
        self.assertEqual(sorted(o["op"] for o in spec["ops"]), ["entity", "move", "remove"])
        replay = copy.deepcopy(gen)
        self.assertEqual(O.apply_ops(replay, spec["ops"], Path(".")), [])
        self.assertEqual(replay["dependencies"]["models"][-1], {"path": "x/new.model"})
        key = lambda p: sorted(json.dumps(e["components"], sort_keys=True) for e in p["entities"])
        self.assertEqual(key(replay), key(edited))


if __name__ == "__main__":
    unittest.main()
