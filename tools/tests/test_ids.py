"""pd2_ids on synthetic tables (no PD2 or D2R data)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_ids as I  # noqa: E402


def table(text: str) -> I.Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "\t").encode("latin-1"))
        return I.Table(p)


class SeedTests(unittest.TestCase):
    def test_vanilla_by_id_then_name_then_append(self):
        I.VANILLA["test"] = 2
        try:
            # 0: same; 1: PD2 rework of a vanilla row (and its old name re-added later); 2+: added after vanilla
            pd2 = ["Attack", "Cold Enchant", "MonLeap", "New Skill", "Frozen Armor"]
            d2r = ["Attack", "Frozen Armor", "Interact", "MonLeap", "Cold Enchant"]
            self.assertEqual(I.seed("test", pd2, d2r, []), [
                ("Attack", 0, "Attack"), ("Cold Enchant", 1, "Frozen Armor"), ("MonLeap", 3, "MonLeap"),
                ("New Skill", 5, "New Skill"), ("Frozen Armor", 6, "Frozen Armor_pd2")])
        finally:
            del I.VANILLA["test"]

    def test_only_appends_for_new_pd2_rows(self):
        existing = [("A", 0, "A"), ("B", 2, "B")]
        self.assertEqual(I.seed("itemstatcost", ["A", "B", "C"], ["A", "X"], existing), [("C", 3, "C")])


class CheckTests(unittest.TestCase):
    def test_accepts_a_good_list(self):
        I.check("t", [("A", 0, "A"), ("B", 2, "B")], ["A", "B"], ["A", "X"])

    def test_refusals(self):
        cases = [
            ([("A", 0, "A")], ["A", "B"], "no id"),
            ([("A", 0, "A"), ("B", 3, "B")], ["A", "B"], "without gaps"),
            ([("A", 0, "Q"), ("B", 2, "B")], ["A", "B"], "D2R's 'A'"),
            ([("A", 0, "A"), ("B", 2, "X")], ["A", "B"], "used twice"),
            ([("A", 0, "A"), ("Z", 2, "Z")], ["A"], "not a PD2 row"),
        ]
        for lines, pd2, msg in cases:
            with self.subTest(msg), self.assertRaisesRegex(I.IdError, msg):
                I.check("t", lines, pd2, ["A", "X"])


class MergeTableTests(unittest.TestCase):
    def test_overlay_refs_formulas_and_appends(self):
        d2r = table("skill|*Id|reqskill1|calc1|localdelay|srvstopfunc\r\n"
                    "Attack|0||1|0|\r\nFrozen Armor|1||2|0|7\r\n")
        pd2 = table("skill|Id|reqskill1|calc1|delay|general\r\n"
                    "Attack|0||5|0|x\r\nCold Enchant|1|Attack|skill('New Skill'.blvl)|3|\r\n"
                    "New Skill|2|Cold Enchant|skill('Cold Enchant'.lvl)|0|\r\n")
        lines = [("Attack", 0, "Attack"), ("Cold Enchant", 1, "Frozen Armor"), ("New Skill", 2, "New Skill")]
        merged, dropped = I.merge_table("skills", pd2, d2r, lines, I.renames({"skills": lines, "missiles": [],
                                                                              "states": [], "itemstatcost": []}))
        self.assertEqual(dropped, ["general"])
        self.assertEqual(merged.rows, [
            ["Attack", "0", "", "5", "0", ""],
            ["Frozen Armor", "1", "Attack", "skill('New Skill'.blvl)", "3", "7"],  # D2R-only srvstopfunc kept
            ["New Skill", "2", "Frozen Armor", "skill('Frozen Armor'.lvl)", "0", ""],
        ])
        self.assertTrue(merged.text().endswith("\r\n"))


class FormulaAndColumnTests(unittest.TestCase):
    def test_fix_formula(self):
        for src, want in [("-min(par5 + lvl,60)", "0-min(par5 + lvl,60)"), ("blvl * -50", "blvl * (0-50)"),
                          ("-1 * par5", "(0-1) * par5"), ("-5", "-5"), ("skill('X'.blvl)-3", "skill('X'.blvl)-3"),
                          ("min((par5 + lvl -1), par6)", "min((par5 + lvl -1), par6)"), ("(0 - 2) * -2", "(0 - 2) * (0-2)")]:
            self.assertEqual(I.fix_formula(src), want)

    def test_quotes_dropped_rightskill_derived(self):
        d2r = table("skill|calc1|passive|rightskill\nA|1|0|1\n")
        pd2 = table('skill|calc1|passive\nA|-"min(lvl,5)"|1\nB|"max(1, 2)"|0\n')
        lines = [("A", 0, "A"), ("B", 1, "B")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["A", "0-min(lvl,5)", "1", "0"], ["B", "max(1, 2)", "0", "1"]])

    def test_name_length(self):
        with self.assertRaisesRegex(I.IdError, "31 bytes"):
            I.check("missiles", [("x" * 32, 0, "x" * 32)], ["x" * 32], [])
        self.assertEqual(I.seed("missiles", ["y" * 40], [], []), [("y" * 40, 0, "y" * 31)])


class ListFileTests(unittest.TestCase):
    def test_seed_lists_appends_and_read_list_round_trips(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for side, names in (("pd2", ["A", "B"]), ("d2r", ["A"])):
                for t in I.TABLES:
                    p = root / side / "global" / "excel" / f"{t}.txt"
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_text("name\n" + "".join(f"{n}\n" for n in names), "latin-1")
            (root / "merge").mkdir()
            self.assertEqual(I.seed_lists(root / "merge", root / "pd2", root / "d2r"), {t: 2 for t in I.TABLES})
            self.assertEqual(I.seed_lists(root / "merge", root / "pd2", root / "d2r"), {t: 0 for t in I.TABLES})
            self.assertEqual(I.read_list(root / "merge" / "ids-skills.tsv"), [("A", 0, "A"), ("B", 1, "B")])
            out = root / "out"
            I.apply(root / "merge" / "ids-skills.tsv", root / "pd2", root / "d2r", out)
            self.assertEqual((out / "global/excel/skills.txt").read_text("latin-1"), "name\nA\nB\n")


if __name__ == "__main__":
    unittest.main()
