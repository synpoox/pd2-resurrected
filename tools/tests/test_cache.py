"""pd2_cache (#440): the extract lock, the cache key, the shared extract and merge, and the changed-files-only install."""
import io
import json
import os
import shutil
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
import pd2_strings  # noqa: E402


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
        class OneFile:
            def __init__(self, path): pass
            def names(self): return ["data\\x.txt"]
            def read(self, name): return b"x"

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "cache").mkdir()
            out = tmp / "wt" / "pd2-data"
            out.parent.mkdir()
            for i, base in enumerate([b'{"archives": {}}', b'{"archives": {}, "v": 2}']):
                (tmp / "b.json").write_bytes(base)
                args = ["--install", str(tmp), "--baseline", str(tmp / "b.json")]
                with cache_env(tmp / "cache"), mock.patch.object(pd2_extract, "ROOT", tmp / "wt"), \
                        mock.patch.object(pd2_extract.mpq, "Archive", OneFile), redirect_stdout(io.StringIO()):
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


class RuleCacheTests(unittest.TestCase):
    def test_a_rule_reruns_exactly_when_its_code_or_what_it_read_changed(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            root, d2r = tmp / "wt", tmp / "d2r"
            write(root / "pd2-baseline.json", "B")
            (root / "pd2-data").mkdir()
            (root / "pd2-data" / "baseline.json").write_bytes(pd2_extract.stamp(b"B"))
            for n, t in (("a", "x"), ("b", "b"), ("c", "c"), ("d", "d")):
                write(root / "merge" / f"{n}.tsv", t)
            (d2r / "global" / "excel").mkdir(parents=True)
            (tmp / "cache").mkdir()
            gen = root / "generated" / "data"

            def run(**env):
                RUNS.clear()
                with mock.patch.dict(os.environ, env), redirect_stdout(io.StringIO()) as so:
                    self.assertEqual(pd2_merge.main(["--root", str(root), "--d2r", str(d2r)]), 0)
                self.assertIn("c ran", so.getvalue(), "a replay prints what the rule printed")
                return sorted(RUNS), {p.relative_to(gen).as_posix(): p.read_text() for p in gen.rglob("*.txt")}

            rules = {"a.tsv": rule_a, "b.tsv": rule_b, "c.tsv": rule_c, "d.tsv": rule_d}
            with cache_env(tmp / "cache"), mock.patch.dict(pd2_merge.RULES, rules, clear=True):
                ran, fresh = run()
                self.assertEqual(ran, ["a", "b", "c", "d"])
                self.assertEqual(fresh, {"a.txt": "X", "b.txt": "Xb", "sub/c.txt": "cFalse2", "d.txt": "False"})
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
                self.assertEqual((ran, out["sub/c.txt"]), (["b", "c"], "CTrue3"), "c looked for the zz.txt b added")
                write(root / "tools" / "tests" / "test_x.py", "")
                write(root / "docs" / "x.md", "")
                self.assertEqual(run()[0], [], "files no rule reads leave every rule cached")
                write(d2r / "global" / "tiles" / "act1" / "x.dt1", "")
                self.assertEqual(run(), (["d"], {**out, "d.txt": "True"}), "D2R's fetched tiles rerun the rule that looks")
                with mock.patch.object(pd2_cache, "sources", lambda fn: [root / "tools" / "tests" / "test_x.py"]):
                    write(root / "tools" / "tests" / "test_x.py", "changed")
                    self.assertEqual(run()[0], ["a", "b", "c", "d"], "changed code reruns its rules")
                self.assertEqual(run(GITHUB_ACTIONS="true")[0], ["a", "b", "c", "d"], "CI always merges fresh")
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

    def test_sources_follow_imports_beside_the_module(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            write(tmp / "m_one.py", "import os\nfrom m_two import f\ndef g(): pass\n")
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
            gone = pd2_cache.prune(cache, 150)
            self.assertEqual([p.relative_to(cache).as_posix() for p in gone], ["rules/k/old", "merge/legacy", "rules/k/new"])
            self.assertTrue((cache / "rules/j/fresh").is_dir(), "used in the last 15 minutes: kept over the cap")


class SyncTests(unittest.TestCase):
    def test_writes_only_what_differs_and_removes_the_rest(self):
        with tempfile.TemporaryDirectory() as tmp:
            src, dest = Path(tmp) / "src", Path(tmp) / "dest"
            write(src / "a.txt", "same")
            write(src / "sub" / "b.txt", "new")
            dest.mkdir()
            shutil.copy2(src / "a.txt", dest / "a.txt")  # same size and time: kept
            write(dest / "sub" / "b.txt", "old")  # same size, other time: rewritten
            write(dest / "gone" / "c.txt", "stale")
            self.assertEqual(pd2_cache.sync(src, dest), 1)
            self.assertEqual((dest / "sub" / "b.txt").read_text(), "new")
            self.assertFalse((dest / "gone").exists())
            self.assertEqual(pd2_cache.sync(src, dest), 0)

    def test_install_keeps_files_outside_the_mod_and_rewrites_only_changes(self):
        try:
            pd2_strings.modkit()  # puts d2rAI-toolkit on sys.path
        except ValueError as e:  # CI has no d2rAI-toolkit (private)
            self.skipTest(str(e))
        from modkit import install as mi
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            proj, game = tmp / "proj", tmp / "game"
            write(proj / "mod.json", json.dumps({"name": "M", "plugins": []}))
            write(proj / "out" / "data" / "global" / "excel" / "a.txt", "1")
            write(game / "D2R.exe", "")
            write(game / "data" / "base.txt", "base game")
            with mock.patch.object(mi, "game_running", lambda: False):
                self.assertIn("(1 written", pd2_cache.install(proj, game))
                data = game / "mods" / "M" / "M.mpq" / "data"
                stamp = (data / "global" / "excel" / "a.txt").stat().st_mtime_ns
                time.sleep(0.05)
                write(proj / "out" / "data" / "b.txt", "2")
                self.assertIn("(1 written", pd2_cache.install(proj, game))
            self.assertEqual((data / "global" / "excel" / "a.txt").stat().st_mtime_ns, stamp, "unchanged file kept")
            self.assertEqual((data / "b.txt").read_text(), "2")
            self.assertTrue((game / "mods" / "M" / "modinfo.json").is_file())
            self.assertEqual((game / "data" / "base.txt").read_text(), "base game")


if __name__ == "__main__":
    unittest.main()
