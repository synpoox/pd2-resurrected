"""pd2_skilldesc on synthetic skilldesc tables: Grim Ward's lines as https://github.com/synpoox/pd2-resurrected/issues/73 found them."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_skilldesc as S  # noqa: E402
from pd2_ids import Table  # noqa: E402

COLS = [f"{p}{s}{n}" for p, n in [("desc", n) for n in range(1, 6)] + [("dsc3", 1)]
        for s in ("line", "texta", "textb", "calca", "calcb")]
FMT = {"StrSkill18", "StrSkill20", "StrSkill4", "StrSkillDamagePercent", "Damplev"}  # D2R keys with a format code


def table(*rows: tuple[str, list[list[str]]]) -> Table:
    """skilldesc with each row's lines in order (5 desc lines, then dsc3 line 1); [] is an empty line."""
    text = "\t".join(["skilldesc", *COLS]) + "\n"
    for name, ls in rows:
        text += "\t".join([name, *(c for line in ls for c in (line or [""] * 5))]) + "\n"
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "skilldesc.txt"
        p.write_bytes(text.encode("latin-1"))
        return Table(p)


D2R = table(("grim ward", [["36", "StrSkillRadiusSingular", "StrSkill18", "ln12*2", "3"],
                           ["36", "StrSkillDurationSingular", "StrSkill20", "clc1", "25"],
                           ["74", "StrSkillDamagePercent", "", "ln56", ""], [], [], []]),
            ("bash", [["75", "StrSkill4", "", "edmn", "edmx"], [], [], [], [], ["76", "Damplev", "skillname139", "par8", ""]]))


class SkilldescTests(unittest.TestCase):
    def test_pd2_lines_move_onto_d2r_functions(self):
        merged = table(("grim ward", [["1", "", "", "", ""], ["19", "StrSkill18", "", "ln12", ""],
                                      ["12", "StrSkill20", "", "ln34", ""], ["2", "StrSkill4", "StrSkill23", "ln56", ""],
                                      ["6", "StrAttackRating", "", "toht", ""], []]),
                       ("bash", [[], [], [], [], [], ["63", "skillname139", "Damplev", "par8", ""]]))
        self.assertEqual(len(S.clashes(merged, D2R, FMT)), 5)  # all but Attack Rating, which D2R prints fine
        S.rewrite(merged, D2R, FMT)
        self.assertEqual(S.clashes(merged, D2R, FMT), [])
        self.assertEqual(merged.rows[0][1:26], ["36", "StrSkill3", "", "usmc", "256",  # Mana Cost, not "an evil force"
                                                "36", "StrSkillRadiusSingular", "StrSkill18", "(ln12)*2", "3",
                                                "36", "StrSkillDurationSingular", "StrSkill20", "ln34", "25",
                                                "74", "StrSkillDamagePercent", "", "ln56", "",
                                                "6", "StrAttackRating", "", "toht", ""])
        self.assertEqual(merged.rows[1][26:], ["76", "Damplev", "skillname139", "par8", ""])

    def test_a_skill_pd2_keeps_off_the_tree_leaves_it(self):
        # PD2's client hides SkillRow 0; D2R draws it in the page's first cell (Blade Dance, the Barbarian's masteries)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "skilldesc.txt"
            p.write_bytes(b"skilldesc\tSkillPage\tSkillRow\tSkillColumn\nBlade Dance\t3\t0\t0\ntiger strike\t3\t1\t2\n"
                          b"asatemp2\t0\t0\t0\n")
            t = Table(p)
        S.hide_off_tree(t)
        self.assertEqual([r[1:] for r in t.rows], [["0", "0", "0"], ["3", "1", "2"], ["0", "0", "0"]])

    def test_builtin_damage_takes_d2rs_line(self):
        d2r = table(("holy bolt", [["75", "StrSkill39", "", "enma", "exma"], [], [], [], [], []]),
                    ("fire bolt", [["75", "StrSkill5", "", "enma", "exma"], [], [], [], [], []]))
        merged = table(("holy bolt", [["10", "", "", "", ""], [], [], [], [], []]),  # printed "Magic Damage: %d-%d3-5"
                       ("new bolt", [["10", "", "", "", ""], [], [], [], [], []]))
        self.assertEqual(len(S.clashes(merged, d2r, FMT)), 2)
        S.rewrite(merged, d2r, FMT, {"holy bolt": "fire", "new bolt": "fire"})  # the skill's own D2R line beats EType
        self.assertEqual([r[1:6] for r in merged.rows], [["75", "StrSkill39", "", "enma", "exma"],
                                                         ["75", "StrSkill5", "", "enma", "exma"]])

    def test_poison_damage_gets_d2rs_length_line_below_it(self):
        d2r = table(("poison nova", [["36", "StrSkillPoisonLengthSingular", "StrSkill63", "edln", "25"],
                                     ["75", "StrSkill8", "", "enms", "exms"], [], [], [], []]))
        merged = table(("poison nova", [["1", "", "", "", ""], ["14", "", "", "", ""], ["12", "StrSkill20", "", "ln34", ""],
                                        [], [], []]))
        S.rewrite(merged, d2r, FMT | {"StrSkillPoisonLengthSingular", "StrSkill8"})
        self.assertEqual(merged.rows[0][6:21], ["36", "StrSkillPoisonLengthSingular", "StrSkill63", "edln", "25",
                                                 "75", "StrSkill8", "", "enms", "exms",
                                                 "12", "StrSkill20", "", "ln34", ""])  # moved up a slot

    def test_a_line_with_no_d2r_layout_fails(self):
        merged = table(("new skill", [["47", "StrSkill20", "", "a", "b"], [], [], [], [], []]))  # D2R: never under 75
        with self.assertRaisesRegex(ValueError, "new skill descline1 47 StrSkill20"):
            S.rewrite(merged, D2R, FMT)

    def test_format_codes_d2r_never_uses_in_skilldesc(self):
        # PD2's 51 formats "+%d Percent ..."; D2R's classic 51 prints it raw. ItemStats1o is D2R's, unused in skilldesc
        d2r = table(("holy shield", [["75", "StrSkillSmiteDamage", "", "pnma", "pxma"], [], [], [], [], []]),
                    ("x", [["74", "StrSkillDamagePercent", "", "a", ""], [], [], [], [], []]))
        merged = table(("jav mastery", [["51", "deadlystrikemastery", "", "edmn", ""], [], [], [], [], []]),
                       ("holy shield", [["49", "ItemStats1o", "", "", ""], ["7", "StrWeaponDmg", "", "75", ""],
                                        [], [], [], []]))
        texts = {"deadlystrikemastery": "+%d Percent Critical Strike Multiplier", "ItemStats1o": "Smite Damage: %d to %d",
                 "StrWeaponDmg": "% Weapon Damage"}  # a literal % under a classic function prints fine
        self.assertEqual(len(S.clashes(merged, d2r, FMT, texts)), 2)
        S.rewrite(merged, d2r, FMT, strings=texts)
        self.assertEqual(S.clashes(merged, d2r, FMT, texts), [])
        self.assertEqual([r[1:11] for r in merged.rows],
                         [["74", "deadlystrikemastery", "", "edmn", "", *[""] * 5],
                          ["75", "StrSkillSmiteDamage", "", "pnma", "pxma", "7", "StrWeaponDmg", "", "75", ""]])


if __name__ == "__main__":
    unittest.main()
