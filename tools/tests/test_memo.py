"""pd2_cache.memo (the per-map coverage scores, #854): hit, misses, CI scopes, single-flight, and data-only entries (#870)."""
import json
import shutil
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_cache  # noqa: E402


def write(p: Path, text: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, "utf-8")


class MemoTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.cache = self.tmp / "rules"
        self.root = self.tmp / "p"
        self.roots = {"p": self.root}
        write(self.root / "a.txt", "one")
        self.runs: list[str] = []

    def memo(self, policy=pd2_cache.Policy(), key="k", sleep=0.0):
        def compute(trace):
            trace.see(self.root / "a.txt")
            trace.see(self.root / "gone.txt")  # absent: a file that appears later is a change
            self.runs.append("x")
            time.sleep(sleep)
            return {"score.json": (self.root / "a.txt").read_bytes(), "m_walls.png": b"\x89PNG"}
        return pd2_cache.memo(self.cache, key, self.roots, compute, policy)

    def test_a_second_call_replays_the_files(self):
        first = self.memo()
        second = self.memo()
        self.assertEqual((first[1], second[1]), (False, True))
        self.assertEqual(first[0], second[0])
        self.assertEqual(self.runs, ["x"])

    def test_a_changed_file_it_saw_misses(self):
        self.memo()
        write(self.root / "a.txt", "two")
        got, hit = self.memo()
        self.assertFalse(hit)
        self.assertEqual(got["score.json"], b"two")

    def test_a_file_it_saw_missing_that_appears_misses(self):
        self.memo()
        write(self.root / "gone.txt", "now here")
        self.assertFalse(self.memo()[1])
        self.assertEqual(len(self.runs), 2)

    def test_a_file_it_did_not_see_changes_nothing(self):
        self.memo()
        write(self.root / "other.txt", "x")
        self.assertTrue(self.memo()[1])

    def test_entries_are_json_and_files_only(self):
        self.memo()
        entry = next((self.cache / "k").iterdir())
        self.assertEqual(sorted(p.name for p in entry.iterdir()), ["files", "manifest.json"])
        self.assertIsInstance(json.loads((entry / "manifest.json").read_text("utf-8")), dict)
        self.assertEqual((entry / "files" / "m_walls.png").read_bytes(), b"\x89PNG")

    def test_a_damaged_entry_is_a_miss(self):
        self.memo()
        entry = next((self.cache / "k").iterdir())
        (entry / "files" / "score.json").unlink()
        self.assertFalse(self.memo()[1])

    def test_ci_scopes_split_main_and_pr(self):
        main = pd2_cache.Policy("main", frozenset({"main"}))
        pr = pd2_cache.Policy("pr", frozenset({"main"}), self.tmp / "rules-pr" / "pr-7")
        self.memo(pr)
        self.assertTrue(self.memo(pr)[1], "a PR replays its own scratch")
        self.assertFalse((self.cache / "k").exists(), "and kept nothing in the shared folder")
        self.assertFalse(self.memo(main)[1], "main does not replay a PR's entry")
        self.assertTrue(self.memo(main)[1], "but replays its own")
        self.assertTrue(self.memo(pr)[1], "a PR replays main's entry too")
        write(self.root / "a.txt", "two")
        self.assertFalse(self.memo(pr)[1])
        other = pd2_cache.Policy("pr", frozenset({"main"}), self.tmp / "rules-pr" / "pr-8")
        self.assertFalse(self.memo(other)[1], "another PR's scratch is not read")

    def test_a_policy_that_keeps_nothing_computes_every_time(self):
        off = pd2_cache.Policy(write=False)
        self.memo(off)
        self.assertFalse(self.memo(off)[1])
        self.assertEqual(len(self.runs), 2)
        self.assertFalse(self.cache.exists())

    def test_two_calls_that_miss_together_compute_once(self):
        res = {}
        ts = [threading.Thread(target=lambda i=i: res.__setitem__(i, self.memo(sleep=1.0)[1])) for i in (0, 1)]
        ts[0].start()
        time.sleep(0.4)
        ts[1].start()
        [t.join(30) for t in ts]
        self.assertEqual(self.runs, ["x"], "the second waited and replayed")
        self.assertEqual((res[0], res[1]), (False, True))

    def test_sources_leave_out(self):
        import importlib
        pkg = self.tmp / "t"
        write(pkg / "memo_a.py", "import memo_b\nimport memo_c\n\n\ndef f():\n    pass\n")
        write(pkg / "memo_b.py", "x = 1\n")
        write(pkg / "memo_c.py", "y = 1\n")
        sys.path.insert(0, str(pkg))
        self.addCleanup(sys.path.remove, str(pkg))
        a = importlib.import_module("memo_a")
        self.addCleanup(lambda: [sys.modules.pop(m, None) for m in ("memo_a", "memo_b", "memo_c")])
        self.assertEqual([p.name for p in pd2_cache.sources(a.f)], ["memo_a.py", "memo_b.py", "memo_c.py"])
        self.assertEqual([p.name for p in pd2_cache.sources(a.f, (pkg / "memo_c.py",))], ["memo_a.py", "memo_b.py"])


if __name__ == "__main__":
    unittest.main()
