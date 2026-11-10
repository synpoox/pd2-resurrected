"""presets/lint: the stand-in line checks on synthetic DT1 and DS1 (no PD2 or D2R data)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets import lint  # noqa: E402
from presets_fixtures import A, B, ds1, dt1  # noqa: E402

PD2 = "pd2assets/x.dt1"
STAND = "act1/a/wall.dt1"


class LintTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = Path(self.tmp.name)
        self.d2r, self.out = t / "d2r", t / "out"
        for d in (self.d2r / "act1/a", self.out / "pd2assets"):
            d.mkdir(parents=True)

    def tearDown(self):
        self.tmp.cleanup()

    def strict(self):
        return P.Tiles(self.d2r, strict=True), P.Tiles(self.out, strict=True)

    def line(self, pd2_tiles, stand_tiles=(A[1:], B[1:], (1, 8, 8), (1, 9, 9)), reason=""):
        (self.d2r / STAND).write_bytes(dt1(list(stand_tiles)))
        (self.out / PD2).write_bytes(dt1(list(pd2_tiles)))
        return lint.check_line(PD2, STAND, *self.strict(), reason)

    def test_a_line_holding_a_quarter_or_more_passes(self):
        ln = self.line([A[1:], (1, 5, 5), (1, 6, 6), (1, 7, 7)])  # 1 of 4
        self.assertEqual((ln.matched, ln.own, ln.problems), (1, 4, []))

    def test_a_line_under_a_quarter_fails_unless_it_has_a_reason(self):
        pd2 = [A[1:], (1, 5, 5), (1, 6, 6), (1, 7, 7), (1, 10, 10)]  # 1 of 5
        ln = self.line(pd2)
        self.assertEqual(len(ln.problems), 1)
        self.assertIn("under a quarter", ln.problems[0])
        self.assertEqual(self.line(pd2, reason="the only cliff D2R has").problems, [])

    def test_the_same_index_with_other_flags_does_not_match(self):
        (self.d2r / STAND).write_bytes(dt1([A[1:]], blocked={A[1:]}))
        (self.out / PD2).write_bytes(dt1([A[1:]]))
        ln = lint.check_line(PD2, STAND, *self.strict())
        self.assertEqual(ln.matched, 0)

    def test_a_line_renaming_no_tile_fails_even_with_a_reason(self):
        ln = self.line([(1, 5, 5), (1, 6, 6)], reason="no better one")
        self.assertEqual(len(ln.problems), 1)
        self.assertIn("renames no tile", ln.problems[0])

    def test_copies_are_left_out_of_the_matched_share(self):
        # 1 of 2 tiles is a copy, the other (tile A) matches: share 1 of the 1 the flags decide
        (self.d2r / STAND).write_bytes(dt1([A[1:]]))
        (self.out / PD2).write_bytes(dt1([A[1:], (1, 5, 5)]))
        ln = lint.check_line(PD2, STAND, *self.strict(), "", {(PD2, 1, 5, 5): ("elsewhere.dt1", 1, 5, 5)})
        self.assertEqual((ln.matched, ln.copied, ln.problems), (1, 1, []))

    def test_a_missing_stand_in_dt1_fails(self):
        (self.out / PD2).write_bytes(dt1([A[1:]]))
        ln = lint.check_line(PD2, "act1/a/gone.dt1", *self.strict())
        self.assertEqual(len(ln.problems), 1)
        self.assertIn("no DT1", ln.problems[0])

    def test_strict_tiles_raise_on_a_missing_file_and_lenient_ones_read_it_empty(self):
        with self.assertRaises(FileNotFoundError):
            P.Tiles(self.d2r, strict=True)["act1/a/gone.dt1"]
        self.assertEqual(P.Tiles(self.d2r)["act1/a/gone.dt1"], {})

    def test_reasons_are_the_third_column_of_a_stand_in_line(self):
        rule = self.d2r / "r.tsv"
        rule.write_text("# c\nSet\tb.json\npd2assets/A.dt1\tx.dt1\t# only cliff\npd2assets/b.dt1\tx.dt1\t#\n"
                        "pd2assets/c.dt1\t-\n", "utf-8")
        self.assertEqual(lint.reasons(rule), {"pd2assets/a.dt1": "only cliff"})
        _, standins, _ = P.read_rule(rule)  # the third column does not break the rule's reader
        self.assertEqual(standins["pd2assets/a.dt1"], "x.dt1")

    def renamed(self, ln, copies_of=lambda pd2, standin: {}):
        # two maps load PD2: m1 draws tile A twice and a tile D2R lacks, m2 only the unmatched tile
        (self.out / "m1.ds1").write_bytes(ds1(3, 1, [{(0, 0): A[1:], (1, 0): A[1:], (2, 0): (1, 7, 7)}], [{}]))
        (self.out / "m2.ds1").write_bytes(ds1(1, 1, [{(0, 0): (1, 7, 7)}], [{}]))

        def layout_of(name, dt1s, copies):
            return P.layout((self.out / name).read_bytes(), dt1s, copies)
        lint.renamed([ln], P.Tiles(self.d2r), P.Tiles(self.out), [("M", "S", [PD2], ["m1.ds1", "m2.ds1"])],
                     layout_of, copies_of)

    def test_the_report_counts_tiles_that_differ_from_a_dash_per_map(self):
        ln = self.line([A[1:], (1, 7, 7)])
        self.renamed(ln)
        self.assertEqual((ln.maps, ln.problems), ({"m1.ds1": 2, "m2.ds1": 0}, []))
        self.assertIn("m1.ds1: 2 tiles differ", lint.report([ln]))

    def test_a_line_whose_output_equals_a_dash_fails_unless_it_has_a_reason(self):
        # the maps draw only tile (1,7,7), which D2R lacks: the line changes nothing there
        (self.out / "m1.ds1").write_bytes(ds1(1, 1, [{(0, 0): (1, 7, 7)}], [{}]))
        (self.out / "m2.ds1").write_bytes(ds1(1, 1, [{(0, 0): (1, 7, 7)}], [{}]))
        for reason, problems in (("", 1), ("copies go to its own cliff", 0)):
            ln = self.line([A[1:], (1, 7, 7)], reason=reason)
            lint.renamed([ln], P.Tiles(self.d2r), P.Tiles(self.out), [("M", "S", [PD2], ["m1.ds1"])],
                         lambda n, d, c: P.layout((self.out / n).read_bytes(), d, c), lambda p, s: {})
            self.assertEqual((ln.maps, len(ln.problems)), ({"m1.ds1": 0}, problems))
            if problems:
                self.assertIn("equals `-`", ln.problems[0])

    def test_copies_count_on_both_sides_and_a_line_that_moves_a_copys_home_is_not_a_dash(self):
        # tile A is a pixel copy of the D2R tile at STAND: both builds name it that way, so the line changes nothing
        # (a no-op); with another home under the line, the output differs
        ln = self.line([A[1:]])
        same = {(PD2, *A[1:]): (STAND, *A[1:])}
        (self.out / "m1.ds1").write_bytes(ds1(1, 1, [{(0, 0): A[1:]}], [{}]))
        for with_line, differs in ((same, 0), ({(PD2, *A[1:]): ("act1/a/other.dt1", *A[1:])}, 1)):
            ln = self.line([A[1:]])
            lint.renamed([ln], P.Tiles(self.d2r), P.Tiles(self.out), [("M", "S", [PD2], ["m1.ds1"])],
                         lambda n, d, c: P.layout((self.out / n).read_bytes(), d, c),
                         lambda p, s, w=with_line: w if s else same)
            self.assertEqual(ln.maps, {"m1.ds1": differs}, with_line)

    def test_a_known_no_op_is_reported_not_failed(self):
        ln = self.line([A[1:]])
        (self.out / "m1.ds1").write_bytes(ds1(1, 1, [{(0, 0): (1, 7, 7)}], [{}]))
        old = lint.KNOWN_NOOPS
        lint.KNOWN_NOOPS = frozenset({PD2})
        try:
            lint.renamed([ln], P.Tiles(self.d2r), P.Tiles(self.out), [("M", "S", [PD2], ["m1.ds1"])],
                         lambda n, d, c: P.layout((self.out / n).read_bytes(), d, c), lambda p, s: {})
        finally:
            lint.KNOWN_NOOPS = old
        self.assertEqual((ln.problems, len(ln.known)), ([], 1))

    def test_lint_skips_dash_lines(self):
        ln = self.line([A[1:]])
        out = lint.lint({PD2: STAND, "pd2assets/n.dt1": None}, {}, *self.strict())
        self.assertEqual([x.pd2 for x in out], [PD2])
        self.assertEqual(ln.problems, [])


if __name__ == "__main__":
    unittest.main()
