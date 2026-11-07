"""pd2_animdata on synthetic animdata.d2 files (no PD2 or D2R data)."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_animdata as A  # noqa: E402


def rec(name: str, frames: int, speed: int = 256) -> bytes:
    return name.encode().ljust(8, b"\0") + struct.pack("<II", frames, speed) + bytes(144)


def file(*recs: bytes) -> bytes:
    buckets = [[] for _ in range(256)]
    for r in recs:
        buckets[A.bucket(A.name_of(r))].append(r)
    return A.write(buckets)


D2R = file(rec("MPA1HTH", 16), rec("AMA1XBW", 20), rec("SKNUHTH", 8))
PD2 = file(rec("$7A1HTH", 10), rec("AMA1XBW", 20, 294), rec("SKNUHTH", 8, 128), rec("MPA1HTH", 16))


class MergeTests(unittest.TestCase):
    def test_adds_new_codes_and_takes_changes_but_kept(self):
        data, added, changed = A.merge(D2R, PD2, {"AMA1XBW"})
        self.assertEqual((added, changed), (1, 1))
        recs = {A.name_of(r): r for b in A.read(data) for r in b}
        self.assertEqual(recs["$7A1HTH"], rec("$7A1HTH", 10))  # Lucion's attack ends after 10 frames
        self.assertIn(rec("$7A1HTH", 10), A.read(data)[A.bucket("$7A1HTH")])  # in the bucket D2R looks in
        self.assertEqual(recs["AMA1XBW"], rec("AMA1XBW", 20))  # listed: D2R's
        self.assertEqual(recs["SKNUHTH"], rec("SKNUHTH", 8, 128))  # not listed: PD2's

    def test_refuses_a_stale_line_and_a_broken_file(self):
        with self.assertRaisesRegex(A.AnimDataError, "PD2 does not change: MPA1HTH"):
            A.merge(D2R, PD2, {"AMA1XBW", "MPA1HTH"})
        with self.assertRaisesRegex(A.AnimDataError, "bytes after the last bucket"):
            A.read(D2R + b"x")


if __name__ == "__main__":
    unittest.main()
