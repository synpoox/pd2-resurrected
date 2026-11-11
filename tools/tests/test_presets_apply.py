"""pd2_presets: the pd2_merge rule."""
import contextlib
import io
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets_fixtures import A, B, FLOOR, TERRAIN, at, cell, dt1, ds1, entity, stock  # noqa: E402,F401


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.rule, self.d2r, self.out = t / "presets-hd.tsv", t / "d2r", t / "out"

        def write(path: Path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.replace("|", "\t").encode())
        write(self.d2r / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1\nTown|0|1|Act1/S.ds1\n")
        for root in (self.d2r, self.out):
            write(root / "global/excel/objpreset.txt", "Index|Act|ObjectClass\n0|1|WaypointOutsideAct1\n")
        write(self.out / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1|File2|File3|File4|File5|File6\n"
              "Town|0|1|Act1/S.ds1|||||\nMap|1|1|Expansion/Map/M.ds1|0||||\nPd2 Map|2|1|Expansion/Map/P.ds1|||||\n")
        write(self.out / "global/excel/levels.txt", "Name|LevelType\nTown|0\nMap|0\nPd2 Map|1\n")
        write(self.out / "global/excel/lvltypes.txt", "Name|File 1\nAct 1 - Town|Act1/A/Wall.dt1\n"
              "Pd2 Set|pd2assets/x.dt1\n")
        write(self.d2r / "global/tiles/act1/a/wall.dt1", dt1([A[1:], B[1:]]))
        write(self.d2r / "global/tiles/act1/s.ds1", ds1(2, 1, [{(0, 0): A[1:], (1, 0): B[1:]}], [{}],
                                                       ["\\d2\\data\\global\\tiles\\act1\\a\\wall.tg1"]))
        write(self.d2r / "hd/env/preset/act1/s.json", json.dumps(stock()).encode())
        write(self.d2r / f"hd/env/preset/{P.TERRAIN}.json", json.dumps({"terrain": TERRAIN}).encode())
        write(self.out / "global/tiles/expansion/map/m.ds1", ds1(1, 2, [{(0, 1): B[1:]}], [{}]))
        write(self.out / "global/tiles/expansion/map/p.ds1", ds1(1, 1, [{(0, 0): B[1:]}], [{}]))

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_preset_on_d2r_tiles_gets_its_kit_and_one_on_pd2_tiles_none(self):
        self.rule.write_text("# tile set\tbiome\nAct 1 - Town\tdata/hd/env/biome/act1_town.json\n")
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        preset = json.loads((self.out / "hd/env/preset/expansion/map/m.json").read_text())
        self.assertEqual([(e["name"], e["components"][0]["position"]["z"]) for e in preset["entities"]],
                         [("torch", 12.0)])
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/act1_town.json")
        self.assertFalse((self.out / "hd/env/preset/expansion/map/p.json").exists())
        self.assertIn("1 presets on PD2 tiles wait for stand-ins", log.getvalue())

    def test_a_preset_on_pd2_tiles_with_stand_ins_draws_the_stand_ins_stock_pieces(self):
        # the PD2 DT1 copies tile B of act1/a/wall.dt1 and adds one D2R lacks; p.ds1 draws both
        (self.out / "global/tiles/pd2assets").mkdir(parents=True)
        (self.out / "global/tiles/pd2assets/x.dt1").write_bytes(dt1([B[1:], (1, 7, 7)], {(1, 7, 7)}))
        (self.out / "global/tiles/expansion/map/p.ds1").write_bytes(ds1(2, 1, [{(0, 0): B[1:], (1, 0): (1, 7, 7)}], [{}]))
        self.rule.write_text("Act 1 - Town\tdata/hd/env/biome/act1_town.json\nPd2 Set\tdata/hd/env/biome/b.json\n"
                             "pd2assets/X.dt1\tact1/a/wall.dt1\n")
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        preset = json.loads((self.out / "hd/env/preset/expansion/map/p.json").read_text())
        self.assertEqual([e["name"] for e in preset["entities"]], ["torch"])  # tile B's piece; the new tile stays empty
        self.assertEqual(preset["biomeFilename"], "data/hd/env/biome/b.json")
        self.assertIn("0 presets on PD2 tiles wait", log.getvalue())
        lay = P.layout((self.out / "global/tiles/expansion/map/p.ds1").read_bytes(),
                       P.stand_in(["pd2assets/x.dt1"], P.Tiles(self.d2r / "global/tiles"),
                                  P.Tiles(self.out / "global/tiles"), {"pd2assets/x.dt1": "act1/a/wall.dt1"}))
        self.assertEqual(lay.keys[(1, 0)], ("w", (("pd2assets/x.dt1", 1, 7, 7),)))
        self.assertEqual(lay.flags[(1, 0)], b"\1" * 25)  # PD2's collision stays

    def test_a_stand_in_line_no_preset_loads_fails(self):
        self.rule.write_text("Act 1 - Town\tb.json\npd2assets/gone.dt1\t-\n")
        with self.assertRaisesRegex(ValueError, "stand-in lines for DT1 no PD2-only preset loads: pd2assets/gone.dt1"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_water_line_no_preset_draws_from_or_naming_an_unknown_plane_fails(self):
        self.rule.write_text(f"Act 1 - Town\tb.json\nwater:act3/river/gone.dt1\t{next(iter(P.WATER_SPAN))}\n")
        with self.assertRaisesRegex(ValueError, "water lines for DT1 no PD2-only preset draws from: act3/river/gone.dt1"):
            P.apply(self.rule, Path(), self.d2r, self.out)
        self.rule.write_text("Act 1 - Town\tb.json\nwater:act1/a/wall.dt1\tdata/hd/env/model/pond.model\n")
        with self.assertRaisesRegex(ValueError, "no water plane WATER_SPAN knows"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_tile_set_with_no_line_fails(self):
        self.rule.write_text("# tile set\tbiome\n")
        with self.assertRaisesRegex(ValueError, "with no line: Act 1 - Town"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_a_line_no_preset_needs_fails(self):
        self.rule.write_text("Act 1 - Town\tb.json\nAct 9 - Gone\tb.json\n")
        with self.assertRaisesRegex(ValueError, "on D2R tiles or stand-ins loads: act 9 - gone"):
            P.apply(self.rule, Path(), self.d2r, self.out)

    def test_without_d2rs_presets_in_the_cache_it_makes_none(self):
        self.rule.write_text("Act 1 - Town\tb.json\n")
        (self.d2r / f"hd/env/preset/{P.TERRAIN}.json").unlink()
        (self.d2r / "hd/env/preset/act1/s.json").unlink()
        for d in ("hd/env/preset/act2/palace", "hd/env/preset/act2", "hd/env/preset/act1", "hd/env/preset"):
            (self.d2r / d).rmdir()
        with redirect_stdout(io.StringIO()) as log:
            P.apply(self.rule, Path(), self.d2r, self.out)
        self.assertIn("skipped", log.getvalue())
        self.assertFalse((self.out / "hd").exists())

    def test_the_output_does_not_depend_on_the_worker_count_and_a_repeated_ds1_is_built_once(self):
        lv = self.out / "global/excel/lvlprest.txt"
        lv.write_text(lv.read_text() + "Map B\t3\t1\tExpansion/Map/N.ds1\t\t\t\t\t\n")
        lv2 = self.out / "global/excel/levels.txt"
        lv2.write_text(lv2.read_text() + "Map B\t0\n")
        (self.out / "global/tiles/expansion/map/n.ds1").write_bytes(
            ds1(2, 1, [{(0, 0): A[1:], (1, 0): B[1:]}], [{}]))
        self.rule.write_text("# tile set\tbiome\nAct 1 - Town\tdata/hd/env/biome/act1_town.json\n")
        pre, outputs = self.out / "hd/env/preset", {}
        for workers in ("1", "2"):
            shutil.rmtree(pre, ignore_errors=True)
            with mock.patch.dict(os.environ, {"PD2R_PRESET_WORKERS": workers}), redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                P.apply(self.rule, Path(), self.d2r, self.out)
            outputs[workers] = {f.relative_to(pre).as_posix(): f.read_bytes() for f in pre.rglob("*.json")}
        self.assertEqual(sorted(outputs["1"]), ["expansion/map/m.json", "expansion/map/n.json"])
        self.assertEqual(outputs["1"], outputs["2"])

    def test_a_ds1_the_plan_lists_twice_is_built_once_and_reported_twice(self):
        from presets import parallel
        seen = []

        def fake(pl, tasks):
            seen.append(list(tasks))
            return {t: t[1] for t in tasks}, 7
        with mock.patch.object(parallel, "_serial", fake):
            built, sources = parallel.build_all(None, [("1", "a.ds1"), ("1", "b.ds1"), ("1", "a.ds1")])
        self.assertEqual(seen, [[("1", "a.ds1"), ("1", "b.ds1")]])
        self.assertEqual((built, sources), (["a.ds1", "b.ds1", "a.ds1"], 7))

    def test_workers_default_to_three_and_the_env_asks_for_more_or_fewer(self):
        from presets import parallel
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("PD2R_PRESET_WORKERS", None)
            self.assertEqual(parallel.wanted(), (3, False))
        with mock.patch.dict(os.environ, {"PD2R_PRESET_WORKERS": "5"}):
            self.assertEqual(parallel.wanted(), (5, True))


class RuleFilesTests(unittest.TestCase):
    """The rule's lines are in merge/presets-hd.tsv (the anchor) and presets-hd.d/*.tsv (#789)."""
    def test_the_anchor_then_the_folders_files_by_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            rule, d = t / "presets-hd.tsv", t / "presets-hd.d"
            d.mkdir()
            rule.write_text("# anchor\nSet A\ta.json\n")
            (d / "z.tsv").write_text("Set Z\tz.json\n")
            (d / "b.tsv").write_text("# note\nSet B\tb.json\npd2assets/x.dt1\t-\n")
            (d / "notes.md").write_text("Set Q\tq.json\n")
            self.assertEqual([f.name for f in P.rule_files(rule)], ["presets-hd.tsv", "b.tsv", "z.tsv"])
            sets, standins, water = P.read_rule(rule)
            self.assertEqual(sets, {"set a": "a.json", "set b": "b.json", "set z": "z.json"})
            self.assertEqual((standins, water), ({"pd2assets/x.dt1": None}, {}))

    def test_a_key_twice_is_an_error_naming_both_files(self):
        for first, second, named in (("Set A\ta.json\n", "set a\tz.json\n", r"a\.tsv and in b\.tsv"),
                                     ("pd2assets/x.dt1\t-\n", "PD2Assets/X.dt1\tact1/a.dt1\n", r"a\.tsv and in b\.tsv"),
                                     ("Set A\ta.json\nSet A\tb.json\n", "", r"a\.tsv and in a\.tsv")):
            with self.subTest(first=first), tempfile.TemporaryDirectory() as tmp:
                t = Path(tmp)
                rule, d = t / "presets-hd.tsv", t / "presets-hd.d"
                d.mkdir()
                rule.write_text("")
                (d / "a.tsv").write_text(first)
                (d / "b.tsv").write_text(second)
                with self.assertRaisesRegex(ValueError, named):
                    P.read_rule(rule)

    def test_walls_lines_are_read_apart_from_stand_ins_and_keyed_once(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            rule, d = t / "presets-hd.tsv", t / "presets-hd.d"
            d.mkdir()
            rule.write_text("")
            (d / "a.tsv").write_text("walls:PD2Assets/X.dt1\tAct2/A.dt1\npd2assets/x.dt1\tact1/b.dt1\n")
            sets, standins, water, walls = P.read_rule_all(rule)
            self.assertEqual((sets, standins, water, walls),
                             ({}, {"pd2assets/x.dt1": "act1/b.dt1"}, {}, {"pd2assets/x.dt1": "act2/a.dt1"}))
            self.assertEqual(P.read_rule(rule)[1], {"pd2assets/x.dt1": "act1/b.dt1"})
            (d / "b.tsv").write_text("walls:pd2assets/x.dt1\tact2/c.dt1\n")
            with self.assertRaisesRegex(ValueError, r"a\.tsv and in b\.tsv"):
                P.read_rule_all(rule)
            (d / "b.tsv").write_text("walls:pd2assets/y.dt1\tact2/c.txt\n")
            with self.assertRaisesRegex(ValueError, "must be .dt1"):
                P.read_rule_all(rule)

    def test_without_the_folder_the_anchor_alone_is_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            rule = Path(tmp) / "presets-hd.tsv"
            rule.write_text("Set A\ta.json\n")
            self.assertEqual(P.rule_files(rule), [rule])
            self.assertEqual(P.read_rule(rule)[0], {"set a": "a.json"})

    def test_the_repos_anchor_holds_no_line_and_its_folder_all_of_them(self):
        rule = Path(__file__).resolve().parent.parent.parent / "merge" / "presets-hd.tsv"
        self.assertEqual([ln for ln in rule.read_text("utf-8").splitlines() if ln and not ln.startswith("#")], [])
        sets, standins, water = P.read_rule(rule)
        self.assertTrue(sets and standins and water)

if __name__ == "__main__":
    unittest.main()
