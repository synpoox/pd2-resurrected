"""The event marker (objects.txt AutoMap 1499) needs a frame: D2R's last automap strip (Default14) has 99, so cel 1499
drew nothing after #965 removed PD2's extra strips (#1601)."""
import csv
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "files" / "data"


class EventMarkerAutomap(unittest.TestCase):
    def test_default14_holds_cel_1499(self):
        for name in ("tiles.sprite", "tiles.lowend.sprite"):
            head = (DATA / "hd/global/ui/automap/default14" / name).read_bytes()[:32]
            frames = struct.unpack_from("<I", head, 20)[0]  # SpA1 header: frame count
            self.assertEqual(frames, 100, name)

    def test_marker_cel_in_range(self):
        rows = list(csv.reader((DATA / "global/excel/objects.txt").open(encoding="utf-8", errors="replace"),
                               delimiter="\t"))
        col = rows[0].index("AutoMap")
        cel = int(next(r for r in rows if r[0] == "empty_eventmarker")[col])
        self.assertEqual(cel, 1499)
        self.assertLess(cel, 15 * 100)


if __name__ == "__main__":
    unittest.main()
