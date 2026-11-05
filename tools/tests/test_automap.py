"""pd2_automap on synthetic tiles: strips of 4 frames, a grey palette (needs d2rAI-toolkit's modkit)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_automap as A  # noqa: E402

try:
    A.modkit()
except ValueError as e:  # CI has no d2rAI-toolkit (private)
    raise unittest.SkipTest(str(e))
from modkit import dc6, images  # noqa: E402

LAYOUT = '{\r\n    "fields": {\r\n        "tilesDefaultFilename": [\r\n            "AUTOMAP/Default00/Tiles",\r\n' \
         '            "AUTOMAP/Default01/Tiles",\r\n        ],\r\n    }\r\n}\r\n'


def cel(colour: int, w: int = 16, h: int = 32, ox: int = 0, oy: int = 0) -> dc6.Frame:
    """A classic cel of one palette index."""
    return w, h, ox, oy, (bytes((w,)) + bytes((colour,)) * w + b"\x80") * h


class ApplyTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        self.pd2, self.d2r, self.out = self.tmp / "pd2", self.tmp / "d2r", self.tmp / "out"
        (self.d2r / "global" / "palette" / "units").mkdir(parents=True)
        (self.d2r / "global" / "palette" / "units" / "pal.dat").write_bytes(bytes(i for i in range(256) for _ in "bgr"))
        (self.d2r / A.LAYOUT).parent.mkdir(parents=True)
        (self.d2r / A.LAYOUT).write_bytes(LAYOUT.encode())
        # D2R's tiles: 4 + 3 frames, HD 64 px in a 66 px stride, lowend 32 in 34; grey 200 + cel (lowend 100 + cel)
        for n, cels in ((0, range(4)), (1, range(4, 7))):
            for sheet, fw, h, base in zip(A.sheets(n), (64, 32), (128, 64), (200, 100)):
                frames = [(fw, h, bytes((base + c,) * 3 + (255,)) * fw * h) for c in cels]
                images.write_sprite(self.d2r / sheet, images.hstack(frames, fw + 2, h), fw, len(frames), lowend=False)
        dc6.write(self.pd2 / A.CLASSIC / "maximaps.dc6", [cel(1, 8, 16)])

    def pixel(self, sheet: Path, frame: int, x: int, y: int) -> int:
        img, fw, _ = images.read_sprite(self.out / sheet)
        return img[2][(y * img[0] + frame * (fw + 2) + x) * 4]  # red channel

    def test_fills_the_last_strip_adds_strips_and_lists_them(self):
        pd2 = [cel(9)] * 7 + [cel(70), cel(80), cel(90)]
        dc6.write(self.pd2 / A.CLASSIC / "maximap.dc6", pd2)
        self.assertEqual(A.apply(self.pd2, self.d2r, self.out), 10)
        self.assertFalse((self.out / A.sheets(0)[0]).exists())  # full strips stay D2R's files
        for low, (fw, h) in enumerate(((64, 128), (32, 64))):
            last, new = A.sheets(1)[low], A.sheets(2)[low]
            self.assertEqual([images.read_sprite(self.out / s)[1:] for s in (last, new)], [(fw, 4), (fw, 2)])
            self.assertEqual(images.read_sprite(self.out / last)[0][:2], (4 * (fw + 2), h))  # D2R's stride and height
            kept = (200, 100)[low]
            middle = [self.pixel(last, f, fw // 2, h // 2) for f in range(4)]
            self.assertEqual(middle, [kept + 4, kept + 5, kept + 6, 70])
            self.assertEqual([self.pixel(new, f, fw // 2, h // 2) for f in range(2)], [80, 90])
        layout = (self.out / A.LAYOUT).read_bytes().decode()
        self.assertIn('"AUTOMAP/Default01/Tiles",\r\n            "AUTOMAP/Default02/Tiles",\r\n        ]', layout)
        for name in ("maximap.dc6", "maximaps.dc6"):  # classic graphics read PD2's cels as they are
            self.assertEqual((self.out / A.CLASSIC / name).read_bytes(), (self.pd2 / A.CLASSIC / name).read_bytes())

    def test_a_bigger_cel_sits_at_its_offset_scaled(self):
        # 32x48 at x offset -16: 4x is 128x192, its right half lands in the 64x128 cell, bottom-aligned and clipped
        dc6.write(self.pd2 / A.CLASSIC / "maximap.dc6", [cel(9)] * 7 + [cel(50, 32, 48, -16)])
        A.apply(self.pd2, self.d2r, self.out)
        sheet = A.sheets(1)[0]
        self.assertEqual([self.pixel(sheet, 3, x, y) for x, y in ((0, 0), (63, 127), (65, 64))], [50, 50, 0])

    def test_no_new_cels_writes_no_tiles(self):
        dc6.write(self.pd2 / A.CLASSIC / "maximap.dc6", [cel(9)] * 7)
        self.assertEqual(A.apply(self.pd2, self.d2r, self.out), 7)
        self.assertFalse((self.out / "hd").exists())

    def test_missing_pd2_art_fails(self):
        with self.assertRaisesRegex(ValueError, "maximap.dc6.*pd2_extract"):
            A.apply(self.pd2, self.d2r, self.out)


if __name__ == "__main__":
    unittest.main()
