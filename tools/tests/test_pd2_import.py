import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pd2_import  # noqa: E402


class SnapshotTest(unittest.TestCase):
    def test_snapshot(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            gen, out = root / "generated" / "data", root / "out"
            for p in ("global/excel/b.txt", "global/a.d2"):
                (gen / p).parent.mkdir(parents=True, exist_ok=True)
                (gen / p).write_bytes(b"new\r\n")
            (out / "files" / "data" / "old.txt").parent.mkdir(parents=True)
            (out / "files" / "data" / "old.txt").write_bytes(b"old")
            pd2_import.snapshot(gen, out)
            self.assertEqual((out / "files" / "data" / "global" / "excel" / "b.txt").read_bytes(), b"new\r\n")
            self.assertFalse((out / "files" / "data" / "old.txt").exists())
            self.assertFalse((root / "generated").exists())
            self.assertEqual((out / "pd2-import.txt").read_bytes(),
                             (pd2_import.HEADER + "\ndata/global/a.d2\ndata/global/excel/b.txt\n").encode())


if __name__ == "__main__":
    unittest.main()
