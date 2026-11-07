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
                with mock.patch.dict(os.environ, {"PD2R_CACHE": str(tmp / "cache")}), \
                        mock.patch.object(pd2_extract, "ROOT", tmp / "wt"), \
                        mock.patch.object(pd2_extract.mpq, "Archive", OneFile), redirect_stdout(io.StringIO()):
                    self.assertEqual(pd2_extract.main(args), 0)
                self.assertTrue(pd2_cache.is_link(out))
                self.assertEqual((out / "data" / "x.txt").read_bytes(), b"x")
                self.assertEqual(len(list((tmp / "cache" / "extract").iterdir())), i + 1, "one extract per stamp")
            self.assertTrue((tmp / "cache" / "pd2_extract.lock").is_file())
            os.rmdir(out) if os.name == "nt" else out.unlink()


class MergeCacheTests(unittest.TestCase):
    def test_a_second_merge_of_the_same_key_copies_the_first(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            root, d2r = tmp / "wt", tmp / "d2r"
            write(root / "pd2-baseline.json", "B")
            (root / "pd2-data").mkdir()
            (root / "pd2-data" / "baseline.json").write_bytes(pd2_extract.stamp(b"B"))
            write(root / "merge" / "ids.json", "{}")
            (d2r / "global" / "excel").mkdir(parents=True)
            (tmp / "cache").mkdir()
            runs = []

            def rule(rule, pd2, d2r_, out):
                runs.append(1)
                write(out / "x.txt", rule.read_text())

            args = ["--root", str(root), "--d2r", str(d2r)]
            with mock.patch.dict(os.environ, {"PD2R_CACHE": str(tmp / "cache")}), \
                    mock.patch.dict(pd2_merge.RULES, {"ids.json": rule}), redirect_stdout(io.StringIO()):
                for _ in range(2):
                    self.assertEqual(pd2_merge.main(args), 0)
                    self.assertEqual((root / "generated" / "data" / "x.txt").read_text(), "{}")
                self.assertEqual(len(runs), 1, "the second run is a cache hit")
                write(root / "merge" / "ids.json", '{"changed": 1}')
                self.assertEqual(pd2_merge.main(args), 0)
                self.assertEqual(len(runs), 2, "a changed merge/ is a new key")
                self.assertEqual((root / "generated" / "data" / "x.txt").read_text(), '{"changed": 1}')
                with mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}):
                    self.assertEqual(pd2_merge.main(args), 0)
                self.assertEqual(len(runs), 3, "CI always merges fresh")
            self.assertEqual(len(list((tmp / "cache" / "merge").iterdir())), 2, "two keys, no temp folder left")


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
