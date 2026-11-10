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
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_cache  # noqa: E402
import pd2_extract  # noqa: E402
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
        write(self.tmp / "child" / "child_merge.py", child)  # a file: a rule's module needs a __file__
        env = {**os.environ, "PD2R_CACHE": str(self.tmp / "cache")}
        env.pop("GITHUB_ACTIONS", None)
        procs = [subprocess.Popen([sys.executable, str(self.tmp / "child" / "child_merge.py"), str(self.tmp / n), str(self.tmp / "d2r"), n], env=env,
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


if __name__ == "__main__":
    unittest.main()
