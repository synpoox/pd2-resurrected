"""pd2_ids on synthetic tables (no PD2 or D2R data)."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_ids as I  # noqa: E402


def table(text: str, key: str | None = None) -> I.Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "\t").encode("latin-1"))
        return I.Table(p, key)


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

    def test_stat_display_follows_the_string(self):
        d2r = table("Stat|descpriority|descfunc|descval|descstrpos|descstrneg\nstrength|67|19||ModStr1a|ModStr1a\n"
                    "str_bytime|1|17||ModStr1a|ModStr1a\ntohit|5|19||ModStr1h|ModStr1h\ndex|6|19||ModStr1b|ModStr1b\n")
        pd2 = table("Stat|descpriority|descfunc|descval|descstrpos|descstrneg\nstrength|68|1|1|ModStr1a|ModStr1a\n"
                    "str_bytime|1|17|1|ModStr1a|ModStr1a\ntohit|5|1|1|PD2Str|PD2Str\ndex|6|1|1|ModStr1a|ModStr1a\n"
                    "new_stat|88|4|1|ModStr1a|ModStr1a\nnew_bytime|2|17|1|ModStr1a|ModStr1a\nnew_pd2|1|1|1|PD2Str|\n")
        names = ["strength", "str_bytime", "tohit", "dex", "new_stat", "new_bytime", "new_pd2"]
        lines = [(s, i, s) for i, s in enumerate(names)]
        merged, _ = I.merge_table("itemstatcost", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["strength", "68", "19", "", "ModStr1a", "ModStr1a"],
                                       ["str_bytime", "1", "17", "", "ModStr1a", "ModStr1a"],
                                       ["tohit", "5", "1", "1", "PD2Str", "PD2Str"],
                                       ["dex", "6", "19", "", "ModStr1a", "ModStr1a"],  # moved to another D2R string
                                       ["new_stat", "88", "19", "", "ModStr1a", "ModStr1a"],
                                       ["new_bytime", "2", "17", "", "ModStr1a", "ModStr1a"],
                                       ["new_pd2", "1", "1", "1", "PD2Str", ""]])

    def test_function_numbers_moved_off_d2r_slots(self):
        d2r = table("Missile|pCltDoFunc|pSrvDoFunc\nflamewave|71|38\n")
        pd2 = table("Missile|pCltDoFunc|pSrvDoFunc\nback|70|38\nbolt|1|1\n")
        lines = [("back", 1, "back"), ("bolt", 2, "bolt")]
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["flamewave", "71", "38"], ["back", "78", "46"], ["bolt", "1", "1"]])

    def test_poison_dagger_function_numbers(self):
        d2r = table("skill|srvstfunc|srvdofunc|cltdofunc\nAttack|1|1|1\n")
        pd2 = table("skill|srvstfunc|srvdofunc|cltdofunc\nPoison Dagger|68|164|102\n")
        lines = [("Poison Dagger", 1, "Poison Dagger")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["Poison Dagger", "32", "190", "122"])

    def test_teleport_goes_to_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nTeleport|155\nBlink|155\n")
        lines = [("Teleport", 1, "Teleport"), ("Blink", 2, "Blink")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Teleport", "190"], ["Blink", "190"]])

    def test_state_hit_parameters_take_mod_ids(self):
        # srvhit 61 reads sHitPar1 as a state and sHitPar3 / cHitPar2 as stats; other functions' parameters stay
        cols = "Missile|pSrvHitFunc|sHitPar1|sHitPar2|sHitPar3|cHitPar1|cHitPar2|cHitPar3"
        d2r = table(f"{cols}\narrow|1|||||||\n")
        pd2 = table(f"{cols}\ncloud|61|236|52|-1|-1||\nblaze|61|44|150|67|-60|500|-60\nnova|5|236|1|67|1|500|1\n")
        lines = [("cloud", 1, "cloud"), ("blaze", 2, "blaze"), ("nova", 3, "nova")]
        ids = {"states": {"236": "282", "44": "44"}, "itemstatcost": {"67": "67", "500": "510"}}
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}), ids)
        self.assertEqual(merged.rows[1:], [["cloud", "61", "282", "52", "-1", "-1", "", ""],
                                           ["blaze", "61", "44", "150", "67", "-60", "510", "-60"],
                                           ["nova", "5", "236", "1", "67", "1", "500", "1"]])

    def test_blood_warp_functions_moved_to_core_rules_slots(self):
        d2r = table("skill|srvdofunc|srvstfunc|cltstfunc\nAttack|1|0|0\n")
        pd2 = table("skill|srvdofunc|srvstfunc|cltstfunc\nAttack|1|0|0\nBlood Warp|157|66|56\n")
        lines = [("Attack", 0, "Attack"), ("Blood Warp", 1, "Blood Warp")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["Blood Warp", "190", "80", "63"])  # router, then the first free start slots

    def test_melee_splash_joins_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nAttack|1\nproc_SplashDamage|163\n")
        lines = [("Attack", 0, "Attack"), ("proc_SplashDamage", 1, "proc_SplashDamage")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["proc_SplashDamage", "190"])  # D2R fills 163

    def test_missile_do_functions_become_the_missile_column(self):
        cols = "skill|srvdofunc|srvmissile|srvmissilea|cltdofunc|cltmissile|cltmissilea"
        d2r = table(f"{cols}\nAttack|1||||||\n")
        pd2 = table(f"{cols}\nbolt|188||jav|117||jav\nkept|188|own|jav|117|own|jav\nother|5||jav|3||jav\n")
        lines = [("bolt", 1, "bolt"), ("kept", 2, "kept"), ("other", 3, "other")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["bolt", "", "jav", "", "", "jav", ""],
                                           ["kept", "", "own", "jav", "", "own", "jav"],
                                           ["other", "5", "", "jav", "3", "", "jav"]])

    def test_name_length(self):
        with self.assertRaisesRegex(I.IdError, "31 bytes"):
            I.check("missiles", [("x" * 32, 0, "x" * 32)], ["x" * 32], [])
        self.assertEqual(I.seed("missiles", ["y" * 40], [], []), [("y" * 40, 0, "y" * 31)])


class ItemTableTests(unittest.TestCase):
    def test_expansion_rows_and_pd2_names(self):
        # setitems: PD2 renamed a vanilla row, inserted one mid-table and appended one; D2R added one after vanilla
        vanilla, I.VANILLA["setitems"] = I.VANILLA["setitems"], 1
        try:
            d2r = table("index|*ID|set\nTorch|0|A\nExpansion||\nCow Horns|1|B\nWarlock Hood|2|W\n")
            pd2 = table("index|set\nSpire|A\nExpansion|\nDeath Band|K\nCow Horns|B\nNew One|N\n")
            lines = I.seed("setitems", pd2.names(), d2r.names(), [])
            self.assertEqual(lines, [("Spire", 0, "Spire"), ("Death Band", 3, "Death Band"), ("Cow Horns", 1, "Cow Horns"),
                                     ("New One", 4, "New One")])
            I.check("setitems", lines, pd2.names(), d2r.names())
            merged, _ = I.merge_table("setitems", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
            self.assertEqual(merged.rows, [["Spire", "0", "A"], ["Expansion", "", ""], ["Cow Horns", "1", "B"],
                                           ["Warlock Hood", "2", "W"], ["Death Band", "3", "K"], ["New One", "4", "N"]])
        finally:
            I.VANILLA["setitems"] = vanilla

    def test_key_column_and_duplicate_names(self):
        d2r = table("ItemType|Code\nNone|none\nWarlock|warl\n", "Code")
        pd2 = table("ItemType|Code\nNone|\nMap|map\n", "Code")
        vanilla, I.VANILLA["itemtypes"] = I.VANILLA["itemtypes"], 1  # D2R's warl ends vanilla
        try:
            self.assertEqual(I.seed("itemtypes", pd2.names(), d2r.names(), []), [("", 0, "none"), ("map", 2, "map")])
        finally:
            I.VANILLA["itemtypes"] = vanilla
        affixes = ["Sturdy", "Sturdy", "Fine"]
        lines = I.seed("magicprefix", affixes, [], [])
        self.assertEqual(lines, [("Sturdy", 0, "Sturdy"), ("Sturdy#2", 1, "Sturdy"), ("Fine", 2, "Fine")])
        I.check("magicprefix", lines, affixes, [])


    def test_property_params_columns_and_defaults(self):
        # skill and state parameters follow P2's remap whether PD2 names the row or gives its id; NPC columns renamed
        props = {"oskill": "skill", "hit-skill": "skill", "skill-rand": "skillrange", "state": "state", "res": ""}
        ids = {"skills": {"400": "430", "401": "431"}, "states": {}}
        ren = I.renames({t: [] for t in I.TABLES})
        ren["skills"], ren["states"] = {"impale": "Javelin and Spear Mastery"}, {"swordmastery": "blademastery"}
        d2r = table("index|*ID|prop1|par1|min1|max1|prop2|par2|min2|max2|spawnable\n")
        pd2 = table("index|prop1|par1|min1|max1|prop2|par2|min2|max2|enabled\n"
                    "A|oskill|Impale|1|1|hit-skill|400|5|3|1\nB|skill-rand|10|400|401|state|swordmastery|||0\n"
                    "C|res|400|5|5||||||\n")
        lines = [("A", 0, "A"), ("B", 1, "B"), ("C", 2, "C")]
        merged, _ = I.merge_table("uniqueitems", pd2, d2r, lines, ren, ids, props)
        self.assertEqual(merged.rows, [
            ["A", "0", "oskill", "Javelin and Spear Mastery", "1", "1", "hit-skill", "430", "5", "3", "1"],
            ["B", "1", "skill-rand", "10", "430", "431", "state", "blademastery", "", "", "0"],
            ["C", "2", "res", "400", "5", "5", "", "", "", "", ""]])
        armor = I.merge_table("armor", table("code|DrehyaMax|nameable|mindam|mindam\nxyz|3|1|5|0\n"),
                              table("code|AnyaMax|Nameable|mindam\n"), [("xyz", 0, "xyz")], ren)[0]
        self.assertEqual(armor.rows, [["xyz", "3", "1", "5"]])  # a repeated column: the first holds the values
        weapons = I.merge_table("weapons", table("code||speed\nxyz|15|-10\n"), table("code|speed|UICatOverride\n"),
                                [("xyz", 0, "xyz")], ren)[0]
        self.assertEqual(weapons.rows, [["xyz", "-10", ""]])  # PD2's unnamed column fills no D2R column
        sets = I.merge_table("setitems", table("index|set\nS|X\n"), table("index|set|spawnable\n"), [("S", 0, "S")], ren)[0]
        self.assertEqual(sets.rows, [["S", "X", "1"]])


class ListFileTests(unittest.TestCase):
    def test_seed_lists_appends_and_read_list_round_trips(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for side, names in (("pd2", ["A", "B"]), ("d2r", ["A"])):
                for t in I.TABLES:
                    p = root / side / "global" / "excel" / f"{t}.txt"
                    p.parent.mkdir(parents=True, exist_ok=True)
                    p.write_text(I.KEY.get(t, "name") + "\n" + "".join(f"{n}\n" for n in names), "latin-1")
            (root / "pd2/global/excel/properties.txt").write_text("code\tfunc1\tstat1\n", "latin-1")
            (root / "merge").mkdir()
            self.assertEqual(I.seed_lists(root / "merge", root / "pd2", root / "d2r"), {t: 2 for t in I.TABLES})
            self.assertEqual(I.seed_lists(root / "merge", root / "pd2", root / "d2r"), {t: 0 for t in I.TABLES})
            self.assertEqual(I.read_list(root / "merge" / "ids-skills.tsv"), [("A", 0, "A"), ("B", 1, "B")])
            out = root / "out"
            I.apply(root / "merge" / "ids-skills.tsv", root / "pd2", root / "d2r", out)
            self.assertEqual((out / "global/excel/skills.txt").read_text("latin-1"), "name\nA\nB\n")


if __name__ == "__main__":
    unittest.main()
