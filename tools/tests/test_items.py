"""pd2_items on synthetic tables (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_ids as I  # noqa: E402
import pd2_items as M  # noqa: E402
from test_ids import table  # noqa: E402

NO_CTX = I.Context({t: [] for t in I.TABLES}, I.renames({t: [] for t in I.TABLES}), {}, {})


class KeyedTests(unittest.TestCase):
    def test_pd2_rows_onto_d2r_rows_by_key_d2r_only_rows_kept(self):
        d2r = table("name|code|type|spawnable\nAxe|axe|axe|1\nExpansion|||\nWarlock Tome|wt1|warl|1\n", "code")
        pd2 = table("name|code|type|spawnable\nBig Axe|axe|axe|1\nExpansion|||\nMap|t11|t1m|1\n", "code")
        self.assertEqual(M.merge_keyed("misc", pd2, d2r, NO_CTX).rows, [
            ["Big Axe", "axe", "axe", "1"], ["Expansion", "", "", ""], ["Warlock Tome", "wt1", "warl", "1"],
            ["Map", "t11", "t1m", "1"]])

    def test_treasure_classes_in_pd2_order_then_d2r_only_ones(self):
        # a treasure class must come after the ones it names, so PD2's own order is kept
        d2r = table("Treasure Class|Item1\nAct 1|Gold\nTerrorize|Act 1\n")
        pd2 = table("Treasure Class|Item1\nAmmo|aqv\nAct 1|Ammo\n")
        self.assertEqual(M.merge_keyed("treasureclassex", pd2, d2r, NO_CTX).rows,
                         [["Ammo", "aqv"], ["Act 1", "Ammo"], ["Terrorize", "Act 1"]])

    def test_by_row_table_whose_key_is_a_value_takes_pd2s(self):
        k = I.KEY.get("qualityitems")
        d2r = table("mod1code|mod1min\natt|5\n", k)
        pd2 = table("nummods|mod1code|mod1min\n1|att%|15\n", k)
        self.assertEqual(M.merge_keyed("qualityitems", pd2, d2r, NO_CTX).rows, [["att%", "15"]])

    def test_cube_pd2_recipes_then_d2r_only_ones(self):
        d2r = table('description|input 1|input 2|output|firstLadderSeason\n'
                    'old|"rin,qty=3"||amu|\nwarlock|wt1|wt2|wt3|1\n')
        pd2 = table('description|input 1|input 2|output|ladder\n3 rings|"rin,qty=3"||amu|1\n"new, odd"|amu|amu|rin|\n')
        self.assertEqual(M.merge_cube(pd2, d2r, NO_CTX).rows, [
            ["3 rings", "rin,qty=3", "", "amu", ""], ["new, odd", "amu", "amu", "rin", ""],
            ["warlock", "wt1", "wt2", "wt3", "1"]])

    def test_cube_numinputs_counts_each_item(self):
        # D2R wants numinputs == items in the cube; PD2 counts stacks. Op 30 and sums past the cube's 12 cells keep PD2's
        head = "description|op|numinputs|input 1|input 2|input 3\n"
        pd2 = table(head + '3 maps||1|"t1me,qty=3"||\nrunes||2|"key,qty=1"|"r01,qty=3"|\nok||3|t1me|t1me|t1me\n'
                    'block|30|2|map|"ggm4,qty=3"|\nimbue|18|3|"upma,qty=50"|"rune,qty=50"|"ggm4,qty=50"\n')
        d2r = table(head + 'vanilla||1|"gcv,qty=3"||\n')
        self.assertEqual([r[2] for r in M.merge_cube(pd2, d2r, NO_CTX).rows], ["3", "4", "3", "2", "3", "1"])

    def test_cube_reroll_rows_remake_the_item(self):
        # op 31 has no outputs in PD2 (its cube code makes the item); the rows ship switched off, core-rules runs them
        head = "enabled|op|input 1|input 2|output\n"
        pd2 = table(head + '1|31|"ring,uni"|"imrn,qty=1"|\n1|31|"armo,set"|"imrn,qty=1"|\n1||"ring,uni"|"imrn,qty=1"|amu\n')
        rows = M.merge_cube(pd2, table(head), NO_CTX).rows
        self.assertEqual([(r[0], r[4]) for r in rows], [("0", "usetype,reg"), ("0", "usetype,reg"), ("1", "amu")])

    def test_cube_mod_skill_params(self):
        ren = I.renames({t: [] for t in I.TABLES})
        ren["skills"] = {"impale": "Javelin and Spear Mastery"}
        ctx = I.Context(NO_CTX.lists, ren, {"skills": {"400": "430"}}, {"oskill": "skill", "hit-skill": "skill"})
        head = "description|output|mod 1|mod 1 chance|mod 1 param|mod 1 min|mod 1 max|mod 2|mod 2 chance|mod 2 param\n"
        pd2 = table(head + "x|amu|oskill||Impale|1|1|hit-skill|5|400\n")
        self.assertEqual(M.merge_cube(pd2, table(head), ctx).rows,
                         [["x", "amu", "oskill", "", "Javelin and Spear Mastery", "1", "1", "hit-skill", "5", "430"]])

    def test_cube_stat_condition_params(self):
        # ops 3-26 read param as an itemstatcost id (PD2's corruptor 361 is the mod's 414); other ops keep theirs
        ctx = I.Context(NO_CTX.lists, NO_CTX.ren, {"itemstatcost": {"361": "414"}}, {})
        head = "description|op|param|value|output\n"
        pd2 = table(head + "roll|18|361|0|useitem\noutcome|16|361|500|useitem\nplayer|3|361|1|amu\nmap|30|361||x\n")
        self.assertEqual([r[2] for r in M.merge_cube(pd2, table(head), ctx).rows], ["414", "414", "414", "361"])

    def test_cube_corruption_that_makes_a_new_item_marks_it(self):
        # the new item gets the corrupted mark and a corruptor above every threshold, so no corruption row matches it again
        lists = {**NO_CTX.lists, "itemstatcost": [("corruptor", 414, "corruptor")]}
        ctx = I.Context(lists, NO_CTX.ren, {"itemstatcost": {"361": "414"}}, {})
        head = "op|param|value|input 1|output|mod 1|mod 1 chance|mod 1 min|mod 1 max|mod 2|mod 2 chance|mod 2 min|mod 2 max\n"
        pd2 = table(head + "16|361|500|weap|usetype,rar||||||||\n16|361|1000|weap|useitem|sock|100|1|6|corruptnum|100|1001|1001\n"
                    "16|361|500|t1me|t51,uni||||||||\n16|361|3000|t1me|useitem|corruptnum|100|3001|3001||||\n"
                    "16|361|200|anni|cm1,mag|corruptnum|100|1001|1001||||\n")
        rows = M.merge_cube(pd2, table(head), ctx).rows
        self.assertEqual(rows[4][5:], ["corruptnum", "100", "1001", "1001", "", "", "", ""])  # marks itself
        self.assertEqual(rows[0][5:], ["corrupt", "100", "100", "100", "corruptnum", "100", "3001", "3001"])
        self.assertEqual(rows[2][5:], ["corrupt", "100", "100", "100", "corruptnum", "100", "3001", "3001"])
        self.assertEqual(rows[1][5], "sock")
        # the mark does not depend on which outcome rows are merged (no map rows here), and must stay above every threshold
        rows = M.merge_cube(table(head + "16|361|500|weap|usetype,rar||||||||\n"), table(head), ctx).rows
        self.assertEqual(rows[0][5:], ["corrupt", "100", "100", "100", "corruptnum", "100", "3001", "3001"])
        with self.assertRaisesRegex(I.IdError, "3001"):
            M.merge_cube(table(head + "16|361|3001|t1me|useitem||||||||\n"), table(head), ctx)


class NoDropTests(unittest.TestCase):
    def test_map_items_never_drop_and_other_rates_stay(self):
        types = table("ItemType|Code|Equiv1|Equiv2\nMap|map||\nMap T1|t1m|map|\nRing|ring||\n", "Code")
        misc = table("name|code|type|spawnable\nKurast Map|t11|t1m|1\nUber Map|ubt|ubr|1\nRing|rin|ring|1\n", "code")
        tc = table("Treasure Class|Picks|NoDrop|Item1|Prob1|Item2|Prob2|Item3|Prob3\n"
                   "Map Tier 1|1||t11|1|ubt|1||\nAct 1 (H) Good|1|3|Map Tier 1|1|rin|2||\n"
                   "Counted|-3||rin|1|t11|2||\n")
        blocked = M.block_drops(tc, {"misc": misc}, types, {"map", "ubt"})
        self.assertEqual(blocked, {"t11", "ubt"})
        self.assertEqual(tc.rows, [["Map Tier 1", "1", "2", "", "", "", "", "", ""],
                                   ["Act 1 (H) Good", "1", "4", "rin", "2", "", "", "", ""],
                                   ["Counted", "-1", "", "rin", "1", "", "", "", ""]])
        self.assertEqual([r[3] for r in misc.rows], ["0", "0", "1"])


class HdTests(unittest.TestCase):
    def test_new_items_uniques_and_sets_get_a_picture(self):
        with tempfile.TemporaryDirectory() as tmp:
            hd = Path(tmp)
            (hd / "items.json").write_text(json.dumps([{"rin": {"asset": "ring/ring"}}, {"amu": {"asset": "amulet/amulet"}}]))
            (hd / "uniques.json").write_text(json.dumps([{"nagelring": {"normal": "ring/nagel", "uber": "x", "ultra": "x"}}]))
            (hd / "sets.json").write_text(json.dumps([]))
            pd2 = {"misc": table("name|code|type|invfile\nRing|rin|ring|invrin\nBig Ring|brn|ring|invrin\n"
                                 "Odd|odd|xx|invodd\n", "code")}
            d2r = {"misc": table("name|code|type|invfile\nRing|rin|ring|invrin\nAmulet|amu|amul|invamu\n", "code")}
            # (mod name, base code, the D2R row it was merged onto)
            uniques = [("Nagelring", "rin", "Nagelring"), ("Big One", "brn", None)]
            files = M.hd_items(hd, pd2, d2r, uniques, [("Gift", "amu", None)])
            self.assertEqual(files["items.json"][2:], [{"brn": {"asset": "ring/ring"}}, {"odd": {"asset": "ring/ring"}}])
            self.assertEqual(files["uniques.json"][1:], [{"big_one": {"normal": "ring/ring", "uber": "ring/ring",
                                                                      "ultra": "ring/ring"}}])
            self.assertEqual(files["sets.json"], [{"gift": {"normal": "amulet/amulet", "uber": "amulet/amulet",
                                                            "ultra": "amulet/amulet"}}])

    def test_a_4th_character_the_key_patch_changes_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            hd = Path(tmp)
            (hd / "items.json").write_text(json.dumps([{"rin": {"asset": "ring/ring"}}]))
            for f in ("uniques.json", "sets.json"):
                (hd / f).write_text("[]")
            d2r = {"misc": table("name|code|type|invfile\nRing|rin|ring|invrin\n", "code")}
            ok = {"misc": table("name|code|type|invfile\nRune|r01s|ring|x\nKey|pk1z|ring|x\n", "code")}
            self.assertEqual(M.hd_items(hd, ok, d2r, [], [])["items.json"][1:],
                             [{"r01s": {"asset": "ring/ring"}}, {"pk1z": {"asset": "ring/ring"}}])
            for code in ("r01S", "r01_"):
                bad = {"misc": table(f"name|code|type|invfile\nRune|{code}|ring|x\n", "code")}
                with self.assertRaisesRegex(I.IdError, code):
                    M.hd_items(hd, bad, d2r, [], [])


class RuleFileTests(unittest.TestCase):
    def test_reads_tables_and_no_drop(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "item-tables.txt"
            p.write_text("# notes\nmerge\tweapons\n\nno-drop\tmap\twhy\n", "utf-8")
            self.assertEqual(M.read_rule(p), (["weapons"], {"map"}))
            p.write_text("merge\tnosuch\n", "utf-8")
            with self.assertRaisesRegex(I.IdError, "nosuch"):
                M.read_rule(p)


if __name__ == "__main__":
    unittest.main()
