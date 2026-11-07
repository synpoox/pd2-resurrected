"""Remapped ids: the tables a save or D2R's code reads by row number, merged by the append-only id lists in merge/
(ADR 0003): skills, states, itemstatcost and missiles (P2), item types, unique and set items, runewords and affixes (P3),
and the level tables (LP1): levels, presets, warps, objects (objtype follows the objects list) and object groups.

merge/ids-<table>.tsv has one line per PD2 row: `PD2 name <tab> id <tab> name`, the row's id and key name in the mod.
Ids count records as the game does (an `Expansion` row is not one); a repeated PD2 name is listed as `name#2`.
An id of `-` leaves the PD2 row out (a cut area, a placeholder row); the third column then says why.
An id at or below D2R's last row merges the PD2 row onto that D2R row and keeps D2R's name, so D2R's own files that
name it stay valid (PD2_NAMES tables keep PD2's: there the key is the text the player sees); a higher id appends the
row. Lines are only ever added (`pd2_merge.py --seed-ids`), never renumbered.
A line whose PD2 name starts with `+` is a row the port adds, not a PD2 one (#233): `+<id> col=value ...` appends a copy
of the mod's row `<id>` (an earlier one) with those columns set, at the line's id after D2R's rows. It is copied at
every merge, so it follows its source row through a PD2 re-baseline.

Seeding a new PD2 row picks, in order:
  1. the same id, below the end of vanilla 1.13c (VANILLA), where both games hold the same vanilla row, often
     renamed on one side (PD2 reworks: Impale -> Javelin and Spear Mastery; D2R fixes: nehlithak -> nihlathak);
  2. a D2R row of the same name that no other line took (rows both games added after vanilla, e.g. MonLeap; not
     for affixes, NO_NAME_MATCH);
  3. the next free id after D2R's last row, under its PD2 name (+ "_pd2" if the mod already has that name).

Merging a row: D2R's row, with every column PD2 also has (or COLUMN_MAP maps) set to PD2's value. Columns D2R added
keep D2R's value on a D2R row and stay blank on an appended one (blank is the vanilla behaviour PD2 has), except the
DERIVED ones. itemstatcost's descfunc and descval follow the description strings (STAT_STRINGS): a row that shows a D2R
string takes the display D2R gives it. References to the P2 tables, including names inside formulas and item properties' skill and state
parameters (by name or PD2 id), are rewritten to the mod's names and ids. PD2 function numbers that D2R reuses for
its own functions move to the slots core-rules installs them in (FUNCS).
PD2's cells carry Excel's quote marks, which the classic loader skipped; they are dropped, and formulas get D2R's
spelling of negative numbers.
"""
from __future__ import annotations

import re
from pathlib import Path
from typing import NamedTuple


def toolkit(root: Path) -> Path:
    """The nearest d2rAI-toolkit beside root or one of its parents (an agent worktree sits deep inside the main
    checkout), else the one beside root."""
    return next((p / "d2rAI-toolkit" for p in root.parents if (p / "d2rAI-toolkit" / "modkit").is_dir()),
                root.parent / "d2rAI-toolkit")

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
KEY = {"itemtypes": "Code", "weapons": "code", "armor": "code", "misc": "code", "gamble": "code",
       "qualityitems": "mod1code"}  # when not the first (PD2's qualityitems starts with nummods)
# the key is the string the player sees (or, for qualityitems, a value), so PD2's wins on a merged row; elsewhere D2R's
# files name the row by D2R's key
PD2_NAMES = {"uniqueitems", "setitems", "runes", "magicprefix", "magicsuffix", "automagic", "rareprefix", "raresuffix",
             "qualityitems"}
# affix names repeat and say nothing of the row's mods, so a PD2 affix after vanilla never takes a D2R-only affix's row
NO_NAME_MATCH = {"magicprefix", "magicsuffix", "automagic", "rareprefix", "raresuffix"}
DUP_NAMES = {"uniqueitems", "runes", "magicprefix", "magicsuffix", "automagic", "rareprefix", "raresuffix",
             "lvlwarp", "objgroup", "lvlprest"}  # keys may repeat
LEFT_OUT = -1  # the id read_list gives a `-` line
_NPC = {f"{d2r}{s}": f"{pd2}{s}" for d2r, pd2 in (("Anya", "Drehya"), ("Hratli", "Hralti"))
        for s in ("Min", "Max", "MagicMin", "MagicMax", "MagicLvl")}
# D2R column -> PD2 column, where the name differs by more than letter case
COLUMN_MAP = {"skills": {"localdelay": "delay"}, "missiles": {"EMax": "Emax"}, "weapons": _NPC, "armor": _NPC,
              "misc": _NPC, "uniqueitems": {"spawnable": "enabled"},
              "itemtypes": {"MaxSockets1": "MaxSock1", "MaxSockets2": "MaxSock25", "MaxSockets3": "MaxSock40"},
              "levels": {f"MonLvl{ex}{d}": f"MonLvl{n}{ex}" for n, d in ((1, ""), (2, "(N)"), (3, "(H)")) for ex in ("", "Ex")},
              "monstats": {f"TreasureClass{k}{d}": f"TreasureClass{n}{d}" for d in ("", "(N)", "(H)")
                           for n, k in ((1, ""), (2, "Champ"), (3, "Unique"), (4, "Quest"))},
              "hireling": {f"Resist{e}{lvl}": f"Resist{lvl}" for e in ("Fire", "Cold", "Lightning", "Poison")
                           for lvl in ("", "/Lvl")}}
_SKILL_REFS = ["sumskill1", "sumskill2", "sumskill3", "sumskill4", "sumskill5", "reqskill1", "reqskill2", "reqskill3"]
_MISSILE_REFS = ["ExplosionMissile", "SubMissile1", "SubMissile2", "SubMissile3", "HitSubMissile1", "HitSubMissile2",
                 "HitSubMissile3", "HitSubMissile4", "CltSubMissile1", "CltSubMissile2", "CltSubMissile3",
                 "CltHitSubMissile1", "CltHitSubMissile2", "CltHitSubMissile3", "CltHitSubMissile4"]
# column -> referenced table, from D2R's data guide and modkit's EXPLICIT_REFS
REFS = {
    "skills": {**{c: "skills" for c in _SKILL_REFS},
               **{c: "missiles" for c in ["srvmissile", "srvmissilea", "srvmissileb", "srvmissilec", "cltmissile",
                                          "cltmissilea", "cltmissileb", "cltmissilec", "cltmissiled"]},
               **{c: "states" for c in ["aurastate", "auratargetstate", "passivestate", "State1", "State2", "State3"]},
               **{f"aurastat{i}": "itemstatcost" for i in range(1, 7)},
               **{f"passivestat{i}": "itemstatcost" for i in range(1, 6)}},
    "states": {"stat": "itemstatcost", "missile": "missiles", "skill": "skills"},
    "itemstatcost": {c: "itemstatcost" for c in ["op base", "op stat1", "op stat2", "op stat3", "maxstat"]},
    "missiles": {"Skill": "skills", **{c: "missiles" for c in _MISSILE_REFS}},
    # tables merged by pd2_tables that name rows of the four id-listed ones
    "monstats": {**{f"Skill{i}": "skills" for i in range(1, 9)}, "SkillDamage": "skills",
                 **{c: "missiles" for c in ["MissA1", "MissA2", "MissS1", "MissS2", "MissS3", "MissS4", "MissC", "MissSQ"]}},
    "monstats2": {"ResurrectSkill": "skills"},
    "charstats": {"StartSkill": "skills", **{f"Skill {i}": "skills" for i in range(1, 11)}},
    # Class and Seller name monstats rows by number, HireDesc a hiredesc code: pd2_tables renames them
    "hireling": {**{f"Skill{i}": "skills" for i in range(1, 7)}, "Class": "monster", "Seller": "monster",
                 "HireDesc": "hiredesc"},
    "monsounds": {f"CvtSk{i}": "skills" for i in range(1, 4)},
    "skilldesc": {f"descmissile{i}": "missiles" for i in range(1, 4)},
}
# columns holding another id-listed table's row number (PD2's) -> that table; levels' Warp0-7 hold lvlwarp's Id column,
# which PD2's new warps keep (pd2_levels checks they don't clash), and LevelType is lvltypes' (pd2_levels)
NUM_REFS = {"levels": {**{f"Vis{i}": "levels" for i in range(8)}, **{f"ObjGrp{i}": "objgroup" for i in range(8)}},
            "lvlprest": {"LevelId": "levels"},
            "objgroup": {f"ID{i}": "objects" for i in range(8)}}
# A misc row with PD2's map spell opens the level its `len` names (ProjectDiablo.dll evaluates the calc as the level id,
# docs/research/pd2-map-system-dll.md): it takes the mod's id, blank for a left-out level (PvP, PD2's Uber Ancients).
# Every other row's len is a duration.
MAP_SPELL = "12"
LEFT_OUT_LEVELS = "levels left out"  # an ids key: the PD2 level ids the port leaves out
# columns that hold the row's own id, written with the mod's (lvlwarp UniqueId: D2R's, the row number)
ID_COLS = {"levels": "Id", "lvlprest": "Def", "lvlwarp": "UniqueId"}
# columns a merged D2R row keeps, where the games give them different meanings (objects Token: D2R's graphics; misc
# spellicon: D2R's cursor for an item spell with a target, which PD2's -1 turns off for identify, #311)
KEEP_D2R = {"objects": {"Token"}, "misc": {"spellicon"}}
# columns D2R's file lacks but its loader reads: misc `auto prefix` (the quivers' automagic, UR6) loads at the items
# record's +0x100, as weapons' and armor's do, since D2R parses all three with one field list (0x315fd0)
ADD_COLUMNS = {"misc": ["auto prefix"]}
FORMULAS = {
    "skills": ["prgcalc1", "prgcalc2", "prgcalc3", "auralencalc", "aurarangecalc", *[f"aurastatcalc{i}" for i in range(1, 7)],
               *[f"passivecalc{i}" for i in range(1, 6)], "petmax", *[f"sumsk{i}calc" for i in range(1, 6)], "sumumod",
               "cltcalc1", "cltcalc2", "cltcalc3", "skpoints", "perdelay", "calc1", "calc2", "calc3", "calc4",
               "ToHitCalc", "DmgSymPerCalc", "EDmgSymPerCalc", "ELenSymPerCalc", "localdelay"],
    "missiles": ["SrvCalc1", "CltCalc1", "SHitCalc1", "CHitCalc1", "DmgCalc1", "Range", "DmgSymPerCalc", "EDmgSymPerCalc"],
    "skilldesc": ["ddam calc1", "ddam calc2", *[f"desccalc{ab}{i}" for i in range(1, 7) for ab in "ab"],
                  *[f"dsc2calc{ab}{i}" for i in range(1, 6) for ab in "ab"], *[f"dsc3calc{ab}{i}" for i in range(1, 8) for ab in "ab"]],
}
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
# itemstatcost display (#67): D2R's stat strings carry the value's format code ("%+d to Strength"), where PD2's descfunc
# and descval print the value before vanilla's "to Strength" (string overrides keep D2R's placeholders, pd2_strings).
# A row whose descstrpos and descstrneg name strings D2R's rows use takes those rows' descfunc and descval: its own D2R
# row's when the strings are unchanged, else the D2R row with PD2's descfunc (bytime rows), else the first.
STAT_STRINGS, STAT_DISPLAY = ("descstrpos", "descstrneg"), ("descfunc", "descval")
# PD2 function numbers that D2R gave its own functions (missile srvdo 38 flamewave, cltdo 70 vine beast death; skill
# srvdo 153 / cltdo 97, RotW's, PD2's leap; Double Throw's srvdo 154, cltdo 98, shit 30, dmg 16 echoingstrike; dmg 17
# ring of fire; Psychic Hammer's dmg 15, holy bolt's; Teleport's srvdo 155, Gust's 156, Blood Warp's 157, srvst 66, cltst 56, Desecrate's srvdo 161, melee
# splash's 163, Poison Dagger's 164, Hunger's 165 and the wall makers' 166, past D2R's table or D2R's own, joining the
# srvdo 190 router, and Desecrate's cltdo 101; Fist of the Heavens' chit 26, where D2R's differs from PD2's 1.13c
# one; Blade Fury's fragment: shit 34, D2R's echoingstrike, and chit 6, below D2R's free range): moved to the free slots core-rules installs them in
# (plugin/core-rules/skill_functions.cpp Functions[]). Charge's cltdo 112 / cltst 57 go to D2R's Charge client
# functions (37, 25), which do what PD2's do (docs/research/w16-charge.md). Combustion's srvdo 159 and Static Field's
# 160 join the router too, Combustion's cltdo 100 takes 120 (docs/research/w27-static-field-combustion.md).
# shortcut: remaps by value on every PD2 row; key it by row if a PD2 row ever means D2R's own function there
# Blessed Hammer's sdo 40 / cdo 72 are D2R's too and take 47 / 80 (docs/research/w20-blessed-hammer.md).
# The monster kits (W36, docs/research/w36-monster-kits.md): PoisonEggSpawn's cltdo 109 takes 126, TorajanBossNest's
# srvdo 181 (the finishers' prg there) joins the router, the Cyclers' srvst 67 (D2R's Frenzy start) takes 83 and the
# summon-on-hit shit 6 (D2R's leaves out PD2's flags) takes 67.
# Ormus' Robes' property func 25 (D2R's 25 is affix-rand) takes the free property slot 26 (P12,
# docs/research/p12-ormus-robes.md).
# levels' Teleport 3 (PD2's path search, research/sorceress-w26.md) is not a D2R value; 1 (allowed) stands in until MS7.
# Map items' pSpell 12 (D2R's 12 is the Resist Scroll) takes the free item spell 16 core-maps installs (MS2).
# PD2's portal operate 77 (the xactportals) takes D2R's free operate slot 60, which core-maps fills (UB2).
# The xactportals' `#1`-`#6` graphics tokens have no art in D2R (#431): they draw D2R's red portal, `PP`, as
# xactportal_red does, until their own art is ported.
FUNCS = {"properties": {f"func{i}": {"25": "26"} for i in range(1, 8)},
         "objects": {"OperateFn": {"77": "60"}, "Token": {f"#{n}": "PP" for n in range(1, 7)}},
         "misc": {"pSpell": {MAP_SPELL: "16"}},
         "levels": {"Teleport": {"3": "1"}},
         "missiles": {"pSrvDoFunc": {"38": "46", "40": "47"}, "pCltDoFunc": {"70": "78", "69": "79", "72": "80"},
                      "pSrvHitFunc": {"30": "66", "34": "69", "6": "67"},
                      "pSrvDmgFunc": {"15": "22", "16": "21", "17": "20"}, "pCltHitFunc": {"26": "71", "6": "68"}},
         "skills": {"srvdofunc": {"153": "172", "154": "183", "155": "190", "156": "190", "157": "190", "159": "190",
                                  "160": "190", "161": "190", "162": "190", "163": "190", "164": "190", "165": "190",
                                  "166": "190", "170": "190", "181": "190"},
                    "cltdofunc": {"97": "115", "98": "116", "100": "120", "101": "121", "102": "122", "112": "37",
                                  "109": "126"},
                    # PD2's srvst 68 calls slot 32, D2R's melee start (W11)
                    "srvstfunc": {"66": "80", "68": "32", "67": "83"}, "cltstfunc": {"56": "63", "57": "25"},
                    # the finishers' prg functions (W29): only these PD2 rows use these values in these columns
                    **{f"srvprgfunc{i}": {"171": "184", "172": "185", "39": "186", "139": "181"} for i in (1, 2, 3)},
                    # and their client halves (W30); D2R's 106 is Bind Demon
                    **{f"cltprgfunc{i}": {"106": "127", "107": "128"} for i in (1, 2, 3)}}}
# missiles.txt parameters a PD2 hit function reads as ids: pSrvHitFunc -> {column: the table it names}. srvhit 61
# (Poison Dagger's cloud, the throwing potions, Kanemith's blaze dot) gives the hit unit state sHitPar1 with stats
# sHitPar3 and cHitPar2.
# srvhit 67 (PD2's 6, the summon-on-hit missiles; keyed by the mod's number) spawns monstats row sHitPar1.
HIT_PARAMS = {"61": {"sHitPar1": "states", "sHitPar3": "itemstatcost", "cHitPar2": "itemstatcost"},
              "67": {"sHitPar1": "monstats"}}
# PD2's srvdo 188 / cltdo 117 (Lightning Bolt, Lightning Fury) only create srvmissile, else srvmissilea (cltmissile,
# else cltmissilea), with the arguments D2R's do dispatcher passes for srvmissile / cltmissile after any do function
# (docs/research/lightning-javelins.md). So the merge moves that missile into srvmissile / cltmissile and drops the
# function number: (function column, PD2 number, missile column, fallback column).
MISSILE_FUNCS = {"skills": [("srvdofunc", "188", "srvmissile", "srvmissilea"),
                            ("cltdofunc", "117", "cltmissile", "cltmissilea")]}
KEY_LIMIT = {"missiles": 31}  # bytes, from D2R's data guide; the other three allow 47
NEGATIVE = re.compile(r"(?<![\w)\]'.])-(\d+)")  # modkit tsv.NEGATIVE: D2R rejects 'x*-5'
FORMULA_REF = re.compile(r"\b(skill|miss|stat)\('([^']*)'")
FORMULA_TABLE = {"skill": "skills", "miss": "missiles", "stat": "itemstatcost"}


class IdError(Exception):
    pass


class Table:
    def __init__(self, path: Path, key: str | None = None):
        text = path.read_bytes().decode("latin-1")  # keeps CRLF, which read_text would turn into LF
        self.newline = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(self.newline)
        while lines and not lines[-1]:
            lines.pop()
        self.header = lines[0].split("\t")
        self.rows = [(line.split("\t") + [""] * len(self.header))[:len(self.header)] for line in lines[1:]]
        self.key = self.header.index(key) if key else 0

    def records(self) -> list[int]:
        """Row positions of the records, numbered as the game numbers them: an `Expansion` row (first column) is not one."""
        return [i for i, r in enumerate(self.rows) if r[0] != "Expansion"]

    def names(self) -> list[str]:
        return [self.rows[i][self.key] for i in self.records()]

    def text(self) -> str:
        return self.newline.join("\t".join(r) for r in [self.header, *self.rows]) + self.newline


def ident(names: list[str]) -> list[str]:
    """Names made unique for the id lists: the second 'Sturdy' is 'Sturdy#2'."""
    seen: dict[str, int] = {}
    out = []
    for n in names:
        seen[n.lower()] = seen.get(n.lower(), 0) + 1
        out.append(n if seen[n.lower()] == 1 else f"{n}#{seen[n.lower()]}")
    return out


def port_row(pd2_name: str) -> tuple[int, dict[str, str]] | None:
    """A port-added line's (row it copies, columns it sets), or None for a PD2 row's line."""
    if not pd2_name.startswith("+"):
        return None
    like, *sets = pd2_name[1:].split(" ")  # shortcut: a value can't hold a space; quote values if one ever must
    if not like.isdigit() or not all("=" in kv for kv in sets):
        raise IdError(f"port row '{pd2_name}': want '+<id> column=value ...'")
    return int(like), dict(kv.split("=", 1) for kv in sets)


def base(name: str) -> str:
    return re.sub(r"#\d+$", "", name)


def list_path(merge_dir: Path, table: str) -> Path:
    return merge_dir / f"ids-{table}.tsv"


def read_list(path: Path) -> list[tuple[str, int, str]]:
    if not path.exists():
        return []
    out = []
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("# "):  # "#2" is the second blank-named row
            parts = line.split("\t")
            if len(parts) != 3 or not (parts[1].isdigit() or parts[1] == "-"):
                raise IdError(f"{path.name} line {n}: want 'PD2 name<tab>id<tab>name' (id '-': left out)")
            out.append((parts[0], LEFT_OUT if parts[1] == "-" else int(parts[1]), parts[2]))
    return out


def seed(table: str, pd2: list[str], d2r: list[str], existing: list[tuple[str, int, str]]) -> list[tuple[str, int, str]]:
    """Lines for the PD2 rows `existing` lacks, by the rules in the module docstring."""
    listed = {p.lower() for p, _, _ in existing}
    taken = {i for _, i, _ in existing}
    names = {n.lower() for _, _, n in existing} | {n.lower() for n in d2r}
    todo = [(i, n) for i, n in enumerate(ident(pd2)) if n.lower() not in listed]
    out_name = (lambda i, j: pd2[i]) if table in PD2_NAMES else (lambda i, j: d2r[j])
    new: dict[int, tuple[str, int, str]] = {}
    for i, n in todo:
        if i < VANILLA[table] and i < len(d2r) and i not in taken:
            new[i] = (n, i, out_name(i, i))
            taken.add(i)
    by_name = {} if table in NO_NAME_MATCH else {n.lower(): i for i, n in enumerate(ident(d2r))}
    for i, n in todo:
        j = by_name.get(n.lower())
        if i not in new and j is not None and j not in taken:
            new[i] = (n, j, out_name(i, j))
            taken.add(j)
    next_id = max([len(d2r) - 1, *(i for _, i, _ in existing)]) + 1
    for i, n in todo:
        if i not in new:
            name = pd2[i] if table in DUP_NAMES or pd2[i].lower() not in names else f"{pd2[i]}_pd2"
            name = name[:KEY_LIMIT.get(table, 47)]  # a clash after cutting fails check(); name that line by hand
            new[i] = (n, next_id, name)
            names.add(name.lower())
            next_id += 1
    return [new[i] for i, _ in todo]


def check(table: str, lines: list[tuple[str, int, str]], pd2: list[str], d2r: list[str]) -> None:
    """The list covers every PD2 row once, matches D2R's names, and appends without gaps."""
    f = f"ids-{table}.tsv"
    pd2 = ident(pd2)
    pd2_names = {n.lower() for n in pd2}
    seen: dict[str, str] = {}
    for p, i, n in lines:
        if p.lower() in seen:
            raise IdError(f"{f}: PD2 row '{p}' listed twice")
        seen[p.lower()] = n
        port = port_row(p)
        if port:
            if i < len(d2r):
                raise IdError(f"{f}: port row '{p}' must take an id after D2R's rows")
            if port[0] >= i:
                raise IdError(f"{f}: port row '{p}' must copy an earlier row")
        elif p.lower() not in pd2_names:
            # shortcut: a PD2 row gone after a re-baseline fails; give its id a placeholder row when that happens
            raise IdError(f"{f}: '{p}' is not a PD2 row any more")
        if i == LEFT_OUT:
            continue
        if len(n.encode("latin-1")) > KEY_LIMIT.get(table, 47):
            raise IdError(f"{f}: '{n}' is longer than D2R's {KEY_LIMIT.get(table, 47)} bytes: give it a shorter name")
        if table not in PD2_NAMES and i < len(d2r) and d2r[i] != n:
            raise IdError(f"{f}: id {i} is D2R's '{d2r[i]}', the list says '{n}'")
    missing = [n for n in pd2 if n.lower() not in seen]
    if missing:
        raise IdError(f"{f}: PD2 rows with no id ({', '.join(missing[:5])}{', ...' if len(missing) > 5 else ''}): "
                      "run tools/pd2_merge.py --seed-ids")
    ids = [i for _, i, _ in lines if i != LEFT_OUT]
    if len(set(ids)) != len(ids):
        raise IdError(f"{f}: an id is listed twice")
    added = sorted(i for i in ids if i >= len(d2r))
    if added != list(range(len(d2r), len(d2r) + len(added))):
        raise IdError(f"{f}: appended ids must run from {len(d2r)} without gaps")
    out_names = [n.lower() for n in [*(n for _, i, n in lines if i >= len(d2r)), *d2r] if n]  # blank: unused rows
    if table not in DUP_NAMES and len(set(out_names)) != len(out_names):
        raise IdError(f"{f}: a name is used twice in the mod")


def renames(lists: dict[str, list[tuple[str, int, str]]]) -> dict[str, dict[str, str]]:
    """table -> {PD2 name (lower case): mod name} for the rows whose name changes."""
    return {t: {base(p).lower(): n for p, i, n in ls if base(p) != n and i != LEFT_OUT and not port_row(p)}
            for t, ls in lists.items()}


def fix_formula(text: str) -> str:
    """A PD2 formula in D2R's spelling: '-"min(a,b)"' -> '0-min(a,b)', 'x*-5' -> 'x*(0-5)'."""
    if re.fullmatch(r"\s*-?\d+\s*", text):
        return text
    # a minus after an operand, spaces allowed ('lvl -1'), is a subtraction, not a negative number
    text = NEGATIVE.sub(lambda m: m[0] if re.search(r"[\w)\]'.]\s*$", text[:m.start()]) else f"(0-{m[1]})", text)
    return "0" + text.lstrip() if text.lstrip().startswith("-") else text


def rewrite_formula(text: str, ren: dict[str, dict[str, str]]) -> str:
    return FORMULA_REF.sub(lambda m: f"{m[1]}('{ren[FORMULA_TABLE[m[1]]].get(m[2].lower(), m[2])}'", text)


def fix_props(header: list[str], row: list[str], ren: dict[str, dict[str, str]], ids: dict[str, dict[str, str]],
              props: dict[str, str]) -> None:
    """Skill and state parameters of item properties (code, [cube: chance,] param, min, max columns) in the mod's
    names and ids; and a cube condition's stat (op 3-26 read `param` as an itemstatcost id, D2R's data guide)."""
    def remap(table: str, v: str) -> str:
        return ids.get(table, {}).get(v, v) if v.isdigit() else ren[table].get(v.lower(), v)
    if "op" in header and "param" in header:
        op, par = header.index("op"), header.index("param")
        if row[op].isdigit() and 3 <= int(row[op]) <= 26:
            row[par] = ids.get("itemstatcost", {}).get(row[par], row[par])
    low = [c.lower() for c in header] + ["", "", ""]
    for j in range(len(header)):
        p = j + 1 if "par" in low[j + 1] else j + 2 if "chance" in low[j + 1] and "par" in low[j + 2] else None
        kind = props.get(row[j]) if p else None
        if kind in ("skill", "state"):
            row[p] = remap(kind + "s", row[p])
        elif kind == "skillrange" and p + 2 < len(header):  # skill-rand: min and max are a range of skill ids
            row[p + 1], row[p + 2] = remap("skills", row[p + 1]), remap("skills", row[p + 2])
        elif kind == "monster" and p + 2 < len(header):
            mon = ids.get("monstats", {})
            row[p + 1], row[p + 2] = mon.get(row[p + 1], row[p + 1]), mon.get(row[p + 2], row[p + 2])


# PD2 values no table defines, which D2R's loader refuses (#141): table -> (column, PD2 value) -> the row's new cells.
# 1.13c finds no row for them (D2Common's loaders read these columns as lookups; a missing string key reads as 5382,
# "An evil force"), so most keep PD2's behaviour: Deep-Wounds is deep-wounds (lookups ignore case), alternategfx is a
# 4-byte code (1.13c keeps the first 4 letters), and a skill, stat, sound, overlay or property no table has is no
# reference. PD2 bugs we fix (docs/pd2-origin-bugs.md): dblue, amu, and the two potions' "An evil force" description.
_EVIL = {"spelldesc": "", "spelldescstr": ""}  # D2R's own Antidote and Thawing Potion rows have no description
FIXES = {"magicsuffix": {("itype4", "amu"): {"itype4": "amul"}},
         "magicprefix": {("transformcolor", "dblue"): {"transformcolor": "dblu"},
                         ("mod1code", "map-mon-extra-mag"): {"mod1code": "", "mod1min": "", "mod1max": ""}},
         "misc": {("alternategfx", "bonechips"): {"alternategfx": "bone"},
                  ("alternategfx", "invmppvp"): {"alternategfx": "invm"},
                  ("stat1", "dmg-min"): {"stat1": "", "calc1": ""}, ("stat2", "dmg-max"): {"stat2": "", "calc2": ""},
                  ("spelldescstr", "ItemStatsAnti"): _EVIL, ("spelldescstr", "ItemStatsThaw"): _EVIL},
         "missiles": {("Skill", "immolationarrow"): {"Skill": ""}, ("Skill", "RadamentUnholyOrb"): {"Skill": ""},
                      ("HitSound", "firearrow firewall"): {"HitSound": ""}, ("ProgOverlay", "fireexplode"): {"ProgOverlay": ""}}}


def fix_row(table: str, header: list[str], row: list[str]) -> list[str]:
    """A PD2 row with FIXES applied (a copy), and Deep-Wounds in deep-wounds' spelling in any table of item properties."""
    row = [("deep-wounds" if v.replace('"', "") == "Deep-Wounds" else v) for v in row] if table in PROPS else list(row)
    for (col, bad), cells in FIXES.get(table, {}).items():
        if col in header and row[header.index(col)].replace('"', "") == bad:
            for c, v in cells.items():
                row[header.index(c)] = v
    return row


def spell(table: str, col: str, v: str, ren: dict[str, dict[str, str]]) -> str:
    """A PD2 cell as the mod's column `col` holds it: quotes gone, references renamed, formulas in D2R's spelling,
    function numbers moved (FUNCS)."""
    v = v.replace('"', "")
    if col in REFS.get(table, {}):
        return ren.get(REFS[table][col], {}).get(v.lower(), v)
    if col in FORMULAS.get(table, []):
        return rewrite_formula(fix_formula(v), ren)
    return FUNCS.get(table, {}).get(col, {}).get(v, v)


def fix_missile_funcs(table: str, header: list[str], row: list[str]) -> None:
    """A row with a MISSILE_FUNCS number gets the missile that function creates in the missile column instead."""
    for func, number, col, fallback in MISSILE_FUNCS.get(table, []):
        if func in header and row[header.index(func)] == number:
            f, m, a = header.index(func), header.index(col), header.index(fallback)
            row[f] = ""
            if not row[m]:
                row[m], row[a] = row[a], ""


def fix_map_level(table: str, header: list[str], row: list[str], pd2_spell: str, ids: dict[str, dict[str, str]]) -> None:
    """A map item's len (PD2 pSpell MAP_SPELL) in the mod's level ids; blank for a left-out level."""
    if table == "misc" and pd2_spell == MAP_SPELL and "len" in header:
        j = header.index("len")
        if row[j] in ids.get("levels", {}):
            row[j] = ids["levels"][row[j]]
        elif row[j] in ids.get(LEFT_OUT_LEVELS, {}):
            row[j] = ""
        else:
            raise IdError(f"misc '{row[0]}': len names PD2's level {row[j]!r}, which has no id")


def fix_hit_params(header: list[str], row: list[str], ids: dict[str, dict[str, str]]) -> None:
    """The state and stat parameters of a HIT_PARAMS function in the mod's ids."""
    if "pSrvHitFunc" not in header:
        return
    for col, t in HIT_PARAMS.get(row[header.index("pSrvHitFunc")], {}).items():
        if col in header:
            j = header.index(col)
            row[j] = ids.get(t, {}).get(row[j], row[j])


def merge_table(table: str, pd2: Table, d2r: Table, lines: list[tuple[str, int, str]],
                ren: dict[str, dict[str, str]], ids: dict[str, dict[str, str]] | None = None,
                props: dict[str, str] | None = None) -> tuple[Table, list[str]]:
    """The mod's table, and the PD2 columns it drops. ids: table -> {PD2 id: mod id}; props: property code -> the kind
    of row its parameter names (fix_props)."""
    for c in ADD_COLUMNS.get(table, ()):
        if c not in d2r.header and c in pd2.header:
            d2r.header.append(c)
            for r in d2r.rows:
                r.append("")
    src: dict[str, int] = {}
    for i, c in enumerate(pd2.header):
        if c:  # an unnamed column (weapons) matches nothing
            src.setdefault(c.lower(), i)  # a repeated column (armor's mindam, maxdam): the first holds the values
    cmap = COLUMN_MAP.get(table, {})
    cols = [(j, src.get(c.lower(), src.get(cmap.get(c, "").lower()))) for j, c in enumerate(d2r.header)]
    used = {i for _, i in cols if i is not None}
    dropped = [c for i, c in enumerate(pd2.header) if i not in used and c.lower() not in ("id", "eol")]
    pd2_rows = {p.lower(): [v.replace('"', "") for v in fix_row(table, pd2.header, pd2.rows[k])]
                for p, k in zip(ident(pd2.names()), pd2.records())}
    derived = {d2r.header.index(c): fn for c, fn in DERIVED.get(table, {}).items() if c in d2r.header}
    id_cols = [j for j, c in enumerate(d2r.header) if c.lower() == "*id" or c == ID_COLS.get(table)]
    nums = {j: NUM_REFS[table][c] for j, c in enumerate(d2r.header) if c in NUM_REFS.get(table, {})}
    keep = {j for j, c in enumerate(d2r.header) if c in KEEP_D2R.get(table, ())}
    out = [list(r) for r in d2r.rows]
    recs = d2r.records()
    shown: dict[tuple[str, ...], list[list[str]]] = {}  # D2R's description strings -> the displays D2R gives them
    if table == "itemstatcost":
        str_cols, desc_cols = ([d2r.header.index(c) for c in cs] for cs in (STAT_STRINGS, STAT_DISPLAY))
        for r in d2r.rows:
            if any(r[j] for j in str_cols):
                shown.setdefault(tuple(r[j] for j in str_cols), []).append([r[j] for j in desc_cols])
    appended: list[list[str]] = []
    for p, i, n in sorted((line for line in lines if line[1] != LEFT_OUT), key=lambda line: line[1]):
        row = out[recs[i]] if i < len(recs) else [""] * len(d2r.header)
        if i >= len(recs):
            out.append(row)
            appended.append(row)
        port = port_row(p)
        if port:
            like, sets = port
            row[:] = out[recs[like]] if like < len(recs) else appended[like - len(recs)]
            for c, v in sets.items():
                if c not in d2r.header:
                    raise IdError(f"ids-{table}.tsv: port row '{p}': {table} has no column '{c}'")
                row[d2r.header.index(c)] = v
            row[d2r.key] = n
            for j in id_cols:
                row[j] = str(i)
            continue
        pr = pd2_rows[p.lower()]
        for j, k in cols:
            if k is None or (j in keep and i < len(recs)):
                continue
            row[j] = spell(table, d2r.header[j], pr[k], ren)
            if j in nums and row[j]:
                if row[j] not in (ids or {}).get(nums[j], {}):
                    raise IdError(f"{table} '{n}': {d2r.header[j]} names PD2's {nums[j]} row {row[j]}, which has no id")
                row[j] = ids[nums[j]][row[j]]
        fix_map_level(table, d2r.header, row, pr[src["pspell"]] if "pspell" in src else "", ids or {})
        own = d2r.rows[recs[i]] if shown and i < len(recs) else None
        if own and not any(row[j] for j in str_cols) and row[desc_cols[0]] == own[desc_cols[0]]:
            # 1.13c's descfunc 27/28 hardcode their text; D2R's read it from the strings (#116: "an evil force")
            for j in str_cols:
                row[j] = own[j]
        strs =tuple(row[j] for j in str_cols) if shown else ()
        if strs in shown:
            own = d2r.rows[recs[i]] if i < len(recs) else None
            same = [[own[j] for j in desc_cols]] if own and strs == tuple(own[j] for j in str_cols) else []
            pd2_func = [d for d in shown[strs] if d[0] == row[desc_cols[0]]]
            for j, v in zip(desc_cols, (same or pd2_func or shown[strs])[0]):
                row[j] = v
        for j, fn in derived.items():
            row[j] = fn(dict(zip(pd2.header, pr)))
        fix_missile_funcs(table, d2r.header, row)
        if table == "missiles" and ids:
            fix_hit_params(d2r.header, row, ids)
        if props is not None:
            fix_props(d2r.header, row, ren, ids or {}, props)
        row[d2r.key] = n
        for j in id_cols:
            row[j] = str(i)
    merged = Table.__new__(Table)
    merged.header, merged.rows, merged.newline, merged.key = d2r.header, out, d2r.newline, d2r.key
    return merged, dropped


def follow(lines: list[tuple[str, int, str]], pd2_keys: list[str], d2r: list[str], pd2: list[str]) -> list[tuple[str, int, str]]:
    """Lines for a table whose rows are another table's row for row (objtype: objects): PD2 row k takes the id of the
    other list's line for PD2 row k, and D2R's name on a merged row."""
    at = {p.lower(): (i, n) for p, i, n in lines}
    out = []
    for key, name in zip(ident(pd2_keys), ident(pd2)):
        i, _ = at[key.lower()]
        out.append((name, i, "" if i == LEFT_OUT else d2r[i] if i < len(d2r) else base(name)))
    return out


FOLLOWERS = {"objects": "objtype"}  # written by the same rule, by follow()


def excel(root: Path) -> Path:
    return root / "global" / "excel"


def load(data: Path, table: str) -> Table:
    return Table(excel(data) / f"{table}.txt", KEY.get(table))


def lists_for(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, list[tuple[str, int, str]]]:
    lists = {}
    for t in TABLES:
        lines = read_list(list_path(merge_dir, t))
        check(t, lines, load(pd2_data, t).names(), load(d2r_data, t).names())
        lists[t] = lines
    return lists


class Context(NamedTuple):
    """What merging any table needs: the id lists, the names they change, PD2 id -> mod id, property kinds."""
    lists: dict[str, list[tuple[str, int, str]]]
    ren: dict[str, dict[str, str]]
    ids: dict[str, dict[str, str]]
    props: dict[str, str]


def context(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> Context:
    return context_of(lists_for(merge_dir, pd2_data, d2r_data), pd2_data, d2r_data)


def context_of(lists: dict[str, list[tuple[str, int, str]]], pd2_data: Path, d2r_data: Path | None = None) -> Context:
    """The Context of id lists already read (and checked). With D2R's data, monstats ids too (HIT_PARAMS)."""
    ids = {}
    for t in ("skills", "states", "itemstatcost", *{t for refs in NUM_REFS.values() for t in refs.values()}, *ID_COLS):
        if not lists[t]:  # nothing to remap (tests with no such table)
            continue
        mod = {p.lower(): i for p, i, _ in lists[t]}
        names = ident(load(pd2_data, t).names())
        ids[t] = {str(k): str(mod[p.lower()]) for k, p in enumerate(names)
                  if mod[p.lower()] != LEFT_OUT}  # a reference to a left-out row fails the merge
        if t == "levels":  # except a map's (fix_map_level)
            ids[LEFT_OUT_LEVELS] = {str(k): "" for k, p in enumerate(names) if mod[p.lower()] == LEFT_OUT}
    if d2r_data is not None and (excel(d2r_data) / "monstats.txt").is_file():
        ids["monstats"] = monstats_ids(load(pd2_data, "monstats"), load(d2r_data, "monstats"))
    return Context(lists, renames(lists), ids, property_kinds(load(pd2_data, "properties")))


def property_kinds(properties: Table) -> dict[str, str]:
    """Property code -> what its parameter names: skill (the skill-event, charged and single-skill functions 11, 19,
    22), skillrange (12, skill-rand: min-max ids), state, or monster (map-glob-add-mon-*: min-max are a monstats id)."""
    h = {c: j for j, c in enumerate(properties.header)}
    kinds = {}
    for r in properties.rows:
        func, stat = r[h["func1"]], r[h["stat1"]]
        kind = "state" if stat == "state" else "skill" if func in ("11", "19", "22") else "skillrange" if func == "12" else ""
        kind = "monster" if stat.startswith("map_glob_add_mon_") else kind
        if kind:
            kinds[r[0]] = kind
    return kinds


PROPS = {"uniqueitems", "setitems", "sets", "runes", "magicprefix", "magicsuffix", "automagic", "cubemain", "gems",
         "qualityitems"}  # tables with item properties (fix_props)


def write(table: str, merged: Table, dropped: list[str], out_data: Path) -> None:
    dest = excel(out_data) / f"{table}.txt"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(merged.text().encode("latin-1"))
    if dropped:
        print(f"  {table}: PD2 columns D2R has no place for: {', '.join(dropped)}")


def monstats_ids(pd2: Table, d2r: Table) -> dict[str, str]:
    """PD2 monstats id -> the mod's. merge/tables.json merges monstats by name: D2R's rows keep their ids, PD2-only rows
    follow D2R's last."""
    mod = {n.lower(): i for i, n in enumerate(d2r.names())}
    added = len(d2r.names())
    for n in pd2.names():
        if n.lower() not in mod:
            mod[n.lower()], added = added, added + 1
    return {str(k): str(mod[n.lower()]) for k, n in enumerate(pd2.names())}


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-<table>.tsv: write that table."""
    table = rule.stem[len("ids-"):]
    ctx = context(rule.parent, pd2_data, d2r_data)
    write(table, *merge_table(table, load(pd2_data, table), load(d2r_data, table), ctx.lists[table], ctx.ren, ctx.ids,
                              ctx.props if table in PROPS else None), out_data)
    if table in FOLLOWERS:
        f = FOLLOWERS[table]
        pd2, d2r = load(pd2_data, f), load(d2r_data, f)
        lines = follow(ctx.lists[table], load(pd2_data, table).names(), d2r.names(), pd2.names())
        write(f, *merge_table(f, pd2, d2r, lines, ctx.ren), out_data)


def seed_lists(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, int]:
    """Append lines for PD2 rows the lists lack. Returns lines added per table."""
    added = {}
    for t in TABLES:
        path = list_path(merge_dir, t)
        existing = read_list(path)
        new = seed(t, load(pd2_data, t).names(), load(d2r_data, t).names(), existing)
        if new:
            text = path.read_text("utf-8") if path.exists() else "# PD2 name\tid\tname (append-only, ADR 0003)\n"
            path.write_text(text + "".join(f"{p}\t{i}\t{n}\n" for p, i, n in new), "utf-8")
        added[t] = len(new)
    return added
