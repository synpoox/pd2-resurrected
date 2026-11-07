"""The table-diff gate (pd2_diff) on synthetic tables: no PD2 install, D2R data or modkit needed."""
import contextlib
import io
import subprocess
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
        (self.root / "d2r/data/global/excel").mkdir(parents=True)
        self.deviations("")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, side: str, name: str, data: bytes):
        base = {"pd2": "pd2-data/data", "mod": "generated/data", "d2r": "d2r/data"}[side]
        p = self.root / base / "global/excel" / f"{name}.txt"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def deviations(self, rows: str):
        (self.root / "docs/deviations.md").write_text(
            "# Deviation list\n\n| Deviation | Status | Gate | Work item |\n|---|---|---|---|\n" + rows, "utf-8")

    def gate(self, scope="P4"):
        return pd2_diff.gate(self.root, self.root / "d2r/data", scope)

    def test_a_missing_d2r_folder_is_an_error_not_unshipped_tables(self):
        with self.assertRaisesRegex(pd2_diff.GateError, "no D2R tables"):
            pd2_diff.gate(self.root, self.root / "nowhere", "P4")

    def test_no_pd2_tables_is_an_error_not_a_pass(self):
        with self.assertRaisesRegex(pd2_diff.GateError, r"no PD2 tables under .*pd2-data.*pd2_extract.*--root"):
            self.gate()

    def test_a_merged_table_equal_to_pd2_passes(self):
        self.write("pd2", "lvlmaze", table("Name | Id | MonLvl", "Rogue Encampment | 1 | 1"))
        self.write("mod", "lvlmaze", table("Name | Id | MonLvl", "Rogue Encampment | 1 | 1"))
        report = self.gate()
        self.assertTrue(report.ok)
        self.assertEqual(report.open(), [])

    def test_a_value_the_merge_fixes_is_not_a_deviation(self):
        for t, h in (("skills", "skill"), ("states", "state"), ("properties", "code | func1 | stat1")):
            for side in ("pd2", "mod"):
                self.write(side, t, table(h))
        self.write("pd2", "gems", table("name | weaponMod2Code", "Um Rune | Deep-Wounds"))
        self.write("mod", "gems", table("name | weaponMod2Code", "Um Rune | deep-wounds"))
        self.assertEqual(self.gate("P3").open(), [])

    def test_a_changed_cell_is_open_until_the_deviation_list_names_it(self):
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Rogue Encampment | 1", "Blood Moor | 2"))
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Rogue Encampment | 1", "Blood Moor | 5"))
        self.assertEqual(self.gate().open(), [("lvlmaze.Blood Moor.MonLvl", "2", "5")])
        self.deviations("| Blood Moor is harder | approximated | `lvlmaze.blood moor.monlvl` | P4 |\n")
        report = self.gate()
        self.assertTrue(report.ok)
        self.assertEqual(report.results[0].covered, 1)

    def test_the_report_counts_deviation_entries_by_status(self):
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Blood Moor | 5"))
        self.deviations("| Blood Moor is harder | approximated | `lvlmaze.*.monlvl` | P4 |\n"
                      "| Maps | not yet | | P3 |\n| Ladder | not yet | | P6 |\n")
        text = pd2_diff.report(self.gate(), "P4")
        self.assertIn("1 table cells on the deviation list", text)
        self.assertIn("Deviation list: 2 not yet, 1 approximated, 0 cut entries", text)

    def test_the_report_names_its_commit_and_marks_uncommitted_changes(self):
        git = ["git", "-C", str(self.root), "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
        subprocess.run([*git, "init", "-q"], check=True)
        subprocess.run([*git, "add", "docs"], check=True)
        subprocess.run([*git, "commit", "-qm", "x"], check=True)
        head = subprocess.run([*git, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
        self.assertEqual(pd2_diff.commit_of(self.root), head)
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.assertEqual(pd2_diff.commit_of(self.root), f"{head}, uncommitted changes")
        first = pd2_diff.report(self.gate(), "P4", pd2_diff.commit_of(self.root)).splitlines()[0]
        self.assertTrue(first.startswith(f"Table-diff gate on {head}, uncommitted changes, scope P4: FAIL, 1 open"), first)

    def test_missing_rows_and_columns_are_deviations_but_d2r_only_rows_are_not(self):
        self.write("pd2", "lvlmaze", table("Name | *comment | MonLvl | Beta | eol", "Rogue Encampment | x | 1 | 0 | 0",
                                          "Expansion | | | | 0", "Abaddon | | 60 | 1 | 0", "Expansion | | | | 0"))
        self.write("mod", "lvlmaze", table("Name | MonLvl | LevelGroup", "Rogue Encampment | 1 | 3",
                                          "Expansion | | ", "Warlock Keep | 50 | 1", "Expansion | 0 |"))
        self.assertEqual(self.gate().open(), [("lvlmaze.*.Beta", "", ""), ("lvlmaze.Abaddon.*", "", "")])

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
        self.write("pd2", "properties", table("code | func1"))  # missiles' hit parameters read the id context
        self.write("pd2", "missiles", table("Missile | Skill | SrvCalc1 | Range", 'teeth | Teeth | skill(\'Teeth\'.blvl)*-2 | "5"'))
        self.write("mod", "missiles", table("Missile | Skill | SrvCalc1 | Range", "teeth | Teeth_pd2 | skill('Teeth_pd2'.blvl)*(0-2) | 5"))
        self.write("pd2", "skills", table("skill | Id | srvmissile", "Teeth | 67 | teeth"))
        self.write("mod", "skills", table("skill | *Id | srvmissile", "Teeth_pd2 | 400 | teeth"))
        self.assertEqual(self.gate("P2").open(), [])
        self.write("mod", "missiles", table("Missile | Skill | SrvCalc1 | Range", "teeth | Teeth | skill('Teeth_pd2'.blvl)*(0-2) | 5"))
        self.assertEqual(self.gate("P2").open(), [("missiles.teeth.Skill", "Teeth", "Teeth")])

    def test_a_missile_do_function_moved_to_the_missile_column_matches(self):
        self.write("pd2", "skills", table("skill | Id | srvdofunc | srvmissile | srvmissilea", "Bolt | 20 | 188 | | jav"))
        self.write("mod", "skills", table("skill | *Id | srvdofunc | srvmissile | srvmissilea", "Bolt | 20 | | jav | "))
        self.assertEqual(self.gate("P2").open(), [])

    def test_tables_merged_by_position_compare_by_position_and_spell_references(self):
        (self.root / "merge").mkdir()
        (self.root / "merge/ids-skills.tsv").write_text("Teeth\t67\tTeeth_pd2\n", "utf-8")
        (self.root / "merge/tables.json").write_text('{"shrines": {"rows": "position"}, "monstats": {}}', "utf-8")
        self.write("pd2", "shrines", table("Shrine Type | Code", "None | 0", "Recharge | 1"))
        self.write("mod", "shrines", table("Name | Code", "None | 0", "Refilling Shrine | 2"))
        self.write("pd2", "monstats", table("Id | Skill1", "fallen | Teeth"))
        self.write("mod", "monstats", table("Id | Skill1", "fallen | Teeth_pd2"))
        self.assertEqual(self.gate().open(), [("shrines.*.Shrine Type", "", ""), ("shrines.1.Code", "1", "2")])

    def test_the_scope_picks_the_work_items_tables(self):
        for t in ("skills", "armor", "lvlmaze"):
            self.write("pd2", t, table("Name | x", "a | 1"))
        tables = {s: [r.table for r in self.gate(s).results] for s in ("P2", "P3", "P4", "M1", "alpha")}
        self.assertEqual(tables, {"P2": ["skills"], "P3": ["armor"], "P4": ["lvlmaze"],
                                  "M1": ["armor", "lvlmaze", "skills"], "alpha": ["armor", "lvlmaze", "skills"]})
        with self.assertRaisesRegex(pd2_diff.GateError, "P9"):
            self.gate("P9")

    def test_the_command_prints_a_pasteable_report_and_fails_on_open_deviations(self):
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Blood Moor | 5"))
        args = ["--scope", "P4", "--root", str(self.root), "--d2r", str(self.root / "d2r/data")]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(pd2_diff.main(args), 1)
        self.assertIn("| lvlmaze | merged | 1 | 0 |", out.getvalue())
        self.assertIn("| `lvlmaze.Blood Moor.MonLvl` | 2 | 5 |", out.getvalue())
        self.deviations("| Blood Moor is harder | approximated | `lvlmaze.*.monlvl` | P4 |\n")
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(pd2_diff.main(args), 0)

    def test_a_whole_table_row_must_go_once_the_table_is_merged(self):
        self.deviations("| Not merged yet | not yet | `lvlmaze.*` | P4 |\n")
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.write("d2r", "lvlmaze", table("Name | MonLvl", "Blood Moor | 1"))
        self.assertEqual(self.gate().results[0].covered, 1)
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Blood Moor | 1"))
        with self.assertRaisesRegex(pd2_diff.GateError, "lvlmaze.*merged"):
            self.gate()

    def test_excels_quote_marks_are_not_data(self):
        self.write("pd2", "lvlmaze", table("Name | MonLvl", 'Blood Moor | "2"'))
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.assertEqual(self.gate().open(), [])

    def test_item_tables_are_read_as_the_merge_writes_them(self):
        (self.root / "merge").mkdir()
        # repeated affix names: matched by the id list, not by name (D2R's own Virulent sits at 0)
        (self.root / "merge/ids-magicprefix.tsv").write_text("Virulent\t1\tVirulent\nVirulent#2\t2\tVirulent\n", "utf-8")
        for t, h in (("skills", "skill"), ("states", "state"), ("properties", "code | func1 | stat1")):
            for side in ("pd2", "mod"):
                self.write(side, t, table(h))
        self.write("pd2", "magicprefix", table("Name | level", "Virulent | 5", "Virulent | 9"))
        self.write("mod", "magicprefix", table("Name | level", "Virulent | 92", "Virulent | 5", "Virulent | 9"))
        # renamed columns (pd2_ids.COLUMN_MAP), and a repeated column whose first holds the values
        self.write("pd2", "armor", table("name | code | DrehyaMax | mindam | mindam", "Buckler | buc | 3 | 1 | 0"))
        self.write("mod", "armor", table("name | code | AnyaMax | mindam", "Buckler | buc | 3 | 1"))
        # a table merged by row keeps D2R's names; Excel's quote marks are not part of a cube recipe's name
        self.write("pd2", "gems", table("name | code", "Saphire | gsb"))
        self.write("mod", "gems", table("name | code", "Sapphire | gsb"))
        self.write("pd2", "cubemain", table("description | output", '"rings, three" | amu'))
        self.write("mod", "cubemain", table("description | output", "rings, three | amu"))
        self.assertEqual(self.gate("P3").open(), [])

    def test_item_property_skill_parameters_follow_the_skill_list(self):
        (self.root / "merge").mkdir()
        (self.root / "merge/ids-skills.tsv").write_text("Attack\t0\tAttack\nSplash\t430\tSplash\n", "utf-8")
        self.write("pd2", "skills", table("skill | Id", "Attack | 0", "Splash | 358"))
        self.write("pd2", "states", table("state | Id"))
        for side in ("pd2", "mod"):
            self.write(side, "properties", table("code | func1 | stat1", "oskill | 11 | item_nonclassskill"))
        self.write("pd2", "gems", table("name | weaponMod1Code | weaponMod1Param", "Ruby | oskill | 1"))
        self.write("mod", "gems", table("name | weaponMod1Code | weaponMod1Param", "Ruby | oskill | 430"))
        self.assertEqual(self.gate("P3").open(), [])

    def test_a_maps_level_follows_the_level_list(self):
        # PD2's map spell 12 is the mod's 16 (FUNCS) and its len the mod's level id; a potion's len is a duration
        (self.root / "merge").mkdir()
        (self.root / "merge/ids-levels.tsv").write_text("Null\t0\tNull\nRuins\t5\tRuins\n", "utf-8")
        self.write("pd2", "properties", table("code | func1"))
        self.write("pd2", "levels", table("Name | Id", "Null | 0", "Ruins | 1"))
        self.write("pd2", "misc", table("name | code | pSpell | len", "Map | t11 | 12 | 1", "Potion | hp1 | 3 | 1"))
        self.write("mod", "misc", table("name | code | pSpell | len", "Map | t11 | 16 | 5", "Potion | hp1 | 3 | 1"))
        self.assertEqual([o for o in self.gate("P3").open() if o[0].startswith("misc.")], [])

    def test_missile_hit_parameters_follow_the_state_list(self):
        (self.root / "merge").mkdir()
        (self.root / "merge/ids-states.tsv").write_text("none\t0\tnone\npoison_strike\t282\tpoison_strike\n", "utf-8")
        self.write("pd2", "properties", table("code | func1"))
        self.write("pd2", "states", table("state | Id", "none | 0", "poison_strike | 236"))
        self.write("pd2", "missiles", table("Missile | pSrvHitFunc | sHitPar1", "cloud | 61 | 1"))
        self.write("mod", "missiles", table("Missile | pSrvHitFunc | sHitPar1", "cloud | 61 | 282"))
        self.assertEqual([o for o in self.gate("P2").open() if o[0].startswith("missiles.")], [])

    def test_every_deviation_row_has_a_known_status(self):
        self.deviations("| Something | maybe | `lvlmaze.*.*` | P4 |\n")
        with self.assertRaisesRegex(pd2_diff.GateError, "maybe"):
            self.gate()

    def test_a_separate_gate_table_covers_its_rows_and_must_name_a_listed_row(self):
        self.write("pd2", "lvlmaze", table("Name | MonLvl", "Blood Moor | 2"))
        self.write("mod", "lvlmaze", table("Name | MonLvl", "Blood Moor | 5"))
        status = "# Deviation list\n\n| Deviation | Status | Why | Work item |\n|---|---|---|---|\n" \
                 "| Blood Moor is harder | approximated | why | P4 |\n\n| Deviation | Gate |\n|---|---|\n"
        (self.root / "docs/deviations.md").write_text(status + "| Blood Moor is harder | `lvlmaze.*.monlvl` |\n", "utf-8")
        report = self.gate()
        self.assertTrue(report.ok)
        self.assertIn("1 approximated", pd2_diff.report(report, "P4"))
        (self.root / "docs/deviations.md").write_text(status + "| Blood Moor is easier | `lvlmaze.*.monlvl` |\n", "utf-8")
        with self.assertRaisesRegex(pd2_diff.GateError, "Blood Moor is easier"):
            self.gate()


if __name__ == "__main__":
    unittest.main()
