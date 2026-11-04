"""pd2_skilltrees on synthetic tables and sprites (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_skilltrees as T  # noqa: E402
from pd2_ids import Table  # noqa: E402


def table(text: str) -> Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "\t").encode("latin-1"))
        return Table(p)


SKILLS = ("skill|charclass|skilldesc|reqskill1|reqskill2|reqskill3\r\n"
          "Attack||attack|||\r\n"
          "Jab|ama|jab|||\r\n"
          "Impale|ama|impale|Jab||\r\n"
          "Fend|ama|fend|Impale|Bolt|\r\n"  # Bolt is on another page: no arrow
          "Skewer|ama|skewer|Jab||\r\n"  # skips a row: branches off Jab -> Impale
          "Bolt|ama|bolt|||\r\n"
          "Avoid|ama|avoid|Fend||\r\n"  # off the tree: no box, no arrow
          "Blink|war|blink|||\r\n")  # the Warlock keeps D2R's tree
DESC = ("skilldesc|SkillPage|SkillRow|SkillColumn\r\nattack|0|0|0\r\njab|3|1|1\r\nimpale|3|3|1\r\nfend|3|4|2\r\n"
        "bolt|1|1|1\r\navoid|0|0|0\r\nblink|1|1|1\r\nskewer|3|3|2\r\n")


class LayoutTests(unittest.TestCase):
    def test_boxes_and_arrows(self):
        out = T.layout(table(SKILLS), table(DESC), 6, 3)
        self.assertEqual(out["ama"], ([(3, 1, 1), (3, 3, 1), (3, 4, 2), (3, 3, 2), (1, 1, 1)],
                                      [((3, 1, 1), (3, 3, 1)), ((3, 3, 1), (3, 4, 2)), ((3, 1, 1), (3, 3, 2))]))
        self.assertEqual(out["sor"], ([], []))
        self.assertNotIn("war", out)

    def test_refusals(self):
        cases = [
            (SKILLS, DESC.replace("fend|3|4|2", "fend|3|7|2"), "'Fend': page 3 row 7 column 2 is off the 6x3 tree"),
            (SKILLS, DESC.replace("fend|3|4|2", "fend|3|3|1"), "'Fend': another ama skill has page 3 row 3 column 1"),
            (SKILLS.replace("fend|Impale|", "fend|Stab|"), DESC, "'Fend': reqskill1 'Stab' is no skill"),
        ]
        for skills, desc, msg in cases:
            with self.subTest(msg), self.assertRaisesRegex(ValueError, msg):
                T.layout(table(skills), table(desc), 6, 3)


class EndsTests(unittest.TestCase):
    def test_straight_down_from_box_bottom_to_box_top(self):
        self.assertEqual(T.ends((10, 0), (10, 400), 148), ((84.0, 398.0), (0.0, 1.0), 250))

    def test_diagonal_leaves_the_side_for_the_near_top_corner(self):
        tip, (dx, dy), _ = T.ends((0, 0), (300, 200), 100)
        self.assertAlmostEqual(tip[0] + dx * T.gap(100), 300)  # the target's top left corner
        self.assertAlmostEqual(tip[1] + dy * T.gap(100), 200)
        tip, (dx, dy), _ = T.ends((300, 0), (0, 200), 100)
        self.assertAlmostEqual(tip[0] + dx * T.gap(100), 100)  # top right
        self.assertLess(dx, 0)

    def test_diagonal_from_a_tail_on_the_straight_arrow(self):
        tip, (dx, dy), length = T.ends((0, 0), (300, 400), 100, tail=(50, 250))
        self.assertAlmostEqual(tip[0] + dx * T.gap(100), 300)  # still the near top corner
        self.assertAlmostEqual(tip[1] + dy * T.gap(100), 400)
        self.assertAlmostEqual(tip[0] - dx * length, 50, delta=1)  # from the tail
        self.assertAlmostEqual(tip[1] - dy * length, 250, delta=1)


# a tiny tree: 3 frames of 130 x 200, a red tab strip, white boxes with a blue counter box at every slot, a grey
# arrow where ARROW_FROM is
G = {"sprite": "t/{}skilltree.sprite", "rows": [20, 50, 80, 110, 140, 170], "columns": [4, 44, 84], "box": 24,
     "clear_below": 12}
W, H, STRIDE = 390, 200, 130
RED, WHITE, BLUE, GREY = (255, 0, 0, 255), (255, 255, 255, 255), (0, 0, 255, 255), (128, 128, 128, 255)


def d2r_tree() -> T.Image:
    img = T.Image(W, H)

    def fill(x, y, w, h, c):
        for j in range(y, y + h):
            img.px[(j * W + x) * 4:(j * W + x + w) * 4] = bytes(c) * w
    fill(0, 0, W, 10, RED)
    for f in range(3):
        for y in G["rows"]:
            for x in G["columns"]:
                fill(f * STRIDE + x, y, 24, 24, WHITE)
                fill(f * STRIDE + x + 26, y + 14, 6, 8, BLUE)
    page, col, r1, r2 = T.ARROW_FROM
    fill(T.frame(page) * STRIDE + G["columns"][col - 1] + 6, G["rows"][r1 - 1] + 24, 12,
         G["rows"][r2 - 1] - G["rows"][r1 - 1] - 24, GREY)
    return img


def px(img: T.Image, x: int, y: int) -> tuple:
    return tuple(img.px[(y * img.w + x) * 4:(y * img.w + x) * 4 + 4])


class RuleTests(unittest.TestCase):
    def test_rule_redraws_every_class_from_the_merged_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            d2r, out, rule = Path(tmp) / "d2r", Path(tmp) / "out", Path(tmp) / "tree-backgrounds.json"
            (out / "global" / "excel").mkdir(parents=True)
            (out / "global" / "excel" / "skills.txt").write_bytes(SKILLS.replace("|", "\t").encode("latin-1"))
            (out / "global" / "excel" / "skilldesc.txt").write_bytes(DESC.replace("|", "\t").encode("latin-1"))
            (d2r / "t").mkdir(parents=True)
            head = T.HEAD.pack(b"SpA1", 31, 128, W, H, 0, 3, 0, 0, W * H * 4, 4)
            for prefix in T.CLASSES.values():
                (d2r / G["sprite"].format(prefix)).write_bytes(head + d2r_tree().px)
            rule.write_text(json.dumps({"hd": G}), "utf-8")
            T.apply(rule, Path(tmp) / "pd2", d2r, out)
            raw = (out / G["sprite"].format("am")).read_bytes()
            self.assertEqual(raw[:T.HEAD.size], head)
            img = T.Image(W, H, bytearray(raw[T.HEAD.size:]))

            def at(page, row, col, dx=12, dy=12):
                return px(img, T.frame(page) * STRIDE + G["columns"][col - 1] + dx, G["rows"][row - 1] + dy)
            self.assertEqual(px(img, 200, 5), RED)  # the tab strip stays
            self.assertEqual(at(3, 1, 1), WHITE)  # Jab
            self.assertEqual(at(3, 4, 2), WHITE)  # Fend
            self.assertEqual(at(1, 1, 1), WHITE)  # Bolt
            self.assertEqual(at(3, 2, 3)[3], 0)  # D2R's box where PD2 has no skill is gone
            self.assertEqual(at(1, 1, 1, dy=30), (0, 0, 0, 0))  # no arrow from Bolt to Fend
            self.assertEqual(at(3, 2, 1), GREY)  # Jab -> Impale over the empty row
            self.assertGreater(at(3, 2, 1, dx=26, dy=21)[3], 0)  # Jab -> Skewer branches off it in that row
            self.assertEqual(at(3, 3, 1, dx=28, dy=18), BLUE)  # Impale's counter box
            self.assertEqual(at(3, 3, 1, dx=33, dy=20)[3], 0)  # Impale -> Fend keeps clear of it
            self.assertGreater(at(3, 3, 1, dx=38, dy=26)[3], 0)  # and goes on past it
            self.assertEqual(px(img, 200, 150)[3], 0)
            sor = (out / G["sprite"].format("so")).read_bytes()[T.HEAD.size:]
            self.assertEqual(sor[(30 * W + 16) * 4:(30 * W + 16) * 4 + 4], bytes(4))  # no sor skills: no boxes
            self.assertFalse((out / G["sprite"].format("wa")).exists())


if __name__ == "__main__":
    unittest.main()
