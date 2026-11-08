"""pd2_skill_icons on synthetic sheets: tiny DC6 and sprite files, a grey palette (needs d2rAI-toolkit's modkit)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_art as A  # noqa: E402
import pd2_skill_icons as S  # noqa: E402

try:
    S.modkit()
except ValueError as e:  # CI has no d2rAI-toolkit (private)
    raise unittest.SkipTest(str(e))
from modkit import dc6, images  # noqa: E402
from test_art import fake_esrgan  # noqa: E402

SPELLS = Path("global") / "ui" / "spells"


def frame(colour: int) -> dc6.Frame:
    """A 48 x 48 classic icon of one palette index."""
    return 48, 48, 0, 0, (bytes((48,)) + bytes((colour,)) * 48 + b"\x80") * 48


def strip(colours: list[int], size: int = 130) -> images.Image:
    """An HD (130 px) or lowend (65 px) strip at D2R's stride, a frame per grey."""
    return images.hstack([(size, size, bytes((c, c, c, 255)) * size * size) for c in colours], size + 2, size)


class ApplyTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.tmp = Path(tmp.name)
        self.pd2, self.d2r, self.out = self.tmp / "pd2", self.tmp / "d2r", self.tmp / "out"
        (self.d2r / "global" / "palette" / "units").mkdir(parents=True)
        (self.d2r / "global" / "palette" / "units" / "pal.dat").write_bytes(bytes(i for i in range(256) for _ in "bgr"))
        # the fake style keeps PD2's frame, upscaled to the HD frame's size (icon_style itself: test_art)
        def style(hd, classic, plate_frames):
            return lambda frame, up: images.scale(up, hd[0][0] * frame[0] // classic[0][0],
                                                  hd[0][1] * frame[1] // classic[0][1])
        for patch in (mock.patch.object(A, "esrgan", fake_esrgan([])), mock.patch.object(A, "icon_style", style)):
            patch.start()
            self.addCleanup(patch.stop)
        self.rule = self.tmp / "skill-icons.txt"
        self.rule.write_text("# notes\nsor\n", "utf-8")

    def sheets(self, d2r: list[int], hd: list[int], pd2: list[int]) -> None:
        """D2R's classic DC6 and HD strip, and PD2's DC6, for the sorceress, as palette indices per frame."""
        dc6.write(self.d2r / SPELLS / "soskillicon.dc6", [frame(c) for c in d2r])
        hd_dir = self.d2r / "hd" / SPELLS / "sorceress"
        images.write_sprite(hd_dir / "soskillicon.sprite", strip(hd), 130, len(hd), lowend=False)
        images.write_sprite(hd_dir / "soskillicon.lowend.sprite", strip([c - 100 for c in hd], 65), 65, len(hd),
                            lowend=False)
        dc6.write(self.pd2 / SPELLS / "soskillicon.dc6", [frame(c) for c in pd2])

    def test_keeps_d2r_art_where_pd2_kept_the_frame_and_upscales_the_rest(self):
        self.sheets(d2r=[10, 20], hd=[200, 210], pd2=[10, 30, 40, 50])
        S.apply(self.rule, self.pd2, self.d2r, self.out)
        out = self.out / "hd" / SPELLS / "sorceress"
        for name, size, kept in [("soskillicon.sprite", 130, 200), ("soskillicon.lowend.sprite", 65, 100)]:
            img, fw, frames = images.read_sprite(out / name)
            self.assertEqual((fw, frames, img[0], img[1]), (size, 4, 4 * (size + 2), size))  # D2R's stride: frame + 2
            pixel = lambda x: img[2][(size // 2 * img[0] + x) * 4]  # noqa: E731  middle row, red channel
            self.assertEqual([pixel(k * (size + 2) + size // 2) for k in range(4)], [kept, 30, 40, 50])
        self.assertEqual((self.out / SPELLS / "soskillicon.dc6").read_bytes(),
                         (self.pd2 / SPELLS / "soskillicon.dc6").read_bytes())

    def test_drops_trailing_vanilla_frames_past_d2rs_strip(self):
        # the barbarian's classic sheet has 156 frames, 92 of them vanilla's unused ones D2R's strip never had
        self.sheets(d2r=[10, 20, 30, 40], hd=[200, 210], pd2=[10, 20, 35, 40])
        S.apply(self.rule, self.pd2, self.d2r, self.out)
        _, _, frames = images.read_sprite(self.out / "hd" / SPELLS / "sorceress" / "soskillicon.sprite")
        self.assertEqual(frames, 3)

    def test_rebuilds_the_shared_sheet_keeping_d2rs_hd_frames_past_pd2s(self):
        # legacy mode crashes on a skill button whose IconCel is past D2R's 24 frames (Force Move's 24, #294)
        self.sheets(d2r=[10], hd=[200], pd2=[10])
        dc6.write(self.d2r / SPELLS / "skillicon.dc6", [frame(10), frame(20)])
        hd_dir = self.d2r / "hd" / SPELLS / "submenu"
        images.write_sprite(hd_dir / "skillicon.sprite", strip([200, 210, 220]), 130, 3, lowend=False)
        images.write_sprite(hd_dir / "skillicon.lowend.sprite", strip([100, 110, 120], 65), 65, 3, lowend=False)
        dot = 10, 10, 0, 0, bytes((10, *(70,) * 10, 0x80)) * 10  # PD2's 10 px placeholder frame
        dc6.write(self.pd2 / SPELLS / "skillicon.dc6", [frame(10), dot])
        S.apply(self.rule, self.pd2, self.d2r, self.out)
        self.assertEqual(len(dc6.read(self.out / SPELLS / "skillicon.dc6")), 2)
        img, fw, frames = images.read_sprite(self.out / "hd" / SPELLS / "submenu" / "skillicon.sprite")
        self.assertEqual((fw, frames), (130, 3))
        pixel = lambda x, y: img[2][(y * img[0] + x) * 4]  # noqa: E731  red channel
        self.assertEqual([pixel(k * 132 + 5, 5) for k in range(3)], [200, 70, 220])
        self.assertEqual(pixel(132 + 30, 30), 0)  # the dot keeps its size: 10 classic px -> 27 HD px

    def test_appends_hireable_frames_from_the_built_class_sheet(self):
        # the Ascendant's class skills have no frame on D2R's merc sheet (#271)
        self.sheets(d2r=[10, 20], hd=[200, 210], pd2=[10, 20])
        hd_dir = self.d2r / "hd" / SPELLS / "hireables"
        images.write_sprite(hd_dir / "hrskillicon.sprite", strip([150, 160]), 130, 2, lowend=False)
        images.write_sprite(hd_dir / "hrskillicon.lowend.sprite", strip([50, 60], 65), 65, 2, lowend=False)
        self.rule.write_text("sor\nhireable sor 1\nhireable sor 0\n", "utf-8")
        S.apply(self.rule, self.pd2, self.d2r, self.out)
        for name, size, want in [("hrskillicon.sprite", 130, [150, 160, 210, 200]),
                                 ("hrskillicon.lowend.sprite", 65, [50, 60, 110, 100])]:
            img, fw, frames = images.read_sprite(self.out / "hd" / SPELLS / "hireables" / name)
            self.assertEqual((fw, frames), (size, 4))
            pixel = lambda x: img[2][(size // 2 * img[0] + x) * 4]  # noqa: E731  middle row, red channel
            self.assertEqual([pixel(k * (size + 2) + size // 2) for k in range(4)], want)
        self.rule.write_text("sor\nhireable sor 9\n", "utf-8")
        with self.assertRaisesRegex(S.SkillIconError, "hireable sor 9"):
            S.apply(self.rule, self.pd2, self.d2r, self.out)

    def test_hireable_lines_match_the_hireableiconcel_cells(self):
        root = Path(__file__).resolve().parents[2]
        lines = [x for x in S.read_lines(root / "merge" / "skill-icons.txt") if x.startswith("hireable ")]
        cells = json.loads((root / "merge" / "tables.json").read_text("utf-8"))["skilldesc"]["cells"]
        cels = sorted(int(v["HireableIconCel"]) for v in cells.values())
        self.assertEqual(cels, list(range(29, 29 + len(lines))))  # D2R's merc sheet has 29 frames

    def test_refuses_a_listed_class_with_no_pd2_sheet_or_a_sheet_not_listed(self):
        self.sheets(d2r=[10], hd=[200], pd2=[10])
        self.rule.write_text("sor\nama\n", "utf-8")
        with self.assertRaisesRegex(S.SkillIconError, "ama.*pd2_extract"):
            S.apply(self.rule, self.pd2, self.d2r, self.out)
        self.rule.write_text("# none\n", "utf-8")
        with self.assertRaisesRegex(S.SkillIconError, "not listed: soskillicon.dc6"):
            S.apply(self.rule, self.pd2, self.d2r, self.out)


if __name__ == "__main__":
    unittest.main()
