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
        pd2 = table('description|input 1|input 2|output|ladder\n3 rings|"rin,qty=3"||amu|1\nnew|amu|amu|rin|\n')
        self.assertEqual(M.merge_cube(pd2, d2r, NO_CTX).rows, [
            ["3 rings", "rin,qty=3", "", "amu", ""], ["new", "amu", "amu", "rin", ""], ["warlock", "wt1", "wt2", "wt3", "1"]])

    def test_cube_mod_skill_params(self):
        ren = I.renames({t: [] for t in I.TABLES})
        ren["skills"] = {"impale": "Javelin and Spear Mastery"}
        ctx = I.Context(NO_CTX.lists, ren, {"skills": {"400": "430"}}, {"oskill": "skill", "hit-skill": "skill"})
        head = "description|output|mod 1|mod 1 chance|mod 1 param|mod 1 min|mod 1 max|mod 2|mod 2 chance|mod 2 param\n"
        pd2 = table(head + "x|amu|oskill||Impale|1|1|hit-skill|5|400\n")
        self.assertEqual(M.merge_cube(pd2, table(head), ctx).rows,
                         [["x", "amu", "oskill", "", "Javelin and Spear Mastery", "1", "1", "hit-skill", "5", "430"]])


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
