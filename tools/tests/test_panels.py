import struct
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_panels as P  # noqa: E402
from pd2_skilltrees import Image  # noqa: E402
import pd2_table  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def ramp(w: int, h: int) -> Image:
    """Each pixel's first byte is its x, drawn (byte 3 255) except column 0."""
    img = Image(w, h)
    for y in range(h):
        for x in range(1, w):
            img.px[(y * w + x) * 4], img.px[(y * w + x) * 4 + 3] = x, 255
    return img


class PanelsTests(unittest.TestCase):
    def test_widen_draws_the_second_column_twice_and_moves_the_frame_apart(self):
        img = ramp(10, 4)
        P.widen(img, grid=[2, 1], cell=2, size=[2, 1], border=1)  # frame x 1-6: border, cells 2-3 and 4-5, border
        self.assertEqual([img.px[(1 * 10 + x) * 4] for x in range(10)], [1, 2, 3, 4, 5, 4, 5, 6, 8, 9])

    def test_dc6_panel_round_trips_and_keeps_later_frames(self):
        panel = ramp(5, 6)
        frames = [(3, 4, 0, 0), (2, 4, 0, 0), (3, 2, 0, 0), (2, 2, 0, 0)]
        blobs = [struct.pack("<8i", 0, w, h, ox, oy, 0, 0, len(b := P.dc6_frame(panel, (k % 2) * 3, (k // 2) * 4, w, h)))
                 + b + b"\xee\xee\xee" for k, (w, h, ox, oy) in enumerate(frames)]
        blobs.append(b"extra frame")
        ptrs, at = [], 24 + 4 * len(blobs)
        for blob in blobs:
            ptrs.append(at)
            at += len(blob)
        data = struct.pack("<6I", 6, 1, 0, 0xEEEEEEEE, 1, len(blobs)) + struct.pack(f"<{len(ptrs)}I", *ptrs) + b"".join(blobs)
        _, back = P.dc6_panel(data)
        self.assertEqual(back.px, panel.px)
        out = P.redraw_dc6(data, {"grid": [1, 1], "cell": 1, "size": [2, 1], "border": 0})
        self.assertTrue(out.endswith(b"extra frame"))
        _, wide = P.dc6_panel(out)
        self.assertEqual([wide.px[(1 * 5 + x) * 4] for x in range(5)], [0, 1, 2, 2, 4])

    @unittest.skipUnless((pd2_table.D2R_DATA / "hd/global/ui/panel/inventory/classic_background.sprite").is_file(),
                         "D2R art not fetched: `modkit fetch`")
    def test_altar_art_rebuilds_byte_for_byte(self):  # #1413
        with tempfile.TemporaryDirectory() as tmp:
            art = [P.HD_ALTAR, P.HD_ALTAR.replace(".sprite", ".lowend.sprite"), P.SD_ALTAR]
            for rel in (*art, "files/data/hd/global/ui/panel/horadric_cube/horadriccube_bg.sprite",
                        "files/data/global/ui/panel/supertransmogrifier.dc6", "files/data/global/ui/panel/pd2_invchar6.dc6"):
                (Path(tmp) / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy(ROOT / rel, Path(tmp) / rel)
            P.build_altar(Path(tmp))
            for rel in art:
                self.assertEqual((Path(tmp) / rel).read_bytes(), (ROOT / rel).read_bytes(), rel)


if __name__ == "__main__":
    unittest.main()
