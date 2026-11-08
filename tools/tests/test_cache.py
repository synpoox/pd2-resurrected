"""pd2_cache (#440): the extract lock, the cache key, and the shared extract and merge."""
import io
import json
import os
import tempfile
import threading
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


class MergeCacheTests(unittest.TestCase):
    def test_a_second_merge_of_the_same_key_copies_the_first(self):
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
            write(root / "merge" / "ids.json", "{}")
            (d2r / "global" / "excel").mkdir(parents=True)
            (tmp / "cache").mkdir()
            runs = []

            def rule(rule, pd2, d2r_, out):
                runs.append(1)
                write(out / "x.txt", rule.read_text())

            args = ["--root", str(root), "--d2r", str(d2r)]
            with cache_env(tmp / "cache"), mock.patch.dict(pd2_merge.RULES, {"ids.json": rule}), redirect_stdout(io.StringIO()):
                for _ in range(2):
                    self.assertEqual(pd2_merge.main(args), 0)
                    self.assertEqual((root / "generated" / "data" / "x.txt").read_text(), "{}")
                self.assertEqual(len(runs), 1, "the second run is a cache hit")
                write(root / "tools" / "tests" / "test_x.py", "")
                self.assertEqual(pd2_merge.main(args), 0)
                self.assertEqual(len(runs), 1, "a test-only change still hits")
                write(root / "merge" / "ids.json", '{"changed": 1}')
                self.assertEqual(pd2_merge.main(args), 0)
                self.assertEqual(len(runs), 2, "a changed merge/ is a new key")
                self.assertEqual((root / "generated" / "data" / "x.txt").read_text(), '{"changed": 1}')
                with mock.patch.dict(os.environ, {"GITHUB_ACTIONS": "true"}):
                    self.assertEqual(pd2_merge.main(args), 0)
                self.assertEqual(len(runs), 3, "CI always merges fresh")
            self.assertEqual(len(list((tmp / "cache" / "merge").iterdir())), 2, "two keys, no temp folder left")


if __name__ == "__main__":
    unittest.main()
