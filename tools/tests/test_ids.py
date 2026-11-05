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

    def test_port_row_copies_an_earlier_row_with_columns_set(self):
        d2r = table("Name|itype1|itype2|rare\r\nSturdy|shld||1\r\nDevil's|amul|circ|1\r\n")
        pd2 = table("Name|itype1|itype2|rare\r\nSturdy|helm||1\r\nNew|ring||\r\n")
        lines = [("Sturdy", 0, "Sturdy"), ("New", 2, "New"), ("+1 itype1=tors itype2= rare=3", 3, "Devil's")]
        merged = I.merge_table("magicprefix", pd2, d2r, lines, {})[0]
        self.assertEqual(merged.rows, [["Sturdy", "helm", "", "1"], ["Devil's", "amul", "circ", "1"],
                                       ["New", "ring", "", ""], ["Devil's", "tors", "", "3"]])
        with self.assertRaisesRegex(I.IdError, "no column 'itype9'"):
            I.merge_table("magicprefix", pd2, d2r, [*lines[:2], ("+1 itype9=x", 3, "Devil's")], {})


class PortRowTests(unittest.TestCase):
    def test_check_takes_port_rows_in_the_appended_ids(self):
        I.check("magicprefix", [("A", 0, "A"), ("B", 2, "B"), ("+0 x=1", 3, "A")], ["A", "B"], ["A", "X"])
        self.assertEqual(I.seed("magicprefix", ["A", "B", "C"], ["A", "X"], [("A", 0, "A"), ("+0 x=1", 2, "A")]),
                         [("B", 1, "B"), ("C", 3, "C")])  # C skips the port row's 2

    def test_check_refuses_a_port_row_on_a_d2r_id_or_copying_a_later_row(self):
        for line, msg in [(("+0 x=1", 1, "A"), "after D2R's rows"), (("+3 x=1", 2, "A"), "earlier row"),
                          (("+-1 x=1", 2, "A"), "want"), (("+0 x", 2, "A"), "want")]:
            with self.subTest(msg), self.assertRaisesRegex(I.IdError, msg):
                I.check("magicprefix", [("A", 0, "A"), line], ["A"], ["A", "X"])


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

    def test_stat_keeps_d2r_strings_its_function_reads(self):
        d2r = table("Stat|descfunc|descval|descstrpos|descstrneg\n"
                    "singleskill|27||ItemModifierClassSkill|ItemModifierClassSkill\nelemskill|19||ModStr3i|ModStr3i\n")
        pd2 = table("Stat|descfunc|descval|descstrpos|descstrneg\nsingleskill|27|||\nelemskill||||\n")
        lines = [("singleskill", 0, "singleskill"), ("elemskill", 1, "elemskill")]
        merged, _ = I.merge_table("itemstatcost", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["singleskill", "27", "", "ItemModifierClassSkill", "ItemModifierClassSkill"],
                                       ["elemskill", "", "", "", ""]])

    def test_function_numbers_moved_off_d2r_slots(self):
        d2r = table("Missile|pCltDoFunc|pSrvDoFunc\nflamewave|71|38\n")
        pd2 = table("Missile|pCltDoFunc|pSrvDoFunc\nback|70|38\nbolt|1|1\nblessedhammer|72|40\n")
        lines = [("back", 1, "back"), ("bolt", 2, "bolt"), ("blessedhammer", 3, "blessedhammer")]
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["flamewave", "71", "38"], ["back", "78", "46"], ["bolt", "1", "1"],
                                       ["blessedhammer", "80", "47"]])

    def test_holy_light_keeps_its_slots_fist_delay_hit_moves(self):
        d2r = table("Missile|pCltHitFunc\nfistoftheheavensdelay|26\n")
        pd2 = table("Missile|pCltHitFunc\nfistoftheheavensdelay|26\nholylightdelay|66\n")
        lines = [("fistoftheheavensdelay", 0, "fistoftheheavensdelay"), ("holylightdelay", 1, "holylightdelay")]
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["fistoftheheavensdelay", "71"], ["holylightdelay", "66"]])  # D2R's 26 differs

    def test_vengeance_keeps_its_slot_joust_goes_to_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nVengeance|174\nJoust|162\n")
        lines = [("Vengeance", 0, "Vengeance"), ("Joust", 1, "Joust")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["Vengeance", "174"], ["Joust", "190"]])  # D2R fills 162

    def test_poison_dagger_function_numbers(self):
        d2r = table("skill|srvstfunc|srvdofunc|cltdofunc\nAttack|1|1|1\n")
        pd2 = table("skill|srvstfunc|srvdofunc|cltdofunc\nPoison Dagger|68|164|102\n")
        lines = [("Poison Dagger", 1, "Poison Dagger")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["Poison Dagger", "32", "190", "122"])

    def test_finisher_function_numbers(self):
        # srvdo 170 joins the router; the prg values move only in the prg columns (srvdo 39 / 139 stay D2R's)
        cols = "skill|srvdofunc|srvprgfunc1|srvprgfunc2|srvprgfunc3"
        d2r = table(f"{cols}\nAttack|1|||\n")
        pd2 = table(f"{cols}\nFists of Fire|170|39|172|171\nBlades of Ice|170||139|143\nCold|139|||\n")
        lines = [("Fists of Fire", 1, "Fists of Fire"), ("Blades of Ice", 2, "Blades of Ice"), ("Cold", 3, "Cold")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Fists of Fire", "190", "186", "185", "184"],
                                           ["Blades of Ice", "190", "", "181", "143"], ["Cold", "139", "", "", ""]])

    def test_finisher_client_function_numbers(self):
        # cltprg 106/107 move only in the prg columns (cltdo 106 stays D2R's Bind Demon); cdo 69 moves, chit 67 stays
        cols = "skill|cltdofunc|cltprgfunc1|cltprgfunc2|cltprgfunc3"
        d2r = table(f"{cols}\nAttack|1|||\n")
        pd2 = table(f"{cols}\nFists of Fire|1|9|107|106\nCobra Strike|1|106|13|107\nBind|106|||\n")
        lines = [("Fists of Fire", 1, "Fists of Fire"), ("Cobra Strike", 2, "Cobra Strike"), ("Bind", 3, "Bind")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Fists of Fire", "1", "9", "128", "127"],
                                           ["Cobra Strike", "1", "127", "13", "128"], ["Bind", "106", "", "", ""]])
        d2r = table("Missile|pCltDoFunc|pCltHitFunc\nbolt|1|\n")
        pd2 = table("Missile|pCltDoFunc|pCltHitFunc\nfistsoffireexplode|69|\nclawsofthunderbolt|1|67\n")
        lines = [("fistsoffireexplode", 0, "fistsoffireexplode"), ("clawsofthunderbolt", 1, "clawsofthunderbolt")]
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["fistsoffireexplode", "79", ""], ["clawsofthunderbolt", "1", "67"]])

    def test_psychic_hammer_damage_moves_dragon_flight_stays(self):
        # dmg 15 is D2R's holy bolt, so PD2's moves to 22; srvdo 178 is free in D2R and keeps its number (W31)
        d2r = table("Missile|pSrvDmgFunc\nholybolt|15\n")
        pd2 = table("Missile|pSrvDmgFunc\npsychichammer|15\n")
        lines = [("psychichammer", 0, "psychichammer")]
        merged, _ = I.merge_table("missiles", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["psychichammer", "22"]])
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nDragon Flight|178\n")
        lines = [("Dragon Flight", 1, "Dragon Flight")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Dragon Flight", "178"]])

    def test_teleport_goes_to_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nTeleport|155\nBlink|155\n")
        lines = [("Teleport", 1, "Teleport"), ("Blink", 2, "Blink")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Teleport", "190"], ["Blink", "190"]])

    def test_gust_and_hunger_go_to_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nGust|156\nHunger|165\n")
        lines = [("Gust", 1, "Gust"), ("Hunger", 2, "Hunger")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Gust", "190"], ["Hunger", "190"]])

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

    def test_summon_hit_moves_to_67_and_names_the_mod_monster(self):
        cols = "Missile|pSrvHitFunc|sHitPar1|sHitPar2|sHitPar3"
        d2r = table(f"{cols}\narrow|1|||\n")
        pd2 = table(f"{cols}\norb|6|999|8|67239936\n")
        merged, _ = I.merge_table("missiles", pd2, d2r, [("orb", 1, "orb")], I.renames({t: [] for t in I.TABLES}),
                                  {"monstats": {"999": "1016"}})
        self.assertEqual(merged.rows[1], ["orb", "67", "1016", "8", "67239936"])

    def test_monstats_ids_follow_the_name_merge(self):
        # D2R's rows keep their ids (the Expansion row is not one); PD2-only rows follow D2R's last
        d2r = table("Id|x\nskeleton1|\nExpansion|\nwarlockimp|\nmaggot|\n")
        pd2 = table("Id|x\nskeleton1|\nExpansion|\nmaggot|\nleoricGuards|\ntorajanBoss|\n")
        self.assertEqual(I.monstats_ids(pd2, d2r), {"0": "0", "1": "2", "2": "3", "3": "4"})

    def test_monster_kit_functions_move_to_core_rules_slots(self):
        cols = "skill|srvdofunc|srvstfunc|cltdofunc"
        pd2 = table(f"{cols}\nPoisonEggSpawn|173|43|109\nTorajanBossNest|181|49|50\nCorpseCycler||67|\n")
        lines = [("PoisonEggSpawn", 0, "PoisonEggSpawn"), ("TorajanBossNest", 1, "TorajanBossNest"),
                 ("CorpseCycler", 2, "CorpseCycler")]
        merged, _ = I.merge_table("skills", pd2, table(f"{cols}\nAttack|1||\n"), lines,
                                  I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows, [["PoisonEggSpawn", "173", "43", "126"], ["TorajanBossNest", "190", "49", "50"],
                                       ["CorpseCycler", "", "83", ""]])

    def test_blood_warp_functions_moved_to_core_rules_slots(self):
        d2r = table("skill|srvdofunc|srvstfunc|cltstfunc\nAttack|1|0|0\n")
        pd2 = table("skill|srvdofunc|srvstfunc|cltstfunc\nAttack|1|0|0\nBlood Warp|157|66|56\n")
        lines = [("Attack", 0, "Attack"), ("Blood Warp", 1, "Blood Warp")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["Blood Warp", "190", "80", "63"])  # router, then the first free start slots

    def test_ormus_property_function_moves_to_core_rules_slot(self):
        self.assertEqual(I.spell("properties", "func1", "25", {}), "26")
        self.assertEqual(I.spell("properties", "func1", "21", {}), "21")

    def test_charge_keeps_its_server_slots_and_takes_d2r_client_functions(self):
        cols = "skill|srvdofunc|srvstfunc|cltdofunc|cltstfunc"
        d2r = table(f"{cols}\nAttack|1|0|0|0\n")
        pd2 = table(f"{cols}\nAttack|1|0|0|0\nCharge|180|72|112|57\n")
        lines = [("Attack", 0, "Attack"), ("Charge", 1, "Charge")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["Charge", "180", "72", "37", "25"])

    def test_static_field_and_combustion_join_the_router(self):
        cols = "skill|srvdofunc|cltdofunc"
        d2r = table(f"{cols}\nAttack|1|0\n")
        pd2 = table(f"{cols}\nAttack|1|0\nStatic Field|160|\nCombustion|159|100\n")
        lines = [("Attack", 0, "Attack"), ("Static Field", 1, "Static Field"), ("Combustion", 2, "Combustion")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Static Field", "190", ""], ["Combustion", "190", "120"]])

    def test_melee_splash_joins_the_router(self):
        d2r = table("skill|srvdofunc\nAttack|1\n")
        pd2 = table("skill|srvdofunc\nAttack|1\nproc_SplashDamage|163\n")
        lines = [("Attack", 0, "Attack"), ("proc_SplashDamage", 1, "proc_SplashDamage")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1], ["proc_SplashDamage", "190"])  # D2R fills 163

    def test_firestorm_and_wall_makers_join_the_router(self):
        d2r = table("skill|srvdofunc|cltdofunc\nAttack|1|1\n")
        pd2 = table("skill|srvdofunc|cltdofunc\nAttack|1|1\nFirestorm|166|119\nDiabWall|166|58\n")
        lines = [("Attack", 0, "Attack"), ("Firestorm", 1, "Firestorm"), ("DiabWall", 2, "DiabWall")]
        merged, _ = I.merge_table("skills", pd2, d2r, lines, I.renames({t: [] for t in I.TABLES}))
        self.assertEqual(merged.rows[1:], [["Firestorm", "190", "119"], ["DiabWall", "190", "58"]])  # D2R fills 166

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


class LevelTableTests(unittest.TestCase):
    def test_left_out_rows(self):
        # a PD2 row the port leaves out (a cut area, a placeholder): listed with id '-', merged nowhere
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "ids-levels.tsv"
            p.write_text("# PD2 name\tid\tname\nA\t0\tA\nPvp Map\t-\tcut: PvP\nB\t2\tB\n", "utf-8")
            lines = I.read_list(p)
        self.assertEqual(lines[1], ("Pvp Map", I.LEFT_OUT, "cut: PvP"))
        I.check("levels", lines, ["A", "Pvp Map", "B"], ["A", "X"])
        self.assertEqual(I.seed("levels", ["A", "Pvp Map", "B", "C"], ["A", "X"], lines), [("C", 3, "C")])
        merged, _ = I.merge_table("levels", table("Name|Id\nA|0\nPvp Map|1\nB|2\n"), table("Name|Id\nA|0\nX|1\n"),
                                  lines, I.renames({"levels": lines}))
        self.assertEqual(merged.rows, [["A", "0"], ["X", "1"], ["B", "2"]])
        self.assertEqual(I.renames({"levels": lines}), {"levels": {}})

    def test_numeric_references_id_columns_and_d2r_columns(self):
        # PD2's level 3 appends at 4 (D2R took 3): its Vis, a preset's LevelId and the Id columns follow it;
        # an objects row keeps D2R's Token on a merged row only
        ids = {"levels": {"0": "0", "1": "1", "3": "4"}, "objgroup": {"0": "0", "2": "5"}}
        lv = I.merge_table("levels", table("Name|Id|Vis0|ObjGrp0\nNull|0|0|0\nTown|1|3|0\nNew|3|1|2\n"),
                           table("Name|Id|Vis0|ObjGrp0\nNull|0|0|0\nTown|1|0|0\n"),
                           [("Null", 0, "Null"), ("Town", 1, "Town"), ("New", 4, "New")], {}, ids)[0]
        self.assertEqual(lv.rows[1], ["Town", "1", "4", "0"])
        self.assertEqual(lv.rows[2], ["New", "4", "1", "5"])
        pr = I.merge_table("lvlprest", table("Name|Def|LevelId\nNew 1|7|3\n"), table("Name|Def|LevelId\n"),
                           [("New 1", 9, "New 1")], {}, ids)[0]
        self.assertEqual(pr.rows, [["New 1", "9", "4"]])
        with self.assertRaisesRegex(I.IdError, "Vis0 names PD2's levels row 2, which has no id"):
            I.merge_table("levels", table("Name|Vis0\nA|2\n"), table("Name|Vis0\n"), [("A", 0, "A")], {}, ids)
        ob = I.merge_table("objects", table("Name|Token|SizeX\nchest|C1|3\nnew|N1|2\n"),
                           table("Class|Name|*ID|Token|SizeX\nChest1|chest|0|hd1|1\n"),
                           [("chest", 0, "Chest1"), ("new", 1, "new")], {})[0]
        self.assertEqual(ob.rows, [["Chest1", "chest", "0", "hd1", "3"], ["new", "new", "1", "N1", "2"]])

    def test_objtype_follows_the_objects_list(self):
        lines = [("Dummy", 0, "Null"), ("<reserved>", I.LEFT_OUT, "placeholder"), ("chest", 2, "chest")]
        self.assertEqual(I.follow(lines, ["Dummy", "<reserved>", "chest"], ["Not Used", "Casket"], ["Not Used", "x", "Chest"]),
                         [("Not Used", 0, "Not Used"), ("x", I.LEFT_OUT, ""), ("Chest", 2, "Chest")])


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
