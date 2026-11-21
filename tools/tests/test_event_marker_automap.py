"""The event marker (objects.txt AutoMap 1499) needs a frame: D2R's last automap strip (Default14) has 99, so cel 1499
drew nothing after #965 removed PD2's extra strips (#1601). Frames 0-98 stay stock's bytes."""
import os
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "files" / "data"
HEAD = struct.Struct("<4sHHIIIIIIII")
STRIP = Path("hd/global/ui/automap/default14")


def rows(path):
    d = path.read_bytes()
    _m, _v, _fw, w, h, _z, n, _a, _b, size, _f = HEAD.unpack_from(d)
    assert size == w * h * 4
    return d[HEAD.size:HEAD.size + size], w, h, n


def stock_root():
    tk = os.environ.get("D2R_TOOLKIT") or str(ROOT.parent / "d2rAI-toolkit")
    return next(iter(Path(tk).glob("d2r-data/*/data")), None)


class EventMarkerAutomap(unittest.TestCase):
    def test_frame_99_drawn_and_stock_frames_kept(self):
        stock = stock_root()
        for name in ("tiles.sprite", "tiles.lowend.sprite"):
            mine, w, h, n = rows(DATA / STRIP / name)
            self.assertEqual(n, 100, name)
            stride = w // n
            frame = b"".join(mine[r * w * 4 + 99 * stride * 4:r * w * 4 + w * 4] for r in range(h))
            self.assertTrue(any(frame[i + 3] for i in range(0, len(frame), 4)), f"{name}: frame 99 is empty")
            if stock is None or not (stock / STRIP / name).is_file():
                continue
            old, ow, oh, on = rows(stock / STRIP / name)
            self.assertEqual((on, oh), (99, h))
            for r in range(h):
                self.assertEqual(mine[r * w * 4:r * w * 4 + ow * 4], old[r * ow * 4:(r + 1) * ow * 4], f"{name} row {r}")

    def test_classic_cel_1499(self):
        d = (DATA / "global/ui/automap/maximap.dc6").read_bytes()
        self.assertGreater(struct.unpack_from("<I", d, 20)[0], 1499)


if __name__ == "__main__":
    unittest.main()
