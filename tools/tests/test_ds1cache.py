"""The presets rule's cache per DS1 (#861): the key, the kit slice, the parts of pd2_cache, and the rule end to end."""
import errno
import io
import json
import shutil
import sys
import tempfile
import unittest
from collections import defaultdict
from contextlib import redirect_stdout
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_cache  # noqa: E402
import pd2_presets as P  # noqa: E402
from presets import apply as A, ds1cache, parallel, slices  # noqa: E402
from presets.passes import edges_walls as W  # noqa: E402
from presets.kit import Kit  # noqa: E402
from presets.layout import Layout  # noqa: E402
from presets_fixtures import B, ds1, entity  # noqa: E402
import test_presets_apply as applied  # noqa: E402

T1 = ("a.dt1", 0, 1, 0)
T2 = ("a.dt1", 0, 2, 0)
OTHER = ("b.dt1", 0, 1, 0)


def put(p: Path, text: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(text.encode())  # LF on any OS: the tables are read line by line


class DigestTests(unittest.TestCase):
    def test_equal_values_hash_equal_whatever_the_order_of_a_set(self):
        self.assertEqual(ds1cache.digest({1, 2, 3}, b"x"), ds1cache.digest({3, 1, 2}, b"x"))
        self.assertNotEqual(ds1cache.digest([1, 2]), ds1cache.digest([2, 1]))
        self.assertNotEqual(ds1cache.digest("a"), ds1cache.digest("b"))

    def test_a_layout_hashes_by_its_content(self):
        a = Layout(2, 1, {(0, 0): ("f", (T1,))})
        self.assertEqual(ds1cache.layout_digest(a), ds1cache.layout_digest(Layout(2, 1, {(0, 0): ("f", (T1,))})))
        self.assertNotEqual(ds1cache.layout_digest(a), ds1cache.layout_digest(Layout(2, 1, {(0, 0): ("f", (T2,))})))
        self.assertNotEqual(ds1cache.layout_digest(a), ds1cache.layout_digest(Layout(3, 1, {(0, 0): ("f", (T1,))})))

    def test_a_memo_is_per_object_and_keeps_the_object(self):
        held, calls = {}, []
        o = {"k": 1}
        make = lambda x: calls.append(1) or ds1cache.digest(x)  # noqa: E731
        self.assertEqual(ds1cache.memo(held, o, make), ds1cache.memo(held, o, make))
        self.assertEqual(len(calls), 1)
        self.assertEqual(ds1cache.memo(held, {"k": 2}, make), ds1cache.digest({"k": 2}))


class UniqueTests(unittest.TestCase):
    def test_a_repeated_row_builds_once_and_a_ds1_under_two_tile_sets_is_an_error(self):
        self.assertEqual(ds1cache.unique([("A", "x.ds1"), ("B", "y.ds1"), ("A", "x.ds1")]), [("A", "x.ds1"), ("B", "y.ds1")])
        with self.assertRaisesRegex(ValueError, "x.ds1 is built under two tile sets, A and B"):
            ds1cache.unique([("A", "x.ds1"), ("B", "x.ds1")])

    def test_two_spellings_of_one_output_file_are_one_task(self):
        self.assertEqual(ds1cache.unique([("A", "Dir/x.ds1"), ("A", "dir\\X.ds1")]), [("A", "Dir/x.ds1")])
        with self.assertRaisesRegex(ValueError, "two tile sets"):
            ds1cache.unique([("A", "Dir/x.ds1"), ("B", "dir/X.DS1")])


class SliceTests(unittest.TestCase):
    def kit(self):
        e = lambda n: [(entity(n, 1.0, 1.0), 0.0, 0.0)]  # noqa: E731
        return Kit(instances={("f", (T1,)): [(0, e("own"))],  # the layout's own key
                              ("f", (T2,)): [(2, e("like"))],  # single layer of a DT1 it draws: LIKE lends it
                              ("w", (T1, T2)): [(1, e("two"))],  # both tiles in DT1 it draws: kept
                              ("f", (OTHER,)): [(3, e("far"))],  # a DT1 it does not draw
                              ("w", (T1, OTHER)): [(3, e("mixed"))]},
                   sources=[{"s": [0]}, {"s": [1]}, {"s": [2]}, {"s": [3]}], outdoor={0, 3},
                   waypoints={("c", "WP"): [(2, e("wp"))], ("c", "Other"): [(3, e("wp2"))]},
                   flags={("f", (T1,)): b"x", ("f", (OTHER,)): b"y"})

    def test_a_slice_keeps_what_a_build_can_look_up_and_renumbers_the_sources_by_rank(self):
        lay = Layout(1, 1, {(0, 0): ("f", (T1,))})
        sl = slices.slice_kit(self.kit(), lay, [("WP", 8.0, 8.0)])
        self.assertEqual(sorted(sl.instances), [("f", (T1,)), ("f", (T2,)), ("w", (T1, T2))])
        self.assertEqual(sl.sources, [{"s": [0]}, {"s": [1]}, {"s": [2]}])
        self.assertEqual({k: [s for s, _ in v] for k, v in sl.instances.items()},
                         {("f", (T1,)): [0], ("f", (T2,)): [2], ("w", (T1, T2)): [1]})
        self.assertEqual(list(sl.waypoints), [("c", "WP")])
        self.assertEqual(sl.outdoor, {0}, "the outdoor flag follows its source's new number")
        self.assertEqual(sl.flags, {("f", (T1,)): b"x"})

    def test_a_layout_that_draws_other_tiles_sees_other_pieces_and_a_key_that_differs(self):
        lay = Layout(1, 1, {(0, 0): ("f", (OTHER,))})
        sl = slices.slice_kit(self.kit(), lay, [])
        self.assertEqual(sorted(sl.instances), [("f", (OTHER,))])
        self.assertEqual(sl.sources, [{"s": [3]}])
        held = {}
        a = ds1cache.kit_digest(slices.slice_kit(self.kit(), Layout(1, 1, {(0, 0): ("f", (T1,))}), []), held)
        b = ds1cache.kit_digest(sl, held)
        self.assertNotEqual(a, b)
        self.assertEqual(a, ds1cache.kit_digest(slices.slice_kit(self.kit(), Layout(1, 1, {(0, 0): ("f", (T1,))}), []), {}))

    def test_a_stock_piece_no_build_can_see_is_not_in_the_key(self):
        lay = Layout(1, 1, {(0, 0): ("f", (T1,))})
        base, edited = self.kit(), self.kit()
        edited.instances[("f", (OTHER,))][0][1][0][0]["components"][0]["position"]["x"] = 99.0  # the DT1 b.dt1 piece
        held = {}
        self.assertEqual(ds1cache.kit_digest(slices.slice_kit(base, lay, []), held),
                         ds1cache.kit_digest(slices.slice_kit(edited, lay, []), held))
        edited.instances[("f", (T2,))][0][1][0][0]["components"][0]["position"]["x"] = 99.0  # lent by LIKE
        self.assertNotEqual(ds1cache.kit_digest(slices.slice_kit(base, lay, []), {}),
                            ds1cache.kit_digest(slices.slice_kit(edited, lay, []), {}))


def part_rule(root: Path, parts: list[str], built: list[str]):
    """A rule of parts as presets-hd is: each part reads merge/<name>.txt and writes generated/data/<name>.json; the rule
    reads merge/rule.txt. A part's key holds what the rule knows of it without reading (here: its name)."""
    def run():
        sc = pd2_cache.scope()
        shared = (root / "merge" / "rule.txt").read_text()
        for name in parts:
            key = "part" + name
            if (extra := pd2_cache.part_get(sc, key)) is not None:
                built.append(f"replayed {name}:{extra['n']}")
                continue
            with pd2_cache.sub_trace() as sub:
                text = (root / "merge" / f"{name}.txt").read_text()
            out = root / "generated" / "data" / f"{name}.json"
            put(out, json.dumps([shared, text]))
            built.append(f"built {name}")
            pd2_cache.part_put(sc, key, [f"data/{name}.json"], {"n": len(text)}, sub.reads)
    return run


class PartTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.root, self.cache, self.d2r = t / "wt", t / "cache" / "rules", t / "d2r"
        self.d2r.mkdir()
        for n in "ab":
            put(self.root / "merge" / f"{n}.txt", n)
        put(self.root / "merge" / "rule.txt", "r")

    def tearDown(self):
        self.tmp.cleanup()

    def go(self, key="rule"):
        built = []
        with redirect_stdout(io.StringIO()):
            pd2_cache.rule(self.cache, key, {"p": self.root, "d": self.d2r}, part_rule(self.root, ["a", "b"], built))
        return built, {p.name: p.read_text() for p in sorted((self.root / "generated" / "data").glob("*.json"))}

    def test_a_rule_that_misses_replays_the_parts_whose_reads_hold(self):
        built, out = self.go()
        self.assertEqual(built, ["built a", "built b"])
        shutil.rmtree(self.root / "generated")  # as a merge starts: no generated/ yet
        self.assertEqual(self.go()[0], [], "nothing changed: the rule's own entry replays whole")
        shutil.rmtree(self.root / "generated")
        put(self.root / "merge" / "rule.txt", "r2")  # only the rule's own read changes: the rule misses
        built, out2 = self.go()
        self.assertEqual(built, ["replayed a:1", "replayed b:1"])
        self.assertEqual(out2, out, "a replayed part is the file it wrote, whatever the rest of the rule reads now")

    def test_a_part_whose_own_input_changed_is_built_again_and_only_it(self):
        self.go()
        shutil.rmtree(self.root / "generated")
        put(self.root / "merge" / "rule.txt", "r2")
        put(self.root / "merge" / "b.txt", "bb")
        built, out = self.go()
        self.assertEqual(built, ["replayed a:1", "built b"])
        self.assertEqual(out["b.json"], json.dumps(["r2", "bb"]))
        shutil.rmtree(self.root / "generated")
        put(self.root / "merge" / "rule.txt", "r3")
        self.assertEqual(self.go()[0], ["replayed a:1", "replayed b:2"], "the rebuilt part was kept under the same key")

    def test_a_replayed_part_hands_its_reads_to_the_rules_entry(self):
        self.go()
        shutil.rmtree(self.root / "generated")
        put(self.root / "merge" / "rule.txt", "r2")
        self.go()  # the rule's entry for this run is kept with a.txt's and b.txt's state although only parts read them
        shutil.rmtree(self.root / "generated")
        put(self.root / "merge" / "a.txt", "aaa")
        built, _ = self.go()
        self.assertEqual(built, ["built a", "replayed b:1"], "the rule did not replay whole: a.txt is in its reads")

    def test_nothing_is_kept_without_a_scope(self):
        self.assertIsNone(pd2_cache.scope())
        with pd2_cache.sub_trace() as sub:
            pass
        self.assertIsNone(sub)


    def test_a_part_keeps_ancestor_listings_and_drops_reads_of_other_outputs(self):
        sc = mock.Mock(dest=self.cache, roots={"p": self.root}, policy=mock.Mock(origin="o"))
        (self.root / "generated" / "data").mkdir(parents=True)
        put(self.root / "generated" / "data" / "a.json", "{}")
        reads = {"f:p:generated/data/a.json": "1", "f:p:generated/data/b.json": "2", "l:p:generated/data": "x",
                 "f:p:merge/a.txt": "3"}
        pd2_cache.part_put(sc, "k", ["data/a.json"], {}, reads, {"data/b.json"})
        kept = json.loads(next(self.cache.glob("k/o/manifest.json")).read_text())["reads"]
        self.assertEqual(sorted(kept), ["f:p:merge/a.txt", "l:p:generated/data"])

    def test_a_rename_onto_a_kept_folder_says_nothing_on_posix_either(self):
        sc = mock.Mock(dest=self.cache, roots={"p": self.root}, policy=mock.Mock(origin="o"))
        (self.root / "generated" / "data").mkdir(parents=True)
        put(self.root / "generated" / "data" / "a.json", "{}")
        real = pd2_cache.os.rename

        def rename(src, dst):
            if str(dst).endswith("o"):
                raise OSError(errno.ENOTEMPTY, "Directory not empty")
            return real(src, dst)
        with mock.patch.object(pd2_cache.os, "rename", rename), redirect_stdout(io.StringIO()) as log:
            pd2_cache.part_put(sc, "k", ["data/a.json"], {}, {})
        self.assertEqual(log.getvalue(), "")


class WallIndexMemoTests(unittest.TestCase):
    """The wall index is memoised per process; the prefab files it read are every asker's (#882)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.root, self.cache, self.d2r = t / "wt", t / "cache" / "rules", t / "d2r"
        put(self.d2r / "hd/prefab.json", "1")
        put(self.root / "merge" / "rule.txt", "r")
        W._INDEX.clear()

    def tearDown(self):
        W._INDEX.clear()
        self.tmp.cleanup()

    def run_rule(self, built):
        # a memoised prefab loader, as apply.py's: the file is opened once, every call notes it
        cache = {}

        def prefab(path):
            f = self.d2r / "hd/prefab.json"
            pd2_cache.note(f)
            return cache.setdefault(path, [f.read_text()] and [])
        lay = SimpleNamespace(keys={(0, 0): ("w", (("act2/a/wall.dt1", 1, 0, 0),))})
        stock = [(lay, {"dependencies": {}, "entities": [entity("wall_a", 1.0, 1.0)]})]
        ctx = W.Context(SimpleNamespace(keys={}), defaultdict(dict), lambda m: None, prefab, stock)

        def run():
            sc = pd2_cache.scope()
            (self.root / "merge" / "rule.txt").read_text()
            for name in "ab":  # two DS1: the first builds the index, the second hits the memo
                if pd2_cache.part_get(sc, "part" + name) is not None:
                    built.append(f"replayed {name}")
                    continue
                with pd2_cache.sub_trace() as sub:
                    W.index(stock, ctx)
                put(self.root / "generated" / "data" / f"{name}.json", "{}")
                built.append(f"built {name}")
                pd2_cache.part_put(sc, "part" + name, [f"data/{name}.json"], {}, sub.reads)

        def boxes(ents, box, pf):
            pf("p")
            return []
        with redirect_stdout(io.StringIO()), mock.patch("pd2_tiles.model_type", return_value="wall"), \
                mock.patch("pd2_tiles.boxes", boxes):
            pd2_cache.rule(self.cache, "rule", {"p": self.root, "d": self.d2r}, run)

    def test_a_changed_prefab_misses_every_ds1_that_used_it(self):
        built = []
        self.run_rule(built)
        self.assertEqual(built, ["built a", "built b"])
        for text, prefab, want in (("r2", "1", ["replayed a", "replayed b"]), ("r3", "2", ["built a", "built b"])):
            shutil.rmtree(self.root / "generated")
            W._INDEX.clear()
            put(self.root / "merge" / "rule.txt", text)  # the rule misses; the parts replay while their reads hold
            if prefab != "1":
                put(self.d2r / "hd/prefab.json", prefab)
            built = []
            self.run_rule(built)
            self.assertEqual(built, want, "b only hit the memo, yet its entry holds the prefab")


class ReleaseTests(unittest.TestCase):
    def test_the_parent_drops_the_wall_stock_before_the_workers_run(self):
        W.CONTEXT["x.ds1"], W._INDEX[(1,)] = object(), ((), {}, {})
        seen = {}
        workers = lambda pl, tasks, spare: seen.update(ctx=dict(W.CONTEXT), idx=dict(W._INDEX)) or ({}, 0)  # noqa: E731
        with mock.patch.object(parallel, "_workers", workers), mock.patch.object(parallel, "wanted", lambda: (2, True)), \
                mock.patch.object(parallel.pd2_cache, "holding", lambda: False):
            parallel._build_unique(None, [("A", "x.ds1"), ("A", "y.ds1")], [object()])
        self.assertEqual(seen, {"ctx": {}, "idx": {}})


class RuleEndToEndTests(unittest.TestCase):
    """presets-hd under the rule cache: the second DS1 shares the stock, the first is touched."""

    def setUp(self):
        applied.ApplyTests.setUp(self)  # the same tables, DT1, DS1 and stock; its out folder moves under generated/
        t = Path(self.tmp.name)
        self.root = t / "wt"
        (self.root / "generated").mkdir(parents=True)
        shutil.move(str(self.out), str(self.root / "generated" / "data"))
        self.out = self.root / "generated" / "data"
        self.cache = t / "cache" / "rules"
        put(self.out / "global/excel/levels.txt", "Name|LevelType\nTown|0\nMap|0\nPd2 Map|1\nMap2|0\n".replace("|", "\t"))
        with open(self.out / "global/excel/lvlprest.txt", "a", newline="") as f:
            f.write("Map2|3|1|Expansion/Map/N.ds1|||||\n".replace("|", "\t"))
        (self.out / "global/tiles/expansion/map/n.ds1").write_bytes(ds1(1, 2, [{(0, 0): B[1:]}], [{}]))
        self.rule.write_text("# tile set\tbiome\nAct 1 - Town\tdata/hd/env/biome/act1_town.json\n")
        put(self.root / "merge" / "marker.txt", "1")

    def go(self):
        def run():
            (self.root / "merge" / "marker.txt").read_text()  # something the rule reads beside its parts
            P.apply(self.rule, Path(), self.d2r, self.out)
        with redirect_stdout(io.StringIO()) as log:
            pd2_cache.rule(self.cache, "rulekey", {"p": self.root, "d": self.d2r}, run)
        pre = self.out / "hd/env/preset/expansion/map"
        return log.getvalue(), {p.name: p.read_bytes() for p in sorted(pre.glob("*.json"))}

    def tearDown(self):
        applied.ApplyTests.tearDown(self)

    def touch(self, text="1"):
        shutil.rmtree(self.root / "generated" / "data" / "hd")
        put(self.root / "merge" / "marker.txt", text)

    def test_a_cached_build_is_the_cold_build_and_a_change_to_one_ds1_builds_that_ds1(self):
        said, cold = self.go()
        self.assertIn("0 of 2 DS1 replayed", said)
        self.assertEqual(sorted(cold), ["m.json", "n.json"])
        self.touch("2")
        said, again = self.go()
        self.assertIn("2 of 2 DS1 replayed", said)
        self.assertEqual(again, cold, "byte for byte")
        self.touch("3")
        (self.out / "global/tiles/expansion/map/n.ds1").write_bytes(ds1(1, 2, [{(0, 1): B[1:]}], [{}]))
        said, changed = self.go()
        self.assertIn("1 of 2 DS1 replayed", said)
        self.assertEqual(changed["m.json"], cold["m.json"])
        self.assertNotEqual(changed["n.json"], cold["n.json"])

    def test_a_key_follows_what_the_ds1_reads_and_nothing_else(self):
        pl = A.plan(self.rule, Path(), self.d2r, self.out)
        ds1s = sum(pl.groups.values(), [])
        self.assertEqual(sorted(ds1s), ["expansion/map/m.ds1", "expansion/map/n.ds1"])
        key = lambda st, d, rule="k": ds1cache.ds1_key(st, "Act 1 - Town", d, rule)  # noqa: E731
        with redirect_stdout(io.StringIO()):
            st = A.setup(pl)
            first = {d: key(st, d) for d in ds1s}
            self.assertEqual(first, {d: key(A.setup(pl), d) for d in ds1s}, "the same inputs, a new State: the same key")
            self.assertEqual(len(set(first.values())), 2)
            self.assertTrue(all(len(k) == 32 for k in first.values()))
            self.assertNotEqual(first[ds1s[0]], key(st, ds1s[0], "k2"), "the rule's code is in the key")
            stock = self.d2r / "hd/env/preset/act1/s.json"
            data = json.loads(stock.read_text())
            data["entities"][1]["components"][0]["position"]["x"] = 15.0  # the torch the DS1 draws
            stock.write_text(json.dumps(data))
            changed = A.setup(pl)
            self.assertTrue(all(key(changed, d) != first[d] for d in ds1s), "a stock piece they draw")


if __name__ == "__main__":
    unittest.main()
