import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pd2_cache_verify as v  # noqa: E402


class VerifyTest(unittest.TestCase):
    def make(self, files):
        d = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, d, True)
        for k, c in files.items():
            (d / k).parent.mkdir(parents=True, exist_ok=True)
            (d / k).write_bytes(c)
        return d

    def test_identical(self):
        a, b = self.make({"data/a.txt": b"1"}), self.make({"data/a.txt": b"1"})
        self.assertEqual(v.main([str(a), str(b)]), 0)

    def test_differences_named(self):
        a = self.make({"data/a.txt": b"1", "data/b.txt": b"x"})
        b = self.make({"data/a.txt": b"2", "data/c.txt": b"x"})
        out = v.diff(v.digests(a), v.digests(b))
        self.assertEqual([line.split(":")[0] for line in out], ["data/a.txt", "data/b.txt", "data/c.txt"])
        self.assertEqual(v.main([str(a), str(b)]), 1)


if __name__ == "__main__":
    unittest.main()
