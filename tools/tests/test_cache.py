"""pd2_cache (#440, #558): the extract lock, the cache key, the shared extract, and the per-rule merge cache."""
import contextlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import threading
import time
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_cache  # noqa: E402
import pd2_extract  # noqa: E402
import pd2_ids  # noqa: E402
import pd2_merge  # noqa: E402


def cache_env(cache: Path):
    """os.environ with the cache root at cache, and without GITHUB_ACTIONS, which turns the cache off (the unit job)."""
    env = {k: v for k, v in os.environ.items() if k != "GITHUB_ACTIONS"}
    return mock.patch.dict(os.environ, {**env, "PD2R_CACHE": str(cache)}, clear=True)


def write(p: Path, text: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)


class LockTests(unittest.TestCase):
    def test_a_second_holder_waits_for_the_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            path, got = Path(tmp) / "x.lock", threading.Event()

            def second():
                with redirect_stdout(io.StringIO()), pd2_cache.lock(path, poll=0.05):
                    got.set()

            with pd2_cache.lock(path):
                t = threading.Thread(target=second)
                t.start()
                self.assertFalse(got.wait(0.5))  # held: the second run waits
            self.assertTrue(got.wait(5))  # released: it goes on
            t.join()
            with pd2_cache.lock(path):  # free again once both are done
                pass


class DeadHolderTests(unittest.TestCase):
    def test_a_killed_holder_frees_the_lock_and_is_named_while_it_runs(self):
        import subprocess
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "x.lock"
            code = ("import sys, time, pathlib; sys.path.insert(0, sys.argv[1]); import pd2_cache\n"
                    "with pd2_cache.lock(pathlib.Path(sys.argv[2])):\n print('held', flush=True); time.sleep(60)")
            p = subprocess.Popen([sys.executable, "-c", code, str(Path(pd2_cache.__file__).parent), str(path)],
                                 stdout=subprocess.PIPE, text=True)
            try:
                self.assertEqual(p.stdout.readline().strip(), "held")
                self.assertRegex(pd2_cache.holder(path), rf"^pid {p.pid} \(running, holding since \d\d:\d\d:\d\d\)$")
            finally:
                p.kill()
                p.wait()
            self.assertRegex(pd2_cache.holder(path), rf"^pid {p.pid} \(gone, ")
            start = time.time()
            with pd2_cache.lock(path, poll=0.05):  # the kernel dropped the dead holder's lock: no wait
                pass
            self.assertLess(time.time() - start, 5)
            self.assertRegex(pd2_cache.holder(path), rf"^pid {os.getpid()} \(running", "the last holder is recorded")


class KeyTests(unittest.TestCase):
    def test_key_follows_content_paths_and_extra_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write(tmp / "merge" / "a.tsv", "1")
            write(tmp / "tools" / "t.py", "x")
            write(tmp / "stamp", "s")
            paths = [tmp / "merge", tmp / "tools", tmp / "stamp", tmp / "missing"]
            k = pd2_cache.key(paths, "d2r")
            self.assertEqual(pd2_cache.key(paths, "d2r"), k)
            write(tmp / "tools" / "__pycache__" / "t.pyc", "junk")
            self.assertEqual(pd2_cache.key(paths, "d2r"), k, "__pycache__ is not an input")
            self.assertNotEqual(pd2_cache.key(paths, "other d2r"), k)
            write(tmp / "merge" / "a.tsv", "2")
            self.assertNotEqual(pd2_cache.key(paths, "d2r"), k)
            write(tmp / "merge" / "a.tsv", "1")
            (tmp / "merge" / "a.tsv").rename(tmp / "merge" / "b.tsv")
            self.assertNotEqual(pd2_cache.key(paths, "d2r"), k, "a renamed file changes the key")
            (tmp / "merge" / "b.tsv").rename(tmp / "merge" / "a.tsv")
            write(tmp / "missing", "")
            self.assertNotEqual(pd2_cache.key(paths, "d2r"), k, "a new file changes the key")


class SharedExtractTests(unittest.TestCase):
    def test_pd2_data_is_a_junction_to_the_extract_for_its_stamp(self):
        try:
            ex = pd2_extract.classic("extract")
        except ValueError as e:  # CI has no d2rAI-toolkit (private)
            self.skipTest(str(e))
        class OneFile:
            def __init__(self, path): pass
            def names(self): return ["data\\x.txt"]
            def read(self, name): return b"x"

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "cache").mkdir()
            out = tmp / "wt" / "pd2-data"
            out.parent.mkdir()
            (tmp / "a.mpq").write_bytes(b"A")  # the archive the baseline pins; OneFile stands in for its contents
            pinned = {"archives": {"a.mpq": ex.sha256(tmp / "a.mpq")}, "extract": {"a.mpq": ["*"]}}
            for i, base in enumerate([pinned, {**pinned, "v": 2}]):
                (tmp / "b.json").write_text(json.dumps(base))
                args = ["--install", str(tmp), "--baseline", str(tmp / "b.json")]
                with cache_env(tmp / "cache"), mock.patch.object(pd2_extract, "ROOT", tmp / "wt"), \
                        mock.patch.object(ex.mpq, "Archive", OneFile), redirect_stdout(io.StringIO()):
                    self.assertEqual(pd2_extract.main(args), 0)
                self.assertTrue(pd2_cache.is_link(out))
                self.assertEqual((out / "data" / "x.txt").read_bytes(), b"x")
                self.assertEqual(len(list((tmp / "cache" / "extract").iterdir())), i + 1, "one extract per stamp")
            self.assertTrue((tmp / "cache" / "pd2_extract.lock").is_file())
            os.rmdir(out) if os.name == "nt" else out.unlink()


RUNS: list[str] = []


def rule_a(rule, pd2, d2r, out):  # its rule upper-cased: "x" and "X" write the same a.txt
    RUNS.append("a")
    write(out / "a.txt", rule.read_text().upper())


def rule_b(rule, pd2, d2r, out):  # reads a.txt; its rule names a file to add
    RUNS.append("b")
    write(out / "b.txt", (out / "a.txt").read_text() + rule.read_text())
    if rule.read_text() == "zz":
        write(out / "zz.txt", "")
    (out / "old.txt").unlink(missing_ok=True)


def rule_c(rule, pd2, d2r, out):  # looks only at whether zz.txt exists, and lists out/
    RUNS.append("c")
    print("c ran")
    write(out / "sub" / "c.txt", rule.read_text() + str((out / "zz.txt").is_file()) + str(len(os.listdir(out))))


def rule_d(rule, pd2, d2r, out):  # like presets-hd: D2R's tiles, if fetched
    RUNS.append("d")
    write(out / "d.txt", str(any((d2r / "global" / "tiles").rglob("*.dt1"))))


def rule_e(rule, pd2, d2r, out):  # a D2R table copied as is (copy2 on Windows raises no audit event)
    RUNS.append("e")
    shutil.copy2(d2r / "global" / "excel" / "e.txt", out / "e.txt")


def rule_f(rule, pd2, d2r, out):  # lists folders after writing into them, as strings.json does
    RUNS.append("f")
    write(out / "new" / "f.txt", "")
    write(out / "f.txt", str(len(os.listdir(out / "new"))) + str(sorted(os.listdir(out))))


class RuleCacheTests(unittest.TestCase):
    def test_a_rule_reruns_exactly_when_its_code_or_what_it_read_changed(self):
        try:
            ex = pd2_extract.classic("extract")
        except ValueError as e:  # CI has no d2rAI-toolkit (private)
            self.skipTest(str(e))
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            root, d2r = tmp / "wt", tmp / "d2r"
            write(root / "pd2-baseline.json", "B")
            (root / "pd2-data").mkdir()
            (root / "pd2-data" / "baseline.json").write_bytes(ex.stamp(b"B"))
            for n, t in (("a", "x"), ("b", "b"), ("c", "c"), ("d", "d"), ("e", "e"), ("f", "f")):
                write(root / "merge" / f"{n}.tsv", t)
            write(d2r / "global" / "excel" / "e.txt", "1")
            (tmp / "cache").mkdir()
            gen = root / "generated" / "data"

            def run(**env):
                RUNS.clear()
                with mock.patch.dict(os.environ, env), redirect_stdout(io.StringIO()) as so:
                    self.assertEqual(pd2_merge.main(["--root", str(root), "--d2r", str(d2r)]), 0)
                self.assertIn("c ran", so.getvalue(), "a replay prints what the rule printed")
                return sorted(RUNS), {p.relative_to(gen).as_posix(): p.read_text() for p in gen.rglob("*.txt")}

            rules = {"a.tsv": rule_a, "b.tsv": rule_b, "c.tsv": rule_c, "d.tsv": rule_d, "e.tsv": rule_e, "f.tsv": rule_f}
            with cache_env(tmp / "cache"), mock.patch.dict(pd2_merge.RULES, rules, clear=True), \
                    mock.patch.object(pd2_merge, "REQUIRED_TABLES", ()), mock.patch.object(pd2_merge, "MIN_TABLES", 0):
                ran, fresh = run()
                self.assertEqual(ran, ["a", "b", "c", "d", "e", "f"])
                self.assertEqual(fresh, {"a.txt": "X", "b.txt": "Xb", "sub/c.txt": "cFalse2", "d.txt": "False",
                                         "e.txt": "1",
                                         "new/f.txt": "", "f.txt": "1['a.txt', 'b.txt', 'd.txt', 'e.txt', 'new', 'sub']"})
                self.assertEqual(run(), ([], fresh), "nothing changed: every rule replays, the same files")
                write(root / "merge" / "c.tsv", "C")
                self.assertEqual(run()[0], ["c"], "only the changed rule")
                write(root / "merge" / "a.tsv", "X")
                self.assertEqual(run()[0], ["a"], "a reran, but wrote the a.txt b read before: b replays")
                write(root / "merge" / "a.tsv", "y")
                ran, out = run()
                self.assertEqual((ran, out["b.txt"]), (["a", "b"], "Yb"), "b read the a.txt a changed")
                write(root / "merge" / "b.tsv", "zz")
                ran, out = run()
                self.assertEqual((ran, out["sub/c.txt"]), (["b", "c", "f"], "CTrue3"), "c and f saw the zz.txt b added")
                write(root / "tools" / "tests" / "test_x.py", "")
                write(root / "docs" / "x.md", "")
                self.assertEqual(run()[0], [], "files no rule reads leave every rule cached")
                write(d2r / "global" / "tiles" / "act1" / "x.dt1", "")
                self.assertEqual(run(), (["d"], {**out, "d.txt": "True"}), "D2R's fetched tiles rerun the rule that looks")
                write(d2r / "global" / "excel" / "e.txt", "22")
                self.assertEqual(run()[0], ["e"], "a changed D2R table reruns the rule that copied it")
                with mock.patch.object(pd2_cache, "sources", lambda fn: [root / "tools" / "tests" / "test_x.py"]):
                    write(root / "tools" / "tests" / "test_x.py", "changed")
                    self.assertEqual(run()[0], ["a", "b", "c", "d", "e", "f"], "changed code reruns its rules")
                self.assertEqual(run(GITHUB_ACTIONS="true")[0], ["a", "b", "c", "d", "e", "f"], "CI always merges fresh")
            self.assertFalse(list((tmp / "cache").rglob("*.tmp*")), "no temp folder left")

    def test_a_removed_file_is_removed_again_on_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            cache, root = tmp / "cache", tmp / "wt"
            write(root / "merge" / "b.tsv", "b")
            out = root / "generated" / "data"
            RUNS.clear()
            for _ in range(2):
                write(out / "a.txt", "A")
                write(out / "old.txt", "stale")
                with redirect_stdout(io.StringIO()):
                    pd2_cache.rule(cache, "k", {"p": root}, lambda: rule_b(root / "merge" / "b.tsv", None, None, out))
                self.assertFalse((out / "old.txt").exists())
                self.assertEqual((out / "b.txt").read_text(), "Ab")
            self.assertEqual(RUNS, ["b"], "the second is a replay")

    def test_a_run_that_writes_outside_generated_is_not_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            cache, root = tmp / "cache", tmp / "wt"
            out = root / "generated" / "data"
            out.mkdir(parents=True)
            runs = []

            def art():  # as pd2_art does for a frame with no art yet: a PNG into merge/art
                runs.append(1)
                write(root / "merge" / "art" / f"x{len(runs)}.png", "png")
                write(out / "a.txt", "A")

            for _ in range(2):
                with redirect_stdout(io.StringIO()) as so:
                    self.assertFalse(pd2_cache.rule(cache, "k", {"p": root}, art))
                self.assertIn("not cached", so.getvalue())
            self.assertEqual(len(runs), 2, "never replayed")
            self.assertFalse(list(cache.rglob("manifest.json")))

    def test_sources_follow_imports_beside_the_module(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write(tmp / "m_one.py", "import os\nfrom m_two import f\ndef g(): pass\ndef main():\n    import m_three\n")
            write(tmp / "m_two.py", "import m_one\ndef f(): pass\n")
            write(tmp / "m_three.py", "")
            sys.path.insert(0, str(tmp))
            try:
                import m_one
                self.assertEqual(pd2_cache.sources(m_one.g), [tmp / "m_one.py", tmp / "m_two.py"])
            finally:
                sys.path.remove(str(tmp))
                for m in ("m_one", "m_two"):
                    sys.modules.pop(m, None)

    def test_prune_drops_least_recently_used_until_under_the_cap(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp)
            for i, n in enumerate(["rules/k/old", "merge/legacy", "rules/k/new", "rules/j/fresh"]):
                write(cache / n / "files" / "f", "x" * 100)
                if n.startswith("rules"):
                    write(cache / n / "manifest.json", json.dumps({"bytes": 100}))
                if n != "rules/j/fresh":
                    t = time.time() - 3600 * (4 - i)
                    os.utime(cache / n, (t, t))
            self.assertEqual([p.relative_to(cache).as_posix() for p in pd2_cache.prune(cache, 150)],
                             ["rules/k/old", "rules/k/new"], "merge/ is left alone by default")
            self.assertTrue((cache / "merge/legacy").is_dir())
            for n in ("rules/k/old", "rules/k/new"):
                write(cache / n / "manifest.json", json.dumps({"bytes": 100}))
                os.utime(cache / n, (time.time() - 7200, time.time() - 7200))
            gone = pd2_cache.prune(cache, 150, legacy=True)
            self.assertEqual(sorted(p.relative_to(cache).as_posix() for p in gone),
                             ["merge/legacy", "rules/k/new", "rules/k/old"], "PD2R_PRUNE_LEGACY: merge/ too")
            self.assertTrue((cache / "rules/j/fresh").is_dir(), "used in the last 15 minutes: kept over the cap")


TABLES = [f"t{i}.txt" for i in range(6)] + ["monstats.txt"]
LIVE: list[int] = []
PEAK: list[int] = []
PARTIAL: list[bool] = []


def rule_tables(rule, pd2, d2r, out):  # writes the table set slowly; the first run writes a partial one if asked
    LIVE.append(1)
    PEAK.append(len(LIVE))
    try:
        for i, t in enumerate(TABLES):
            if PARTIAL and i == 2 and PARTIAL.pop():
                return
            write(out / "global" / "excel" / t, rule.read_text())
            time.sleep(0.02)
    finally:
        LIVE.pop()


CHILD = """
import sys, time, pathlib
from unittest import mock
sys.path.insert(0, %(tools)r)
import pd2_merge
meet, name, TABLES = pathlib.Path(%(meet)r), sys.argv[3], %(tables)r

def rule(rule, pd2, d2r, out):
    meet.mkdir(exist_ok=True)
    (meet / ("run-" + name)).write_text("x")
    end = time.time() + 30
    while len(list(meet.glob("run-*"))) < 2 and time.time() < end:
        time.sleep(0.01)
    if len(list(meet.glob("run-*"))) == 2:
        (meet / ("met-" + name)).write_text("x")
    (out / "global" / "excel").mkdir(parents=True, exist_ok=True)
    for t in TABLES:
        (out / "global" / "excel" / t).write_text(rule.read_text())

with mock.patch.dict(pd2_merge.RULES, {"tables.tsv": rule}, clear=True), \\
        mock.patch.object(pd2_merge, "REQUIRED_TABLES", ("monstats.txt",)), \\
        mock.patch.object(pd2_merge, "MIN_TABLES", len(TABLES)):
    sys.exit(pd2_merge.main(["--root", sys.argv[1], "--d2r", sys.argv[2]]))
"""


class ConcurrentMergeTests(unittest.TestCase):
    def setUp(self):
        try:
            self.ex = pd2_extract.classic("extract")
        except ValueError as e:  # CI has no d2rAI-toolkit (private)
            self.skipTest(str(e))
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.tmp = Path(self.tmp.name)
        (self.tmp / "cache").mkdir()
        write(self.tmp / "d2r" / "global" / "excel" / "x.txt", "1")
        for name in ("a", "b"):
            root = self.tmp / name
            write(root / "pd2-baseline.json", "B")
            (root / "pd2-data").mkdir()
            (root / "pd2-data" / "baseline.json").write_bytes(self.ex.stamp(b"B"))
            write(root / "merge" / "tables.tsv", name)  # differs: neither replays the other's rule
        LIVE.clear(), PEAK.clear(), PARTIAL.clear()

    def merge(self, name):
        with redirect_stdout(io.StringIO()):
            return pd2_merge.main(["--root", str(self.tmp / name), "--d2r", str(self.tmp / "d2r")])

    def patched(self):
        stack = [cache_env(self.tmp / "cache"), mock.patch.dict(pd2_merge.RULES, {"tables.tsv": rule_tables}, clear=True),
                 mock.patch.object(pd2_merge, "REQUIRED_TABLES", ("monstats.txt",)),
                 mock.patch.object(pd2_merge, "MIN_TABLES", len(TABLES))]
        return stack

    def test_two_merges_at_once_both_write_a_whole_tree_without_waiting_on_each_other(self):
        # two processes (the rule cache traces per process); each rule waits until the other one is running too
        child = CHILD % {"tools": str(Path(pd2_cache.__file__).parent), "meet": str(self.tmp / "meet"), "tables": TABLES}
        for n in ("a", "b"):  # a file: a rule's module needs a __file__. Each differs, so the rules' keys do (a shared key
            write(self.tmp / "child" / f"child_merge_{n}.py", child + f"# {n}\n")  # is single-flight, #821)
        env = {**os.environ, "PD2R_CACHE": str(self.tmp / "cache")}
        env.pop("GITHUB_ACTIONS", None)
        procs = [subprocess.Popen([sys.executable, str(self.tmp / "child" / f"child_merge_{n}.py"), str(self.tmp / n), str(self.tmp / "d2r"), n], env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) for n in ("a", "b")]
        outs = [p.communicate(timeout=120)[0] for p in procs]
        self.assertEqual([p.returncode for p in procs], [0, 0], outs)
        self.assertEqual(sorted(p.name for p in (self.tmp / "meet").glob("met-*")), ["met-a", "met-b"],
                         "each merge saw the other running: no machine-wide lock")
        for n in ("a", "b"):
            tables = {p.name for p in (self.tmp / n / "generated" / "data" / "global" / "excel").glob("*.txt")}
            self.assertEqual(tables, set(TABLES))
        self.assertFalse((self.tmp / "cache" / "pd2_merge.lock").exists())
        self.assertFalse(list((self.tmp / "cache").rglob("*.tmp*")), "no temp folder left")

    def test_a_pure_cache_hit_does_not_wait_for_a_slot_and_a_miss_does(self):
        with contextlib.ExitStack() as st:
            for c in self.patched():
                st.enter_context(c)
            st.enter_context(mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": "1"}))
            self.assertEqual(self.merge("a"), 0)  # keeps the rule
            with mock.patch.object(pd2_cache.Slots, "acquire", side_effect=RuntimeError("asked for a slot")):
                self.assertEqual(self.merge("a"), 0, "every rule replays: no slot needed")
                with self.assertRaises(RuntimeError):
                    self.merge("b")  # a different rule input misses and needs one

    def test_a_ci_merge_takes_a_slot_though_it_has_no_rule_cache(self):
        with contextlib.ExitStack() as st:
            for c in self.patched():
                st.enter_context(c)
            st.enter_context(mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}))
            self.assertIsNone(pd2_cache.root(), "CI merges fresh, with no rule cache")
            with mock.patch.object(pd2_cache.Slots, "acquire", side_effect=RuntimeError("asked for a slot")):
                with self.assertRaises(RuntimeError):
                    self.merge("a")
            self.assertEqual(self.merge("a"), 0, "a free slot lets it run")
            self.assertFalse(list((self.tmp / "cache" / "rules").glob("*")), "and nothing is cached")

    def test_a_partial_tree_is_rebuilt_not_handed_back(self):
        with contextlib.ExitStack() as st:
            for c in self.patched():
                st.enter_context(c)
            PARTIAL.append(True)
            self.assertEqual(self.merge("a"), 0, "the check fails, the rebuild is whole")
            excel = self.tmp / "a" / "generated" / "data" / "global" / "excel"
            self.assertEqual({p.name for p in excel.glob("*.txt")}, set(TABLES))

    def test_a_partial_entry_is_evicted_so_the_next_run_keeps_and_replays_a_good_one(self):
        with contextlib.ExitStack() as st:
            for c in self.patched():
                st.enter_context(c)
            PARTIAL.append(True)
            outs = []
            for _ in range(3):
                buf = io.StringIO()
                with redirect_stdout(buf):
                    rc = pd2_merge.main(["--root", str(self.tmp / "a"), "--d2r", str(self.tmp / "d2r")])
                self.assertEqual(rc, 0)
                outs.append(buf.getvalue())
        self.assertIn("rebuilding without the rule cache", outs[0])
        self.assertNotIn("rebuilding", outs[1], "the partial entry is gone: the rule runs and keeps a whole one")
        self.assertIn("0 of 1 rules replayed", outs[1])
        self.assertNotIn("rebuilding", outs[2])
        self.assertIn("1 of 1 rules replayed", outs[2], "the good entry is replayed")
        self.assertEqual(len(list((self.tmp / "cache" / "rules").glob("*/*"))), 1)


class RaceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.cache, self.root = self.tmp / "cache" / "rules", self.tmp / "root"
        (self.root / "generated").mkdir(parents=True)

    def keep(self, name="out.txt", where="data/global/excel"):
        def run():
            write(self.root / "generated" / where / name, "x")
        with redirect_stdout(io.StringIO()):
            self.assertFalse(pd2_cache.rule(self.cache, "k", {"p": self.root}, run))
        return next(self.cache.glob("k/*"))

    def test_an_evict_or_prune_during_a_replay_leaves_a_run_not_a_crash(self):
        entry = self.keep()
        real, hit = shutil.copy2, []

        def copy_then_lose_the_entry(src, dst, *a, **k):
            if not hit:  # another run deletes the entry between two files of this replay
                hit.append(1)
                pd2_cache.evict([entry])
            return real(src, dst, *a, **k)

        runs = []
        with mock.patch.object(shutil, "copy2", copy_then_lose_the_entry), redirect_stdout(io.StringIO()):
            replayed = pd2_cache.rule(self.cache, "k", {"p": self.root}, lambda: runs.append(1) or write(
                self.root / "generated" / "data/global/excel/out.txt", "x"))
        self.assertTrue(hit)
        self.assertFalse(replayed, "the entry was gone: the rule ran")
        self.assertEqual(runs, [1])
        self.assertTrue((self.root / "generated/data/global/excel/out.txt").is_file())

    def test_evict_and_prune_rename_before_deleting_and_sweep_leftovers(self):
        entry = self.keep()
        dead = entry.with_name(entry.name + ".dead.1")
        stale = entry.with_name("x.tmp9")
        for d in (dead, stale):
            write(d / "f", "x")
            for q in (d, d / "f"):
                os.utime(q, (time.time() - 7200,) * 2)
        self.assertEqual(pd2_cache.prune(self.cache.parent, 10**9), [])
        self.assertFalse(dead.exists() or stale.exists(), "stale leftovers swept")
        self.assertTrue(entry.is_dir())
        pd2_cache.evict([entry])
        self.assertFalse(entry.exists() or list(self.cache.glob("k/*")))
        pd2_cache.evict([entry])  # already gone: no error

    def test_a_replay_skips_other_runs_tmp_folders_with_a_truncated_manifest(self):
        entry = self.keep()
        for name in ("zz.tmp123", "zz.dead.123"):  # a keep mid-write sorts among the entries
            write(self.cache / "k" / name / "manifest.json", '{"reads": ')
        (entry / "manifest.json").write_text('{"reads": ', "utf-8")  # and a damaged real entry
        runs = []
        with redirect_stdout(io.StringIO()):
            replayed = pd2_cache.rule(self.cache, "k", {"p": self.root}, lambda: runs.append(1) or write(
                self.root / "generated" / "data/global/excel/out.txt", "x"))
        self.assertFalse(replayed)
        self.assertEqual(runs, [1], "ran the rule, no JSONDecodeError")

    def test_prune_survives_a_leftover_vanishing_and_spares_a_busy_one(self):
        entry = self.keep()
        busy = entry.with_name("busy.tmp7")
        write(busy / "deep/f", "x")
        os.utime(busy, (time.time() - 7200,) * 2)  # its own mtime is old, a file in it is new
        ghost = entry.with_name("ghost.tmp8")
        write(ghost / "f", "x")
        real = pd2_cache._newest

        def vanish(p):
            if p == ghost:
                shutil.rmtree(p)
            return real(p)
        with mock.patch.object(pd2_cache, "_newest", vanish):
            pd2_cache.prune(self.cache.parent, 10**9)
        self.assertTrue(busy.is_dir() and entry.is_dir())

    def test_delete_that_is_refused_keeps_the_entry_and_is_not_counted_freed(self):
        entry = self.keep()
        with mock.patch.object(os, "rename", side_effect=PermissionError), redirect_stdout(io.StringIO()) as out:
            self.assertFalse(pd2_cache._delete(entry))
        self.assertIn("in use", out.getvalue())
        self.assertTrue(entry.is_dir())

    def test_touches_counts_removed_files_and_folders(self):
        for field in ("removed", "rmdirs"):
            e = self.cache / field
            write(e / "manifest.json", json.dumps({"files": [], "removed": [], "rmdirs": [], field: ["data/global/excel/a"]}))
            self.assertTrue(pd2_cache.touches(e, "data/global/excel/"))

    def test_evict_touching_spares_entries_the_check_does_not_read(self):
        tables, other = self.keep("a.txt"), None
        self.root2 = self.root
        with redirect_stdout(io.StringIO()):
            pd2_cache.rule(self.cache, "k2", {"p": self.root}, lambda: write(self.root / "generated/data/global/x.bin", "x"))
        other = next(self.cache.glob("k2/*"))
        pd2_cache.evict([tables, other], "data/global/excel/")
        self.assertFalse(tables.exists())
        self.assertTrue(other.is_dir())


class ToolkitKeyTests(unittest.TestCase):
    """A rule's key takes the toolkit modules its code imports, not the whole toolkit (#808)."""

    def kit(self, tmp: Path) -> Path:
        kit = tmp / "kit"
        for f in ("modkit/__init__.py", "modkit/base.py", "modkit/dc6.py", "modkit/other.py", "modkit/hd.py",
                  "shared/classic_import/__init__.py", "shared/classic_import/extract.py",
                  "shared/classic_import/ids.py"):
            write(kit / f, "")
        write(kit / "modkit/base.py", "from . import dc6\n")
        write(kit / "shared/classic_import/merge.py", "from . import extract\n")
        return kit

    def rule(self, tmp: Path, line: str):
        write(tmp / "tk_rule.py", line + "\ndef go(): pass\ndef main():\n    from modkit import other\n")
        sys.path.insert(0, str(tmp))
        self.addCleanup(sys.path.remove, str(tmp))
        self.addCleanup(lambda: [sys.modules.pop(m) for m in [m for m in sys.modules if m.startswith("tk_")]])
        import tk_rule
        return tk_rule.go

    def key(self, fn, kit, tmp):
        return pd2_merge.rule_key(fn, kit, pd2_merge.base_key(tmp, kit, tmp), "r")

    def test_only_the_modules_a_rule_imports_change_its_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            kit = self.kit(tmp)
            write(tmp / "pd2-baseline.json", "{}")
            fn = self.rule(tmp, "def modkit():\n    from modkit import base\n    return base\n")
            before = self.key(fn, kit, tmp)
            names = {p.relative_to(kit).as_posix() for p in pd2_cache.toolkit_sources(kit, pd2_cache.sources(fn))}
            self.assertEqual(names, {"modkit/__init__.py", "modkit/base.py", "modkit/dc6.py"})  # base's relative import too
            for unrelated in ("modkit/other.py", "modkit/hd.py", "shared/classic_import/ids.py"):  # main() is no import
                write(kit / unrelated, "X = 1\n")
                self.assertEqual(self.key(fn, kit, tmp), before, unrelated)
            for used in ("modkit/base.py", "modkit/dc6.py", "modkit/__init__.py"):
                write(kit / used, (kit / used).read_text() + "# edit\n")
                after = self.key(fn, kit, tmp)
                self.assertNotEqual(after, before, used)
                before = after

    def test_the_merge_driver_is_in_every_key(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            kit = self.kit(tmp)
            write(tmp / "pd2-baseline.json", "{}")
            fn = self.rule(tmp, "")
            before = self.key(fn, kit, tmp)
            write(kit / "shared/classic_import/ids.py", "X = 1\n")  # the driver does not import it
            self.assertEqual(self.key(fn, kit, tmp), before)
            for f in ("merge.py", "extract.py"):
                write(kit / "shared/classic_import" / f, (kit / "shared/classic_import" / f).read_text() + "# edit\n")
                after = self.key(fn, kit, tmp)
                self.assertNotEqual(after, before, f)
                before = after

    def test_presets_rule_key_leaves_out_toolkit_modules_it_does_not_import(self):
        kit = pd2_ids.toolkit(Path(pd2_cache.__file__).resolve().parent.parent)
        if not (kit / "modkit").is_dir():
            self.skipTest("no d2rAI-toolkit beside the repo")
        used = {p.relative_to(kit).as_posix()
                for p in pd2_cache.toolkit_sources(kit, pd2_cache.sources(pd2_merge.RULES["presets-hd.tsv"]))}
        every = {p.relative_to(kit).as_posix() for p in (kit / "modkit").glob("*.py")}
        self.assertTrue(every, "the toolkit has modkit modules")
        self.assertEqual(used, set(), "presets-hd.tsv imports no toolkit module")


class SlimSourcesTests(unittest.TestCase):
    """The presets rule is 97% of a merge: its key must not follow the id lists and the level merge (#808)."""
    def names(self, rule):
        return {p.name for p in pd2_cache.sources(pd2_merge.RULES[rule])}

    def test_presets_rule_does_not_take_the_id_and_level_merge_into_its_key(self):
        used = self.names("presets-hd.tsv")
        for m in ("pd2_ids.py", "pd2_levels.py", "pd2_strings.py", "pd2_tables.py", "pd2_skilldesc.py", "pd2_automap.py"):
            self.assertNotIn(m, used)
        self.assertLessEqual({"pd2_table.py", "pd2_ds1.py"}, used)

    def test_other_rules_still_key_on_pd2_ids(self):
        self.assertIn("pd2_ids.py", self.names("ids-skills.tsv"))
        self.assertIn("pd2_levels.py", self.names("level-files.tsv"))

    def test_slim_modules_keep_the_old_names(self):
        import pd2_ds1, pd2_levels, pd2_table
        for n in ("IdError", "Table", "excel", "load", "toolkit", "KEY"):
            self.assertIs(getattr(pd2_ids, n), getattr(pd2_table, n))
        for n in ("SLOTS", "col", "ds1_header", "norm", "object_list"):
            self.assertIs(getattr(pd2_levels, n), getattr(pd2_ds1, n))


class PackageKeyTests(unittest.TestCase):
    def test_editing_any_module_of_a_package_changes_its_rules_sources(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write(tmp / "pk_top.py", "from pk_pkg.apply import go\n")
            write(tmp / "pk_pkg" / "__init__.py", "")
            write(tmp / "pk_pkg" / "apply.py", "from .other import f\ndef go(): pass\n")
            write(tmp / "pk_pkg" / "other.py", "def f(): pass\n")
            write(tmp / "pk_pkg" / "passes" / "__init__.py", "")
            write(tmp / "pk_pkg" / "passes" / "found.py", "x = 1\n")  # found by discovery, imported by no one
            sys.path.insert(0, str(tmp))
            try:
                import pk_top
                paths = pd2_cache.sources(pk_top.go)
                self.assertEqual({p.relative_to(tmp).as_posix() for p in paths},
                                 {"pk_pkg/__init__.py", "pk_pkg/apply.py", "pk_pkg/other.py",
                                  "pk_pkg/passes/__init__.py", "pk_pkg/passes/found.py"})
                before = pd2_cache.key(paths)
                write(tmp / "pk_pkg" / "passes" / "found.py", "x = 2\n")
                self.assertNotEqual(pd2_cache.key(pd2_cache.sources(pk_top.go)), before)
            finally:
                sys.path.remove(str(tmp))
                for m in [m for m in sys.modules if m.startswith("pk_")]:
                    sys.modules.pop(m)

    def test_package_imports_under_tools_enter_the_key(self):
        forms = {"absolute_from": "from pk2_pkg.consts import X\n", "import_dotted": "import pk2_pkg.consts\n",
                 "from_package": "from pk2_pkg import consts\n"}
        for name, line in forms.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as tmp:
                tmp = Path(tmp)
                write(tmp / "pk2_rule.py", line + "def go(): pass\n")
                write(tmp / "pk2_pkg" / "__init__.py", "")
                write(tmp / "pk2_pkg" / "consts.py", "X = 1\n")
                sys.path.insert(0, str(tmp))
                try:
                    import pk2_rule
                    before = pd2_cache.key(pd2_cache.sources(pk2_rule.go))
                    write(tmp / "pk2_pkg" / "consts.py", "X = 2\n")
                    self.assertNotEqual(pd2_cache.key(pd2_cache.sources(pk2_rule.go)), before)
                finally:
                    sys.path.remove(str(tmp))
                    for m in [m for m in sys.modules if m.startswith("pk2_")]:
                        sys.modules.pop(m)

    def test_module_files_resolves_every_import_form(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            for f in ("pk3_a/__init__.py", "pk3_a/deep.py", "pk3_a/sub/__init__.py", "pk3_a/sub/mod.py"):
                write(tmp / f, "")
            rel = lambda fs: sorted(f.relative_to(tmp).as_posix() for f in fs)
            self.assertEqual(rel(pd2_cache.module_files(tmp, "pk3_a.deep", [])), ["pk3_a/deep.py"])
            self.assertEqual(rel(pd2_cache.module_files(tmp, "pk3_a", ["deep", "sub", "nope"])),
                             ["pk3_a/__init__.py", "pk3_a/deep.py", "pk3_a/sub/__init__.py"])
            # from .. import deep, written in pk3_a/sub/mod.py: the folder two levels up is pk3_a's, module empty
            self.assertEqual(rel(pd2_cache.module_files(tmp / "pk3_a", "", ["deep"])), ["pk3_a/__init__.py", "pk3_a/deep.py"])
            self.assertEqual(rel(pd2_cache.module_files(tmp / "pk3_a", "sub.mod", [])), ["pk3_a/sub/mod.py"])

    def test_the_base_key_follows_cache_format_not_the_cache_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write(tmp / "pd2-baseline.json", "{}")
            before = pd2_merge.base_key(tmp, tmp / "kit", tmp)
            self.assertEqual(pd2_merge.base_key(tmp, tmp / "kit", tmp), before)  # pd2_cache.py's bytes are not in it
            with mock.patch.object(pd2_cache, "CACHE_FORMAT", pd2_cache.CACHE_FORMAT + 1):
                self.assertNotEqual(pd2_merge.base_key(tmp, tmp / "kit", tmp), before)

    def test_the_presets_rule_covers_the_whole_presets_package(self):
        names = {p.relative_to(Path(pd2_cache.__file__).parent).as_posix()
                 for p in pd2_cache.sources(pd2_merge.RULES["presets-hd.tsv"])}
        self.assertIn("presets/water.py", names)
        self.assertIn("presets/hooks/tall.py", names)


class CheckOutputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_check_output_names_what_is_missing(self):
        gen = self.tmp / "gen"
        self.assertIn("lacks monstats.txt", pd2_merge.check_output(gen, ("monstats.txt",), 0))
        write(gen / "global" / "excel" / "a.txt", "")
        self.assertIn("fewer than 2", pd2_merge.check_output(gen, (), 2))
        self.assertIsNone(pd2_merge.check_output(gen, ("a.txt",), 1))


class MergeSlotTests(unittest.TestCase):
    """The machine-wide cap on concurrent merges (#760): a counting semaphore of OS-locked slot files."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name) / "merge-slots"

    def test_the_cap_holds_with_one_more_worker_than_slots(self):
        live, peak, lock = [0], [0], threading.Lock()

        def worker():
            with redirect_stderr(io.StringIO()), pd2_cache.Slots(self.dir, 3, poll=0.01):
                with lock:
                    live[0] += 1
                    peak[0] = max(peak[0], live[0])
                time.sleep(0.15)
                with lock:
                    live[0] -= 1
        ts = [threading.Thread(target=worker) for _ in range(4)]
        [t.start() for t in ts]
        [t.join(30) for t in ts]
        self.assertEqual(peak[0], 3)
        self.assertEqual(live[0], 0)

    def test_spare_slots_leave_one_slot_free_and_come_from_the_holders_own_pool(self):
        held = pd2_cache.Slots(self.dir, 4, poll=0.01)
        held.acquire()
        spares = pd2_cache.spare_slots(9)
        self.addCleanup(lambda: [s.release() for s in spares])
        held.release()
        self.assertEqual(len(spares), 2)  # 4 slots: the holder's, two spares, one left free for an arriving merge
        probe = pd2_cache.Slots(self.dir, 4)
        self.assertTrue(probe.try_acquire())
        probe.release()
        self.assertEqual({s.n for s in spares}, {4})

    def test_spare_slots_are_none_without_a_held_slot_and_clone_is_unheld(self):
        self.assertEqual(pd2_cache.spare_slots(3), [])
        a = pd2_cache.Slots(self.dir, 2, "x", 0.5)
        a.acquire()
        self.addCleanup(a.release)
        c = a.clone()
        self.assertEqual((c.folder, c.n, c.what, c.poll, c.f), (a.folder, 2, "x", 0.5, None))

    def test_a_waiter_says_it_is_queued_once_and_takes_the_slot_a_release_frees(self):
        a = pd2_cache.Slots(self.dir, 1, poll=0.01)
        a.acquire()
        buf, got = io.StringIO(), []

        def wait():
            with redirect_stderr(buf):
                b = pd2_cache.Slots(self.dir, 1, poll=0.01)
                b.acquire()
                got.append(b)
        t = threading.Thread(target=wait)
        t.start()
        time.sleep(0.2)
        self.assertEqual(got, [])
        a.release()
        t.join(10)
        self.assertEqual(len(got), 1)
        got[0].release()
        self.assertEqual(buf.getvalue().count("queued: 1/1 slots busy"), 1)

    def test_a_killed_holder_frees_its_slot(self):
        code = ("import sys, time; sys.path.insert(0, sys.argv[1]); import pd2_cache; from pathlib import Path\n"
                "s = pd2_cache.Slots(Path(sys.argv[2]), 1); s.acquire(); print('held', flush=True); time.sleep(60)")
        p = subprocess.Popen([sys.executable, "-c", code, str(Path(pd2_cache.__file__).parent), str(self.dir)],
                             stdout=subprocess.PIPE, text=True)
        self.addCleanup(p.kill)
        self.assertEqual(p.stdout.readline().strip(), "held")
        s = pd2_cache.Slots(self.dir, 1)
        self.assertIsNone(s._try(0), "held by the child")
        p.kill()
        p.wait(10)
        deadline, f = time.time() + 10, None
        while time.time() < deadline and not (f := s._try(0)):
            time.sleep(0.05)
        self.assertTrue(f, "the OS dropped the dead holder's lock")
        f.close()

    def test_ci_shares_the_agents_cap_and_names_itself_when_it_waits(self):
        home = Path(self.tmp.name)
        with mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true", "PD2R_CACHE": str(home), "PD2R_MERGE_SLOTS": "1"}):
            self.assertEqual(pd2_cache.slot_home(), home)
            first, second = pd2_cache.merge_slots(home), pd2_cache.merge_slots(home)
            self.assertEqual(first.folder, home / "merge-slots")  # where the agents' merges count
            first.acquire()
            self.addCleanup(first.release)
            second.poll, buf = 0.01, io.StringIO()

            def wait():
                with redirect_stderr(buf):
                    second.acquire()
            t = threading.Thread(target=wait, daemon=True)
            t.start()
            time.sleep(0.3)
            self.assertIn("CI merge queued: 1/1 slots busy", buf.getvalue())
            first.release()
            t.join(10)
            self.assertIsNotNone(second.f)
            second.release()
        with mock.patch.dict(os.environ, {"PD2R_CACHE": str(home / "none")}):
            self.assertIsNone(pd2_cache.slot_home())

    def test_an_agents_spare_slots_never_reach_the_ci_reserve_and_a_clone_keeps_the_pool(self):
        held = pd2_cache.Slots(self.dir, 5, poll=0.01, reserved=2)  # agent pool: slots 0-2
        held.acquire()
        self.addCleanup(held.release)
        c = held.clone()
        self.assertEqual((c.reserved, c.ci, c.n, c.f), (2, False, 5, None))
        spares = pd2_cache.spare_slots(9)
        self.addCleanup(lambda: [x.release() for x in spares])
        self.assertEqual(len(spares), 1)  # holder + 1 spare + 1 free of the 3-slot pool
        ci = pd2_cache.Slots(self.dir, 5, reserved=2, ci=True)
        for i in (3, 4):  # both reserved slots are free to take
            f = ci._try(i)
            self.assertIsNotNone(f)
            f.close()
        free = [i for i in range(3) if (f := ci._try(i)) and not f.close()]
        self.assertEqual(len(free), 1)

    def test_agents_never_hold_a_reserved_slot_and_ci_can(self):
        agents = [pd2_cache.Slots(self.dir, 4, poll=0.01, reserved=2) for _ in range(3)]
        agents[0].acquire()
        agents[1].acquire()
        self.addCleanup(agents[0].release)
        self.addCleanup(agents[1].release)
        buf, got = io.StringIO(), []

        def wait():
            with redirect_stderr(buf):
                agents[2].acquire()
                got.append(1)
        t = threading.Thread(target=wait, daemon=True)
        t.start()
        time.sleep(0.3)
        self.assertEqual(got, [], "the third agent waits though two slots are free")
        self.assertIn("merge queued: 2/2 slots busy", buf.getvalue())
        self.assertIn("reserved for CI", buf.getvalue())
        ci = [pd2_cache.Slots(self.dir, 4, poll=0.01, reserved=2, ci=True) for _ in range(2)]
        for c in ci:
            c.acquire()
            self.addCleanup(c.release)
        self.assertEqual(sorted(int(c.f.name.rsplit(".", 1)[1]) for c in ci), [2, 3])
        self.assertEqual(got, [])
        agents[0].release()
        t.join(10)
        self.assertEqual(got, [1])
        agents[2].release()

    def test_ci_takes_any_slot_when_all_are_busy_but_one(self):
        agents = [pd2_cache.Slots(self.dir, 3, poll=0.01, reserved=1) for _ in range(2)]
        for a in agents:
            a.acquire()
            self.addCleanup(a.release)
        ci = pd2_cache.Slots(self.dir, 3, poll=0.01, reserved=1, ci=True)
        ci.acquire()
        self.addCleanup(ci.release)
        self.assertTrue(ci.f.name.endswith("slot.2"))
        later = pd2_cache.Slots(self.dir, 3, poll=0.01, reserved=1, ci=True)
        self.assertEqual(later._order(), [2, 1, 0])
        self.assertEqual(agents[0]._order(), [0, 1])

    def test_reserving_everything_still_leaves_agents_one_slot(self):
        self.assertEqual(pd2_cache.Slots(self.dir, 2, reserved=5)._order(), [0])
        self.assertEqual(pd2_cache.Slots(self.dir, 3)._order(), [0, 1, 2])

    def test_the_reserve_comes_from_the_environment(self):
        home = Path(self.tmp.name)
        with mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": "5", "PD2R_CI_RESERVED": "1", "GITHUB_ACTIONS": ""}):
            s = pd2_cache.merge_slots(home)
            self.assertEqual((s.reserved, s.ci, s._order()), (1, False, [0, 1, 2, 3]))
        with mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": "5", "GITHUB_ACTIONS": "true"}):
            os.environ.pop("PD2R_CI_RESERVED", None)
            s = pd2_cache.merge_slots(home)
            self.assertEqual((s.reserved, s.ci), (2, True))
        with mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": "5", "PD2R_CI_RESERVED": "x", "GITHUB_ACTIONS": ""}):
            self.assertEqual(pd2_cache.merge_slots(home).reserved, 2)

    def test_the_cap_comes_from_the_environment(self):
        with mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": "2"}):
            self.assertEqual(pd2_cache.merge_slots(Path(self.tmp.name)).n, 2)
        with mock.patch.dict(os.environ, {"PD2R_MERGE_SLOTS": ""}):
            self.assertEqual(pd2_cache.merge_slots(Path(self.tmp.name)).n, 4)


class SingleFlightTests(unittest.TestCase):
    """Two merges that miss one rule key together run the rule once (#821)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.cache = self.tmp / "rules"
        self.runs: list[str] = []
        self.roots = []
        for n in ("one", "two"):
            write(self.tmp / n / "merge" / "r.tsv", "same")  # equal inputs: one's entry is two's hit
            self.roots.append(self.tmp / n)

    def rule_for(self, i, before=None, slots=None, seconds=0.0):
        root = self.roots[i]

        def work():
            if before:
                before()
            self.runs.append(root.name)
            (root / "merge" / "r.tsv").read_text()
            time.sleep(seconds)
            write(root / "generated" / "data" / "a.txt", "A")
        return pd2_cache.rule(self.cache, "k", {"p": root}, work, slots=slots)  # callers capture stdout (it is per process)

    def test_the_second_merge_waits_and_replays_with_the_same_bytes(self):
        res = {}
        ts = [threading.Thread(target=lambda i=i: res.__setitem__(i, self.rule_for(i, seconds=1.0))) for i in (0, 1)]
        with redirect_stdout(io.StringIO()) as so:
            ts[0].start()
            time.sleep(0.4)
            ts[1].start()
            [t.join(30) for t in ts]
        self.assertEqual(self.runs, ["one"], "the rule ran once")
        self.assertEqual((res[0], res[1]), (False, True))
        self.assertIn("waited for another run", so.getvalue())
        a = [(r / "generated" / "data" / "a.txt").read_bytes() for r in self.roots]
        self.assertEqual(a[0], a[1], "the same bytes with and without the wait")

    def test_a_holder_that_dies_hands_the_rule_to_the_waiter(self):
        lockfile = self.tmp / "rules.locks" / "k.local"
        code = ("import sys, time; sys.path.insert(0, sys.argv[1]); import pd2_cache; from pathlib import Path\n"
                "with pd2_cache.lock(Path(sys.argv[2])):\n    print('held', flush=True); time.sleep(60)")
        p = subprocess.Popen([sys.executable, "-c", code, str(Path(pd2_cache.__file__).parent), str(lockfile)],
                             stdout=subprocess.PIPE, text=True)
        self.addCleanup(p.kill)
        self.assertEqual(p.stdout.readline().strip(), "held")
        res = []
        t = threading.Thread(target=lambda: res.append(self.rule_for(1)))
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            t.start()
            time.sleep(1.5)
            self.assertEqual(self.runs, [], "waiting on the live holder")
            p.kill()
            p.wait(10)
            t.join(30)
        self.assertEqual(self.runs, ["two"], "the holder died with nothing kept: the waiter runs the rule itself")
        self.assertEqual(res, [False])

    def test_a_merge_holding_the_only_slot_does_not_deadlock_on_a_key_lock(self):
        folder = self.tmp / "slots"
        a, b = pd2_cache.Slots(folder, 1, poll=0.01), pd2_cache.Slots(folder, 1, poll=0.01)
        b.acquire()  # merge two holds the only slot (an earlier rule ran)
        started, res = threading.Event(), {}

        def holder():  # merge one: holds the key lock, then needs the slot to run
            with redirect_stderr(io.StringIO()):
                res[0] = self.rule_for(0, before=lambda: (started.set(), a.acquire()), slots=a)
            a.release()

        def waiter():  # merge two: needs the key lock one holds, while holding the slot one waits for
            started.wait(10)
            time.sleep(0.2)
            res[1] = self.rule_for(1, slots=b)
        ts = [threading.Thread(target=holder), threading.Thread(target=waiter)]
        with redirect_stdout(io.StringIO()):
            [t.start() for t in ts]
            [t.join(20) for t in ts]
        self.assertFalse(any(t.is_alive() for t in ts), "deadlock: each waits for what the other holds")
        self.assertEqual(self.runs, ["one"])
        self.assertTrue(res[1], "two replays what one kept")
        self.assertIsNone(b.f, "and gave its slot up while it waited")


class CiPolicyTests(unittest.TestCase):
    """The gate replays rules without letting a PR's code write what main's code replays (#820)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.home = Path(self.tmp.name)
        self.cache = self.home / "rules"
        self.root = self.home / "wt"
        self.out = self.root / "generated" / "data"
        self.root.mkdir()
        self.runs: list[str] = []

    def go(self, policy, text="v"):
        def work():
            self.runs.append(text)
            write(self.out / "a.txt", text)
        with redirect_stdout(io.StringIO()):
            return pd2_cache.rule(self.cache, "k", {"p": self.root}, work, policy)

    def test_scope_picks_the_policy(self):
        for scope, want in (("main", ("main", None, True)), ("pr-12", ("pr", self.home / "rules-pr" / "pr-12", True))):
            with mock.patch.dict(os.environ, {"PD2R_CI_SCOPE": scope}):
                p = pd2_cache.ci_policy(self.home)
                self.assertEqual((p.origin, p.scratch, p.write), want)
                self.assertEqual(p.trusted, frozenset({"main"}))
        for scope in ("", "pr-", "pr-1/../x", "pr-1x", "other"):
            with mock.patch.dict(os.environ, {"PD2R_CI_SCOPE": scope}):
                self.assertIsNone(pd2_cache.ci_policy(self.home), f"{scope!r}: no cache, merge fresh")

    def test_a_pr_replays_mains_entries_and_never_writes_the_shared_folder(self):
        main = pd2_cache.Policy("main", frozenset({"main"}))
        pr = pd2_cache.Policy("pr", frozenset({"main"}), self.home / "rules-pr" / "pr-7")
        self.assertFalse(self.go(main, "m"))
        shutil.rmtree(self.root / "generated")  # a merge starts from an empty generated/
        self.assertTrue(self.go(pr, "p"), "main's entry replays in a PR")
        self.assertEqual(self.runs, ["m"])
        shared = sorted(self.cache.rglob("manifest.json"))
        write(self.root / "x", "")
        # a rule whose inputs differ: a PR run is kept in the scratch folder only
        (self.root / "merge").mkdir()
        def other():
            self.runs.append("p")
            write(self.out / "a.txt", "p")
        with redirect_stdout(io.StringIO()):
            pd2_cache.rule(self.cache, "k2", {"p": self.root}, other, pr)
            self.assertTrue(pd2_cache.rule(self.cache, "k2", {"p": self.root}, other, pr), "the PR replays its own")
        self.assertEqual(sorted(self.cache.rglob("manifest.json")), shared, "the shared folder is untouched")
        self.assertTrue(list((self.home / "rules-pr" / "pr-7" / "k2").glob("*/manifest.json")))
        with redirect_stdout(io.StringIO()):
            self.assertFalse(pd2_cache.rule(self.cache, "k2", {"p": self.root}, other,
                                            pd2_cache.Policy("pr", frozenset({"main"}), self.home / "rules-pr" / "pr-8")),
                             "another PR does not read it")
            self.assertFalse(pd2_cache.rule(self.cache, "k2", {"p": self.root}, other, main), "nor does main")

    def test_main_replays_only_entries_main_kept(self):
        self.assertFalse(self.go(pd2_cache.Policy()), "an agent's local merge keeps an entry")
        main = pd2_cache.Policy("main", frozenset({"main"}))
        shutil.rmtree(self.root / "generated")
        self.assertFalse(self.go(main), "main does not replay a local entry: runs fresh")
        shutil.rmtree(self.root / "generated")
        self.assertTrue(self.go(main), "but replays its own")
        shutil.rmtree(self.root / "generated")
        self.assertTrue(self.go(pd2_cache.Policy()), "agents replay anything")
        # an entry from before origins (no key) is not main's either
        for m in self.cache.rglob("manifest.json"):
            d = json.loads(m.read_text("utf-8"))
            d.pop("origin")
            m.write_text(json.dumps(d), "utf-8")
        shutil.rmtree(self.root / "generated")
        self.assertFalse(self.go(main))

    def test_a_run_without_a_scope_keeps_nothing(self):
        self.assertFalse(self.go(pd2_cache.Policy(write=False)))
        shutil.rmtree(self.root / "generated")
        self.assertFalse(self.go(pd2_cache.Policy(write=False)))
        self.assertEqual(self.runs, ["v", "v"])
        self.assertFalse(self.cache.exists())

    def test_a_pr_gate_evicts_only_its_own_scratch_entries(self):
        shared, mine = self.cache / "k" / "e1", self.home / "rules-pr" / "pr-7" / "k" / "e2"
        pr = pd2_cache.Policy("pr", frozenset({"main"}), self.home / "rules-pr" / "pr-7")
        self.assertEqual(pd2_cache.own([shared, mine], pr), [mine])
        self.assertEqual(pd2_cache.own([shared, mine], pd2_cache.Policy("main", frozenset({"main"}))), [shared, mine])

    def test_the_main_gate_does_not_wait_on_an_agent_run_it_cannot_replay(self):
        lock = self.home / "rules.locks" / "k.local"
        with pd2_cache.lock(lock):  # an agent's run of the key
            t0 = time.time()
            self.assertFalse(self.go(pd2_cache.Policy("main", frozenset({"main"}))))
            self.assertLess(time.time() - t0, 5)
        self.assertEqual(self.runs, ["v"])

    def test_sweep_drops_stale_pr_folders_empty_key_folders_and_old_locks(self):
        old = time.time() - 10 * 86400
        for n in ("rules-pr/pr-1/k/e", "rules-pr/pr-2/k/e"):
            write(self.home / n / "manifest.json", "{}")
        for n in ("rules-pr/pr-1", "rules-pr/pr-1/k", "rules-pr/pr-1/k/e", "rules-pr/pr-1/k/e/manifest.json"):
            os.utime(self.home / n, (old, old))
        (self.home / "rules" / "empty").mkdir(parents=True)
        write(self.home / "rules.locks" / "old.main", "")
        write(self.home / "rules.locks" / "new.main", "")
        os.utime(self.home / "rules.locks" / "old.main", (old, old))
        pd2_cache.sweep(self.home)
        self.assertFalse((self.home / "rules-pr" / "pr-1").exists())
        self.assertTrue((self.home / "rules-pr" / "pr-2" / "k" / "e").is_dir())
        self.assertFalse((self.home / "rules" / "empty").exists())
        self.assertFalse((self.home / "rules.locks" / "old.main").exists())
        self.assertTrue((self.home / "rules.locks" / "new.main").exists())

    def test_prune_counts_the_pr_scratch_folders(self):
        cache = self.home
        for n in ("rules/k/old", "rules-pr/pr-3/k/e"):
            write(cache / n / "files" / "f", "x" * 100)
            write(cache / n / "manifest.json", json.dumps({"bytes": 100}))
            os.utime(cache / n, (time.time() - 7200, time.time() - 7200))
        gone = pd2_cache.prune(cache, 100)
        self.assertEqual([p.relative_to(cache).as_posix() for p in gone], ["rules/k/old"], "oldest first, scratch counted")
        self.assertEqual(len(pd2_cache.prune(cache, 0)), 1)

    def test_pd2_merge_main_uses_the_cache_on_ci_only_with_a_scope(self):
        calls = []
        def fake(root, d2r, cache, slots, policy):
            calls.append((cache, policy))
            return []
        for env, want in (({"PD2R_CI_SCOPE": "pr-5"}, (self.home / "rules", "pr")), ({}, (None, "local")),
                          ({"PD2R_CI_SCOPE": "main"}, (self.home / "rules", "main"))):
            calls.clear()
            with mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true", "PD2R_CACHE": str(self.home), **env}),                     mock.patch.object(pd2_merge, "merge", fake), redirect_stdout(io.StringIO()):
                pd2_merge.main(["--root", str(self.root)])
            self.assertEqual((calls[0][0], calls[0][1].origin), want, env)


if __name__ == "__main__":
    unittest.main()
