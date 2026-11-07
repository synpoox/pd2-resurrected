"""pd2_art's cache and pd2_items' PD2 pictures, with a fake upscaler (nearest 4x; needs d2rAI-toolkit's modkit)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_art as A  # noqa: E402
import pd2_items as M  # noqa: E402

try:
    A.modkit()
except ValueError as e:  # CI has no d2rAI-toolkit (private)
    raise unittest.SkipTest(str(e))
from modkit import dc6, images  # noqa: E402
from test_ids import table  # noqa: E402


def fake_esrgan(calls: list):
    def run(src: Path, dst: Path) -> None:
        calls.append(sorted(p.name for p in src.iterdir()))
        for p in src.iterdir():
            w, h, rgba = images.read_png(p)
            assert min(w, h) >= A.MIN_SIZE, "a small frame turns Real-ESRGAN's whole run to noise"
            images.write_png(dst / p.name, images.scale((w, h, rgba), w * A.SCALE, h * A.SCALE))
    return run


def solid(w: int, h: int, grey: int) -> images.Image:
    return w, h, bytes((grey, grey, grey, 255)) * w * h


class UpscaleTests(unittest.TestCase):
    def test_upscales_missing_frames_in_one_run_and_reads_cached_ones_after(self):
        with tempfile.TemporaryDirectory() as tmp:
            art, calls = Path(tmp), []
            frames = {"items/invwss": solid(2, 3, 90), "spells/skillicon/22": solid(1, 1, 40)}
            with mock.patch.object(A, "esrgan", fake_esrgan(calls)):
                first = A.upscale(art, frames)
                again = A.upscale(art, frames)
            self.assertEqual(len(calls), 1)
            self.assertEqual(first, again)
            self.assertEqual(first["items/invwss"][:2], (8, 12))
            self.assertEqual(len(list((art / "items").glob("invwss-*.png"))), 1)

    def test_a_changed_classic_frame_is_upscaled_again(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            with mock.patch.object(A, "esrgan", fake_esrgan(calls)):
                A.upscale(Path(tmp), {"items/x": solid(1, 1, 10)})
                A.upscale(Path(tmp), {"items/x": solid(1, 1, 20)})
            self.assertEqual(len(calls), 2)

    def test_missing_art_with_no_upscaler_names_the_setting(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(A, "UPSCALER", Path(tmp) / "none.exe"):
            with self.assertRaisesRegex(A.ArtError, "REALESRGAN"):
                A.upscale(Path(tmp), {"items/x": solid(1, 1, 10)})


def plate_with_glyph(size: int, x: int, y: int, w: int) -> images.Image:
    """A brown plate with a light square glyph at (x, y), w wide."""
    px = [(230, 230, 235, 255) if x <= c < x + w and y <= r < y + w else (90, 60, 40, 255)
          for r in range(size) for c in range(size)]
    return size, size, b"".join(bytes(p) for p in px)


class IconStyleTests(unittest.TestCase):
    def test_pd2_glyph_on_d2rs_plate_in_d2rs_glyph_colours(self):
        spots = [(4, 4), (30, 4), (4, 30), (30, 30), (17, 17)]  # D2R's icons: glyphs in different places
        classic = [plate_with_glyph(48, x, y, 12) for x, y in spots]
        hd = [plate_with_glyph(130, x * 130 // 48, y * 130 // 48, 32) for x, y in spots]
        finish = A.icon_style(hd, classic)
        pd2 = plate_with_glyph(48, 17, 4, 12)  # PD2's glyph: top middle
        up = images.scale(pd2, 192, 192)
        w, h, rgba = finish(pd2, up)
        pixel = lambda x, y: rgba[(y * w + x) * 4:(y * w + x) * 4 + 3]  # noqa: E731
        self.assertEqual((w, h), (130, 130))
        self.assertGreater(min(pixel(62, 26)), 180)  # the glyph, in D2R's light glyph colours
        self.assertEqual(pixel(20, 110), bytes((90, 60, 40)))  # D2R's plate, untouched
        dot = 10, 10, bytes(400)
        self.assertEqual(finish(dot, images.scale(dot, 40, 40))[:2], (27, 27))  # a placeholder keeps its size
        # a motif every icon of one sheet shares (the Paladin's aura ring) stays off the plate taken from all sheets
        motif = [plate_with_glyph(130, 17 * 130 // 48, 17 * 130 // 48, 32)] * 5
        w, h, rgba = A.icon_style(motif, classic, plate_frames=hd + motif[:2])(pd2, up)
        self.assertEqual(rgba[(70 * w + 62) * 4:(70 * w + 62) * 4 + 3], bytes((90, 60, 40)))


class ItemPictureTests(unittest.TestCase):
    def test_new_invfiles_get_hd_sprites_in_each_users_folder_and_the_classic_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pd2, d2r, out = root / "pd2", root / "d2r", root / "out"
            (d2r / "global" / "palette" / "units").mkdir(parents=True)
            (d2r / "global" / "palette" / "units" / "pal.dat").write_bytes(bytes(i for i in range(256) for _ in "bgr"))
            items = pd2 / "global" / "items"
            items.mkdir(parents=True)
            for name in ("invwss", "invgem", "invrin"):  # 28 x 28, one palette index
                dc6.write(items / f"{name}.dc6", [(28, 28, 0, 0, (bytes((28,)) + bytes((50,)) * 28 + b"\x80") * 28)])
            d2r_inv = {"invrin"}  # a D2R picture PD2 also ships: D2R's HD art stays
            # invfile -> the item tables of the items drawn with it; invnone has no DC6
            uses = {"invwss": {"misc"}, "invgem": {"misc", "weapons"}, "invrin": {"misc"}, "invnone": {"misc"}}
            with mock.patch.object(A, "esrgan", fake_esrgan([])):
                pictures = M.item_pictures(root / "art", pd2, d2r, out, uses, d2r_inv)
            self.assertEqual(pictures, {"invwss": "pd2/invwss", "invgem": "pd2/invgem"})
            ui = out / "hd" / "global" / "ui" / "items"
            sprite, fw, n = images.read_sprite(ui / "misc" / "pd2" / "invwss.sprite")
            self.assertEqual((sprite[:2], fw, n), ((98, 98), 98, 1))  # D2R's 98 px per 28 px cell
            self.assertEqual(images.read_sprite(ui / "misc" / "pd2" / "invwss.lowend.sprite")[0][:2], (49, 49))
            self.assertTrue((ui / "weapon" / "pd2" / "invgem.sprite").is_file())
            self.assertEqual((out / "global" / "items" / "invwss.dc6").read_bytes(),
                             (items / "invwss.dc6").read_bytes())
            self.assertFalse((out / "global" / "items" / "invrin.dc6").exists())

    def test_an_extract_with_no_item_pictures_fails(self):  # #351: it gave "0 upscaled" and no HD pictures
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(M.I.IdError, "pd2_extract"):
                M.item_pictures(root / "art", root / "pd2", root / "d2r", root / "out", {"invwss": {"misc"}}, set())


class HdPictureTests(unittest.TestCase):
    def test_pd2_pictures_replace_borrowed_and_d2r_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            hd = Path(tmp)
            (hd / "items.json").write_text(json.dumps([{"rin": {"asset": "ring/ring"}}]))
            nagel = {"normal": "ring/nagel", "uber": "x", "ultra": "x"}
            (hd / "uniques.json").write_text(json.dumps([{"nagelring": nagel}]))
            (hd / "sets.json").write_text("[]")
            pd2 = {"misc": table("name|code|type|invfile\nRing|rin|ring|invrin2\nShard|wss|xx|invwss\n", "code")}
            d2r = {"misc": table("name|code|type|invfile\nRing|rin|ring|invrin\n", "code")}
            pictures = {"invwss": "pd2/invwss", "invrin2": "pd2/invrin2", "invnag": "pd2/invnag"}
            models = {}
            files = M.hd_items(hd, pd2, d2r, [("Nagelring", "rin", "Nagelring", "invnag")],
                               [("Gift", "rin", None, "")], pictures, models)
            # the ground model each picture's item drew before: its own, the base item's, else the table's first
            self.assertEqual(models, {"pd2/invrin2": "ring/ring", "pd2/invwss": "ring/ring", "pd2/invnag": "ring/nagel"})
            (hd / "misc" / "ring").mkdir(parents=True)
            (hd / "misc" / "ring" / "ring.json").write_text("{}")
            (hd / "misc" / "ring" / "nagel.json").write_text('{"n": 1}')
            M.ground_models(hd, hd / "out", pictures, models, {"invwss": {"misc"}, "invrin2": {"misc"},
                                                               "invnag": {"misc", "weapons"}})
            self.assertEqual((hd / "out" / "weapon" / "pd2" / "invnag.json").read_text(), '{"n": 1}')
            self.assertTrue((hd / "out" / "misc" / "pd2" / "invwss.json").is_file())
            self.assertEqual(files["items.json"], [{"rin": {"asset": "pd2/invrin2"}}, {"wss": {"asset": "pd2/invwss"}}])
            nag = {"normal": "pd2/invnag", "uber": "pd2/invnag", "ultra": "pd2/invnag"}
            self.assertEqual(files["uniques.json"], [{"nagelring": nag}])
            rin = {"normal": "pd2/invrin2", "uber": "pd2/invrin2", "ultra": "pd2/invrin2"}
            self.assertEqual(files["sets.json"], [{"gift": rin}])


if __name__ == "__main__":
    unittest.main()
