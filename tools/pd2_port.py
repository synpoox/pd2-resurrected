"""pd2-resurrected's data for d2rAI-toolkit's classic import (shared/classic_import): what the mod decides about PD2's
tables, as the toolkit's merge, rule and gate take it. The merge itself, the id lists' format and the column and
reference rules live in the toolkit (ADR 0003 for the lists, ADR 0007 for when the import runs).

merge/ids-<table>.tsv has one line per PD2 row: `PD2 name <tab> id <tab> name`, the row's id and key name in the mod.
Ids count records as the game does (an `Expansion` row is not one); a repeated PD2 name is listed as `name#2`.
An id of `-` leaves the PD2 row out (a cut area, a placeholder row); the third column then says why.
An id at or below D2R's last row merges the PD2 row onto that D2R row and keeps D2R's name, so D2R's own files that
name it stay valid (PD2's names win in the tables whose key is the text the player sees); a higher id appends the
row. Lines are only ever added (`pd2_merge.py --seed-ids`), never renumbered.
A line whose PD2 name starts with `+` is a row the port adds, not a PD2 one (#233): `+<id> col=value ...` appends a copy
of the mod's row `<id>` (an earlier one) with those columns set, at the line's id after D2R's rows. It is copied at
every merge, so it follows its source row through a PD2 re-baseline.

Seeding a new PD2 row picks, in order: the same id below the end of vanilla 1.13c (VANILLA), where both games hold the
same vanilla row; a D2R row of the same name no other line took; the next free id after D2R's last row, under its PD2
name (+ "_pd2" if the mod already has that name).
"""
from __future__ import annotations

from pathlib import Path

import pd2_table  # noqa: F401  (first: puts the toolkit on sys.path)
from shared.classic_import import ids as I  # noqa: E402

SUFFIX = "_pd2"  # added to a PD2 row's name when the mod already has that name


# --- the tables, ids and what the games differ in (lifted from the former pd2_ids) ---

TABLES = ["skills", "states", "itemstatcost", "missiles",  # P2
          # P3: item tables a save or D2R's code reads by row number
          "itemtypes", "uniqueitems", "setitems", "runes", "magicprefix", "magicsuffix", "automagic", "rareprefix",
          "raresuffix",
          # LP1: the level tables a DS1, a save's map memory or D2R's code reads by row number
          "levels", "lvlprest", "lvlwarp", "objects", "objgroup"]
# records below this id are the same vanilla 1.13c row in both games. itemstatcost: none, because both games filled
# vanilla's unused stat slots (183-213, 256-285) with different stats. itemtypes: D2R added warl at 67. setitems: PD2
# inserted a Bul-Kathos item at 117. runes: PD2 reordered its rows, so they match by name.
VANILLA = {"skills": 357, "states": 185, "itemstatcost": 0, "missiles": 684, "itemtypes": 67, "uniqueitems": 401,
           "setitems": 117, "runes": 0, "magicprefix": 669, "magicsuffix": 747, "automagic": 36, "rareprefix": 46,
           "raresuffix": 155,
           # D2R's 137 (Colossal Summit) and its preset 1091 end the shared rows; objects 573-583 are PD2's
           # <reserved> rows, left out; lvlwarp and objgroup: every D2R row is vanilla's
           "levels": 137, "lvlprest": 1091, "lvlwarp": 88, "objects": 573, "objgroup": 133}
# P3 tables pd2_items merges by key with no id list: matched by key, or row by row (the same rows in both games)
_BY_ROW = ["gems", "qualityitems", "lowqualityitems", "books", "itemratio", "uniqueprefix", "uniquesuffix",
           "uniqueappellation"]
VANILLA |= {t: 0 for t in ["weapons", "armor", "misc", "sets", "properties", "treasureclassex", "gamble"]}
VANILLA |= {t: 1 << 20 for t in _BY_ROW}
# columns holding another id-listed table's row number (PD2's) -> that table; levels' Warp0-7 hold lvlwarp's Id column,
# which PD2's new warps keep (pd2_levels checks they don't clash), and LevelType is lvltypes' (pd2_levels)
NUM_REFS = {"levels": {**{f"Vis{i}": "levels" for i in range(8)}, **{f"ObjGrp{i}": "objgroup" for i in range(8)}},
            "lvlprest": {"LevelId": "levels"},
            "objgroup": {f"ID{i}": "objects" for i in range(8)},
            # a thrown weapon's missile (PD2's throwing potions, 912-947, are the mod's 970-1005; IR5)
            "weapons": {"missiletype": "missiles"}}
# A misc row with PD2's map spell opens the level its `len` names (ProjectDiablo.dll evaluates the calc as the level id,
# docs/research/pd2-map-system-dll.md): it takes the mod's id, blank for a left-out level (PvP, PD2's Uber Ancients).
# Every other row's len is a duration.
MAP_SPELL = "12"
LEFT_OUT_LEVELS = "levels left out"  # an ids key: the PD2 level ids the port leaves out
# columns a merged D2R row keeps, where the games give them different meanings (objects Token: D2R's graphics; misc
# spellicon: D2R's cursor for an item spell with a target, which PD2's -1 turns off for identify, #311)
KEEP_D2R = {"objects": {"Token"}, "misc": {"spellicon"}}
# columns D2R's file lacks but its loader reads: misc `auto prefix` (the quivers' automagic, UR6) loads at the items
# record's +0x100, as weapons' and armor's do, since D2R parses all three with one field list (0x315fd0)
ADD_COLUMNS = {"misc": ["auto prefix"]}
# D2R columns computed from the PD2 row. rightskill (right-mouse list): D2R sets it to "not passive" on every class skill;
# taken-over rows need it too, since PD2 turned some vanilla skills passive (Impale -> Javelin and Spear Mastery).
# setitems spawnable and the itemtypes socket thresholds: the value D2R gives every one of its own rows.
# levels DrawEdges, a column both games have, overrides PD2's value: D2R's client gives only acts 1-3 an edge tile
# (0x327090), so in act 5 (Act 4) it draws a null one and crashes (FUN_14021d090, #258) near the drawn floor's end;
# PD2-only areas in act 5 draw none. D2R's own act 5 rows (134, 136) keep PD2's value, the same as D2R's.
DERIVED = {"skills": {"rightskill": lambda pd2: "0" if pd2.get("passive") == "1" else "1"},
           "levels": {"DrawEdges": lambda pd2: "0" if pd2.get("Act") == "4"
                      and int(pd2.get("Id") or 0) >= VANILLA["levels"] else pd2.get("DrawEdges", "")},
           "setitems": {"spawnable": lambda pd2: "1"},
           "itemtypes": {"MaxSocketsLevelThreshold1": lambda pd2: "25", "MaxSocketsLevelThreshold2": lambda pd2: "40"}}
# PD2 function numbers that D2R gave its own functions (missile srvdo 38 flamewave, cltdo 70 vine beast death; skill
# srvdo 153 / cltdo 97, RotW's, PD2's leap; Double Throw's srvdo 154, cltdo 98, shit 30, dmg 16 echoingstrike; dmg 17
# ring of fire; Psychic Hammer's dmg 15, holy bolt's; Teleport's srvdo 155, Gust's 156, Blood Warp's 157, srvst 66, cltst 56, Desecrate's srvdo 161, melee
# splash's 163, Poison Dagger's 164, Hunger's 165 and the wall makers' 166, past D2R's table or D2R's own, joining the
# srvdo 190 router, and Desecrate's cltdo 101; Fist of the Heavens' chit 26, where D2R's differs from PD2's 1.13c
# one; Blade Fury's fragment: shit 34, D2R's echoingstrike, and chit 6, below D2R's free range): moved to the free slots core-rules installs them in
# (plugin/core-rules/skill_functions.cpp Functions[]). Charge's cltdo 112 / cltst 57 go to D2R's Charge client
# functions (37, 25), which do what PD2's do (docs/research/w16-charge.md). Combustion's srvdo 159 and Static Field's
# 160 join the router too, Combustion's cltdo 100 takes 120 (docs/research/w27-static-field-combustion.md).
# The death novas' srvdo 169 joins the router and their cltdo 105 (D2R's) takes 125 (#480).
# shortcut: remaps by value on every PD2 row; key it by row if a PD2 row ever means D2R's own function there
# Blessed Hammer's sdo 40 / cdo 72 are D2R's too and take 47 / 80 (docs/research/w20-blessed-hammer.md).
# The monster kits (W36, docs/research/w36-monster-kits.md): PoisonEggSpawn's cltdo 109 takes 126, TorajanBossNest's
# srvdo 181 (the finishers' prg there) joins the router, the Cyclers' srvst 67 (D2R's Frenzy start) takes 83 and the
# summon-on-hit shit 6 (D2R's leaves out PD2's flags) takes 67.
# Royal Fire's srvdo 143 / cltdo 83 (PD2's boss branch in vanilla slots D2R keeps) join the router and take 123 (UB6).
# Lucion's srvdo 184-186 (D2R's slots hold the finishers' prg) join the router; LucionDash's start, srvst 76, picks a
# leap's spot, so it takes D2R's Leap start, 40 (#487).
# Ormus' Robes' property func 25 (D2R's 25 is affix-rand) takes the free property slot 26 (P12,
# docs/research/p12-ormus-robes.md).
# levels' Teleport 3 (PD2's path search, research/sorceress-w26.md) is not a D2R value; 1 (allowed) stands in until MS7.
# Map items' pSpell 12 (D2R's 12 is the Resist Scroll) takes the free item spell 16 core-events installs (MS2).
# PD2's portal operate 77 (the xactportals) takes D2R's free operate slot 60, which core-events fills (UB2). Its map-event
# altar operate 79 (TaintedSunMapEventAltar, ForceShardAltar) takes the free slot 35, D2R's unused Steeg Stone's (MS8),
# and its seal operate 80 (Zhar's portals and tomes) slot 64 (MS7c, #531).
# The xactportals' `#1`-`#6` graphics tokens and Zhar's portals' `AY` and `#a` have no art in D2R (#431): they draw
# D2R's red portal, `PP`, as xactportal_red does, until their own art is ported.
FUNCS = {"properties": {f"func{i}": {"25": "26"} for i in range(1, 8)},
         "objects": {"OperateFn": {"77": "60", "79": "35", "80": "64"},
                     "Token": {**{f"#{n}": "PP" for n in range(1, 7)}, "AY": "PP", "#a": "PP"}},
         "misc": {"pSpell": {MAP_SPELL: "16"}},
         "levels": {"Teleport": {"3": "1"}},
         "missiles": {"pSrvDoFunc": {"38": "46", "40": "47"}, "pCltDoFunc": {"70": "78", "69": "79", "72": "80"},
                      "pSrvHitFunc": {"30": "66", "34": "69", "6": "67", "60": "68"},
                      "pSrvDmgFunc": {"15": "22", "16": "21", "17": "20"}, "pCltHitFunc": {"26": "71", "6": "68", "65": "72"}},
         "skills": {"srvdofunc": {"153": "172", "154": "183", "155": "190", "156": "190", "157": "190", "159": "190",
                                  "160": "190", "161": "190", "162": "190", "163": "190", "164": "190", "165": "190",
                                  "166": "190", "169": "190", "170": "190", "181": "190", "143": "190", "184": "190",
                                  "185": "190", "186": "190"},
                    "cltdofunc": {"97": "115", "98": "116", "100": "120", "101": "121", "102": "122", "112": "37",
                                  "109": "126", "105": "125", "83": "123"},
                    # item casts (IR5): PD2's ItemEffect 183 (the novas' ring at the target) and ItemCltEffect 116 are
                    # our Double Throw slots, so the novas get 187; Lightning Bolt and Fury's 187 / 116 get 1, D2R's item
                    # cast of srvmissile / cltmissile at the target. 188 / 117 stay: D2R's empty slots do PD2's work.
                    "ItemEffect": {"183": "187", "187": "1"}, "ItemCltEffect": {"116": "1"},
                    # PD2's srvst 68 calls slot 32, D2R's melee start (W11)
                    "srvstfunc": {"66": "80", "68": "32", "67": "83", "76": "40"}, "cltstfunc": {"56": "63", "57": "25", "58": "29"},
                    # the finishers' prg functions (W29): only these PD2 rows use these values in these columns
                    **{f"srvprgfunc{i}": {"171": "184", "172": "185", "39": "186", "139": "181"} for i in (1, 2, 3)},
                    # and their client halves (W30); D2R's 106 is Bind Demon
                    **{f"cltprgfunc{i}": {"106": "127", "107": "128"} for i in (1, 2, 3)}}}
# missiles.txt parameters a PD2 hit function reads as ids: pSrvHitFunc -> {column: the table it names}. srvhit 61
# (Poison Dagger's cloud, the throwing potions, Kanemith's blaze dot) gives the hit unit state sHitPar1 with stats
# sHitPar3 and cHitPar2.
# srvhit 67 (PD2's 6, the summon-on-hit missiles; keyed by the mod's number) spawns monstats row sHitPar1, and srvhit
# 68 (PD2's 60, the throwing potions and Sharptooth's Armageddon) row sHitPar3.
HIT_PARAMS = {"61": {"sHitPar1": "states", "sHitPar3": "itemstatcost", "cHitPar2": "itemstatcost"},
              "67": {"sHitPar1": "monstats"}, "68": {"sHitPar3": "monstats"}}
# PD2's srvdo 188 / cltdo 117 (Lightning Bolt, Lightning Fury) only create srvmissile, else srvmissilea (cltmissile,
# else cltmissilea), with the arguments D2R's do dispatcher passes for srvmissile / cltmissile after any do function
# (docs/research/lightning-javelins.md). So the merge moves that missile into srvmissile / cltmissile and drops the
# function number: (function column, PD2 number, missile column, fallback column).
MISSILE_FUNCS = {"skills": [("srvdofunc", "188", "srvmissile", "srvmissilea"),
                            ("cltdofunc", "117", "cltmissile", "cltmissilea")]}

# PD2 values no table defines, which D2R's loader refuses (#141): table -> (column, PD2 value) -> the row's new cells.
# 1.13c finds no row for them (D2Common's loaders read these columns as lookups; a missing string key reads as 5382,
# "An evil force"), so most keep PD2's behaviour: Deep-Wounds is deep-wounds (lookups ignore case), alternategfx is a
# 4-byte code (1.13c keeps the first 4 letters), and a skill, stat, sound, overlay or property no table has is no
# reference. PD2 bugs we fix (docs/pd2-origin-bugs.md): dblue, amu, and the two potions' "An evil force" description.
_EVIL = {"spelldesc": "", "spelldescstr": ""}  # D2R's own Antidote and Thawing Potion rows have no description
# Map stats (#383): descfunc 6-9 print the value, then descstr2. PD2's map stats name vanilla keys there, plain text in
# 1.13c but printf text in D2R ("%+d%% Deadly Strike"), so each points at a copy without the codes (pd2_strings builds
# it: PD2's text, else D2R's). D2R's own stats keep the vanilla keys.
PLAIN_STRINGS = {k: f"{k}Map" for k in [
    "ModStr1h", "ModStr1j", "ModStr1k", "ModStr1l", "ModStr1n", "ModStr1o", "ModStr1p", "ModStr1q", "ModStr1r",
    "ModStr1s", "ModStr1t", "ModStr2g", "ModStr2h", "ModStr2l", "ModStr2w", "ModStr2z", "Modstr2v", "ModStr3m",
    "ModStr4h", "ModStr4i", "ModStr4m", "ModStr4p", "ModStr4v", "ModStr5c", "ModStr5g", "ModStr5i", "ModStr5k",
    "ModStr5m", "ModStr5q", "ModStr5u", "ModStr5v", "ModStr5w", "ModStr5y", "ModitemdamColdsk", "ModitemdamFiresk",
    "ModitemdamLtngsk", "ModitemdamPoissk", "strModMagicDamage"]}
# Map tiers (#383): D2R fills spelldesc 2's %d from spelldesccalc, 1 on every PD2 map, so the tier comes from the map's
# item type. Unique maps (t5m) count as tier 3, as PD2's wiki says (zone level 89, like tier 3).
_TIER = {f"t{t}m": {"spelldescstr": "MapTierValue", "spelldesccalc": str(min(t, 3))} for t in (1, 2, 3, 5)}
FIXES = {"itemstatcost": {("descstr2", k): {"descstr2": v} for k, v in PLAIN_STRINGS.items()},
         "magicsuffix": {("itype4", "amu"): {"itype4": "amul"}},
         "magicprefix": {("transformcolor", "dblue"): {"transformcolor": "dblu"},
                         ("mod1code", "map-mon-extra-mag"): {"mod1code": "", "mod1min": "", "mod1max": ""}},
         "misc": {("alternategfx", "bonechips"): {"alternategfx": "bone"},
                  ("alternategfx", "invmppvp"): {"alternategfx": "invm"},
                  ("stat1", "dmg-min"): {"stat1": "", "calc1": ""}, ("stat2", "dmg-max"): {"stat2": "", "calc2": ""},
                  ("spelldescstr", "ItemStatsAnti"): _EVIL, ("spelldescstr", "ItemStatsThaw"): _EVIL,
                  **{("type", t): cells for t, cells in _TIER.items()},
                  # PD2's unlimited books share spell 13 (D2R's respec token); core-rules gives each its own free item
                  # spell, the Navigator 17 (Town Portal) and the Almanac 18 (Identify), kept after use (IR5)
                  ("code", "rtp"): {"pSpell": "17"}, ("code", "rid"): {"pSpell": "18"}},
         "missiles": {("Skill", "immolationarrow"): {"Skill": ""}, ("Skill", "RadamentUnholyOrb"): {"Skill": ""},
                      ("HitSound", "firearrow firewall"): {"HitSound": ""}, ("ProgOverlay", "fireexplode"): {"ProgOverlay": ""}}}


# --- the merge: what the mod fixes in a PD2 row after the toolkit has merged it ---

SPELL = I.make_spell(FUNCS)
FIX_ROW = I.fix_cells(FIXES, {"Deep-Wounds": "deep-wounds"})


def map_level(header: list[str], row: list[str], src: dict[str, str], own: list[str] | None, ids: dict[str, dict[str, str]]
              ) -> None:
    """A map item's len (PD2 pSpell MAP_SPELL) in the mod's level ids; blank for a left-out level."""
    spell = next((v for k, v in src.items() if k.lower() == "pspell"), "")
    if spell == MAP_SPELL and "len" in header:
        j = header.index("len")
        if row[j] in ids.get("levels", {}):
            row[j] = ids["levels"][row[j]]
        elif row[j] in ids.get(LEFT_OUT_LEVELS, {}):
            row[j] = ""
        else:
            raise I.IdError(f"misc '{row[0]}': len names PD2's level {row[j]!r}, which has no id")


def finish_of(table: str, ctx: I.Context, d2r: I.Table | None = None) -> I.Finish:
    """What the merge fixes in each merged PD2 row of `table`, in order: map items' levels, itemstatcost's display
    (D2R's table `d2r`), the DERIVED columns, missile functions, hit parameters and item properties' ids."""
    return I.finishes(
        (lambda h, r, s, o: map_level(h, r, s, o, ctx.ids)) if table == "misc" else None,
        I.stat_display(d2r) if table == "itemstatcost" and d2r is not None else None,
        I.derive(DERIVED[table]) if table in DERIVED else None,
        I.missile_funcs(MISSILE_FUNCS[table]) if table in MISSILE_FUNCS else None,
        I.hit_params(HIT_PARAMS, ctx.ids) if table == "missiles" else None,
        I.props_finish(ctx.ren, ctx.ids, ctx.props) if table in I.PROPS else None)


def merge_args(table: str) -> dict:
    """ids.merge_table's keywords for `table`, but the finish."""
    return {"spell": SPELL, "fix_row": FIX_ROW, "d2r_columns": tuple(KEEP_D2R.get(table, ())),
            "add_columns": tuple(ADD_COLUMNS.get(table, ())), "num_refs": NUM_REFS, "source": "PD2"}


def item_merge_args(ctx: I.Context):
    """items.apply's merge_args: the table's keywords, and its finish (the toolkit adds the property fixes itself)."""
    def args(table: str) -> dict:
        project = I.finishes(
            (lambda h, r, s, o: map_level(h, r, s, o, ctx.ids)) if table == "misc" else None,
            I.derive(DERIVED[table]) if table in DERIVED else None,
            I.missile_funcs(MISSILE_FUNCS[table]) if table in MISSILE_FUNCS else None)
        return {**merge_args(table), "finish": project}
    return args


def context_of(lists: dict[str, list[I.Line]], pd2_data: Path, d2r_data: Path | None = None) -> I.Context:
    """The Context of id lists already read: PD2's NUM_REFS, monstats ids, and the level ids a map item's len may leave
    out (LEFT_OUT_LEVELS)."""
    ctx = I.context_of(lists, pd2_data, d2r_data, num_refs=NUM_REFS, monster_stats="map_glob_add_mon_")
    if lists.get("levels"):
        ctx.ids[LEFT_OUT_LEVELS] = I.left_out_of(lists, {"levels": I.load(pd2_data, "levels").names()})["levels"]
    return ctx


def lists_for(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, list[I.Line]]:
    return I.lists_for(merge_dir, TABLES, pd2_data, d2r_data, source="PD2", seed_hint="run tools/pd2_merge.py --seed-ids")


def apply_ids(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-<table>.tsv: write that table."""
    table = rule.stem[len("ids-"):]
    lists = lists_for(rule.parent, pd2_data, d2r_data)
    ctx = context_of(lists, pd2_data, d2r_data)
    d2r = I.load(d2r_data, table)
    merged, dropped = I.merge_table(table, I.load(pd2_data, table), d2r, lists[table], ctx.ren, ctx.ids,
                                    finish=finish_of(table, ctx, d2r), **merge_args(table))
    if table == "levels":
        I.fix_levels(merged, len(d2r.records()), len(I.load(d2r_data, "lvltypes").records()) - 1)
    I.write(table, merged, out_data, dropped, "PD2")
    if table in I.FOLLOWERS:
        f = I.FOLLOWERS[table]
        pd2_f, d2r_f = I.load(pd2_data, f), I.load(d2r_data, f)
        lines = I.follow(lists[table], I.load(pd2_data, table).names(), d2r_f.names(), pd2_f.names())
        merged_f, dropped_f = I.merge_table(f, pd2_f, d2r_f, lines, ctx.ren)
        I.write(f, merged_f, out_data, dropped_f, "PD2")


def seed_lists(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, int]:
    """Append id-list lines for PD2 rows that have none; the lines added per table."""
    return I.seed_lists(merge_dir, TABLES, pd2_data, d2r_data, VANILLA, SUFFIX)
