"""The table-diff gate (pd2_diff) on synthetic tables: no PD2 install, D2R data or modkit needed."""
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_diff  # noqa: E402


def table(*rows: str) -> bytes:
    return "".join("\t".join(c.strip() for c in r.split("|")) + "\r\n" for r in rows).encode("latin-1")


class GateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "docs").mkdir()
        self.unported("")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, side: str, name: str, data: bytes):
        base = {"pd2": "pd2-data/data", "mod": "generated/data", "d2r": "d2r/data"}[side]
        p = self.root / base / "global/excel" / f"{name}.txt"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def unported(self, rows: str):
        (self.root / "docs/unported.md").write_text(
            "# Unported list\n\n| Deviation | Status | Gate | Work item |\n|---|---|---|---|\n" + rows, "utf-8")

    def gate(self, scope="P4"):
        return pd2_diff.gate(self.root, self.root / "d2r/data", scope)

    def test_a_merged_table_equal_to_pd2_passes(self):
        self.write("pd2", "levels", table("Name | Id | MonLvl", "Rogue Encampment | 1 | 1"))
        self.write("mod", "levels", table("Name | Id | MonLvl", "Rogue Encampment | 1 | 1"))
        report = self.gate()
        self.assertTrue(report.ok)
        self.assertEqual(report.open(), [])

    def test_a_changed_cell_is_open_until_the_unported_list_names_it(self):
        self.write("pd2", "levels", table("Name | MonLvl", "Rogue Encampment | 1", "Blood Moor | 2"))
        self.write("mod", "levels", table("Name | MonLvl", "Rogue Encampment | 1", "Blood Moor | 5"))
        self.assertEqual(self.gate().open(), [("levels.Blood Moor.MonLvl", "2", "5")])
        self.unported("| Blood Moor is harder | approximated | `levels.blood moor.monlvl` | P4 |\n")
        report = self.gate()
        self.assertTrue(report.ok)
        self.assertEqual(report.results[0].covered, 1)

    def test_missing_rows_and_columns_are_deviations_but_d2r_only_rows_are_not(self):
        self.write("pd2", "levels", table("Name | *comment | MonLvl | Beta | eol", "Rogue Encampment | x | 1 | 0 | 0",
                                          "Expansion | | | | 0", "Abaddon | | 60 | 1 | 0", "Expansion | | | | 0"))
        self.write("mod", "levels", table("Name | MonLvl | LevelGroup", "Rogue Encampment | 1 | 3",
                                          "Expansion | | ", "Warlock Keep | 50 | 1", "Expansion | 0 |"))
        self.assertEqual(self.gate().open(), [("levels.*.Beta", "", ""), ("levels.Abaddon.*", "", "")])

    def test_an_unmerged_table_is_compared_with_d2rs_and_an_unshipped_one_is_a_deviation(self):
        self.write("pd2", "monstats", table("Id | Level", "fallen | 2"))
        self.write("d2r", "monstats", table("Id | Level", "fallen | 1"))
        self.write("pd2", "arena", table("Suicide | PlayerKill", "-10 | 100"))
        report = self.gate()
        self.assertEqual(report.open(), [("arena.*.*", "", ""), ("monstats.fallen.Level", "2", "1")])
        self.assertEqual([r.source for r in report.results], ["none", "D2R"])

    def test_rows_renamed_by_the_id_lists_and_their_rewritten_references_match(self):
        (self.root / "merge").mkdir()
        (self.root / "merge/ids-skills.tsv").write_text("# PD2 name\tid\tname\nTeeth\t67\tTeeth_pd2\n", "utf-8")
        self.write("pd2", "missiles", table("Missile | Skill | SrvCalc1 | Range", 'teeth | Teeth | skill(\'Teeth\'.blvl)*-2 | "5"'))
        self.write("mod", "missiles", table("Missile | Skill | SrvCalc1 | Range", "teeth | Teeth_pd2 | skill('Teeth_pd2'.blvl)*(0-2) | 5"))
        self.write("pd2", "skills", table("skill | Id | srvmissile", "Teeth | 67 | teeth"))
        self.write("mod", "skills", table("skill | *Id | srvmissile", "Teeth_pd2 | 400 | teeth"))
        self.assertEqual(self.gate("P2").open(), [])
        self.write("mod", "missiles", table("Missile | Skill | SrvCalc1 | Range", "teeth | Teeth | skill('Teeth_pd2'.blvl)*(0-2) | 5"))
        self.assertEqual(self.gate("P2").open(), [("missiles.teeth.Skill", "Teeth", "Teeth")])

    def test_the_scope_picks_the_work_items_tables(self):
        for t in ("skills", "armor", "levels"):
            self.write("pd2", t, table("Name | x", "a | 1"))
        tables = {s: [r.table for r in self.gate(s).results] for s in ("P2", "P3", "P4", "M1", "alpha")}
        self.assertEqual(tables, {"P2": ["skills"], "P3": ["armor"], "P4": ["levels"],
                                  "M1": ["armor", "levels", "skills"], "alpha": ["armor", "levels", "skills"]})
        with self.assertRaisesRegex(pd2_diff.GateError, "P9"):
            self.gate("P9")

    def test_the_command_prints_a_pasteable_report_and_fails_on_open_deviations(self):
        self.write("pd2", "levels", table("Name | MonLvl", "Blood Moor | 2"))
        self.write("mod", "levels", table("Name | MonLvl", "Blood Moor | 5"))
        args = ["--scope", "P4", "--root", str(self.root), "--d2r", str(self.root / "d2r/data")]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(pd2_diff.main(args), 1)
        self.assertIn("| levels | merged | 1 | 0 |", out.getvalue())
        self.assertIn("| `levels.Blood Moor.MonLvl` | 2 | 5 |", out.getvalue())
        self.unported("| Blood Moor is harder | approximated | `levels.*.monlvl` | P4 |\n")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(pd2_diff.main(args), 0)

    def test_every_unported_row_has_a_known_status(self):
        self.unported("| Something | maybe | `levels.*.*` | P4 |\n")
        with self.assertRaisesRegex(pd2_diff.GateError, "maybe"):
            self.gate()


if __name__ == "__main__":
    unittest.main()
