"""Remapped ids: the tables a save or D2R's code reads by row number, merged by the append-only id lists in merge/
(ADR 0003): skills, states, itemstatcost and missiles (P2), item types, unique and set items, runewords and affixes (P3).

merge/ids-<table>.tsv has one line per PD2 row: `PD2 name <tab> id <tab> name`, the row's id and key name in the mod.
Ids count records as the game does (an `Expansion` row is not one); a repeated PD2 name is listed as `name#2`.
An id at or below D2R's last row merges the PD2 row onto that D2R row and keeps D2R's name, so D2R's own files that
name it stay valid (PD2_NAMES tables keep PD2's: there the key is the text the player sees); a higher id appends the
row. Lines are only ever added (`pd2_merge.py --seed-ids`), never renumbered.

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

TABLES = ["skills", "states", "itemstatcost", "missiles",  # P2
          # P3: item tables a save or D2R's code reads by row number
          "itemtypes", "uniqueitems", "setitems", "runes", "magicprefix", "magicsuffix", "automagic", "rareprefix",
          "raresuffix"]
# records below this id are the same vanilla 1.13c row in both games. itemstatcost: none, because both games filled
# vanilla's unused stat slots (183-213, 256-285) with different stats. itemtypes: D2R added warl at 67. setitems: PD2
# inserted a Bul-Kathos item at 117. runes: PD2 reordered its rows, so they match by name.
VANILLA = {"skills": 357, "states": 185, "itemstatcost": 0, "missiles": 684, "itemtypes": 67, "uniqueitems": 401,
           "setitems": 117, "runes": 0, "magicprefix": 669, "magicsuffix": 747, "automagic": 36, "rareprefix": 46,
           "raresuffix": 155}
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
DUP_NAMES = {"uniqueitems", "runes", "magicprefix", "magicsuffix", "automagic", "rareprefix", "raresuffix"}  # keys may repeat
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
    "hireling": {f"Skill{i}": "skills" for i in range(1, 7)},
    "monsounds": {f"CvtSk{i}": "skills" for i in range(1, 4)},
    "skilldesc": {f"descmissile{i}": "missiles" for i in range(1, 4)},
}
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
DERIVED = {"skills": {"rightskill": lambda pd2: "0" if pd2.get("passive") == "1" else "1"},
           "setitems": {"spawnable": lambda pd2: "1"},
           "itemtypes": {"MaxSocketsLevelThreshold1": lambda pd2: "25", "MaxSocketsLevelThreshold2": lambda pd2: "40"}}
# itemstatcost display (#67): D2R's stat strings carry the value's format code ("%+d to Strength"), where PD2's descfunc
# and descval print the value before vanilla's "to Strength" (string overrides keep D2R's placeholders, pd2_strings).
# A row whose descstrpos and descstrneg name strings D2R's rows use takes those rows' descfunc and descval: its own D2R
# row's when the strings are unchanged, else the D2R row with PD2's descfunc (bytime rows), else the first.
STAT_STRINGS, STAT_DISPLAY = ("descstrpos", "descstrneg"), ("descfunc", "descval")
# PD2 function numbers that D2R gave its own functions (missile srvdo 38 flamewave, cltdo 70 vine beast death; skill
# srvdo 153 / cltdo 97, RotW's, PD2's leap; Double Throw's srvdo 154, cltdo 98, shit 30, dmg 16 echoingstrike; dmg 17
# ring of fire; Blood Warp's srvdo 157, srvst 66, cltst 56, its do joining the srvdo 190 router): moved to the free
# slots core-rules installs them in (plugin/core-rules/skill_functions.cpp Functions[])
# shortcut: remaps by value on every PD2 row; key it by row if a PD2 row ever means D2R's own function there
FUNCS = {"missiles": {"pSrvDoFunc": {"38": "46"}, "pCltDoFunc": {"70": "78"}, "pSrvHitFunc": {"30": "66"},
                      "pSrvDmgFunc": {"16": "21", "17": "20"}},
         "skills": {"srvdofunc": {"153": "172", "154": "183", "157": "190"}, "cltdofunc": {"97": "115", "98": "116"},
                    "srvstfunc": {"66": "80"}, "cltstfunc": {"56": "63"}}}
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
            if len(parts) != 3 or not parts[1].isdigit():
                raise IdError(f"{path.name} line {n}: want 'PD2 name<tab>id<tab>name'")
            out.append((parts[0], int(parts[1]), parts[2]))
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
        if p.lower() not in pd2_names:
            # shortcut: a PD2 row gone after a re-baseline fails; give its id a placeholder row when that happens
            raise IdError(f"{f}: '{p}' is not a PD2 row any more")
        if len(n.encode("latin-1")) > KEY_LIMIT.get(table, 47):
            raise IdError(f"{f}: '{n}' is longer than D2R's {KEY_LIMIT.get(table, 47)} bytes: give it a shorter name")
        if table not in PD2_NAMES and i < len(d2r) and d2r[i] != n:
            raise IdError(f"{f}: id {i} is D2R's '{d2r[i]}', the list says '{n}'")
    missing = [n for n in pd2 if n.lower() not in seen]
    if missing:
        raise IdError(f"{f}: PD2 rows with no id ({', '.join(missing[:5])}{', ...' if len(missing) > 5 else ''}): "
                      "run tools/pd2_merge.py --seed-ids")
    ids = [i for _, i, _ in lines]
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
    return {t: {base(p).lower(): n for p, _, n in ls if base(p) != n} for t, ls in lists.items()}


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


def spell(table: str, col: str, v: str, ren: dict[str, dict[str, str]]) -> str:
    """A PD2 cell as the mod's column `col` holds it: quotes gone, references renamed, formulas in D2R's spelling,
    function numbers moved (FUNCS)."""
    v = v.replace('"', "")
    if col in REFS.get(table, {}):
        return ren[REFS[table][col]].get(v.lower(), v)
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


def merge_table(table: str, pd2: Table, d2r: Table, lines: list[tuple[str, int, str]],
                ren: dict[str, dict[str, str]], ids: dict[str, dict[str, str]] | None = None,
                props: dict[str, str] | None = None) -> tuple[Table, list[str]]:
    """The mod's table, and the PD2 columns it drops. ids: table -> {PD2 id: mod id}; props: property code -> the kind
    of row its parameter names (fix_props)."""
    src: dict[str, int] = {}
    for i, c in enumerate(pd2.header):
        if c:  # an unnamed column (weapons) matches nothing
            src.setdefault(c.lower(), i)  # a repeated column (armor's mindam, maxdam): the first holds the values
    cmap = COLUMN_MAP.get(table, {})
    cols = [(j, src.get(c.lower(), src.get(cmap.get(c, "").lower()))) for j, c in enumerate(d2r.header)]
    used = {i for _, i in cols if i is not None}
    dropped = [c for i, c in enumerate(pd2.header) if i not in used and c.lower() not in ("id", "eol")]
    pd2_rows = {p.lower(): [v.replace('"', "") for v in pd2.rows[k]] for p, k in zip(ident(pd2.names()), pd2.records())}
    derived = {d2r.header.index(c): fn for c, fn in DERIVED.get(table, {}).items() if c in d2r.header}
    id_col = next((j for j, c in enumerate(d2r.header) if c.lower() == "*id"), None)
    out = [list(r) for r in d2r.rows]
    recs = d2r.records()
    shown: dict[tuple[str, ...], list[list[str]]] = {}  # D2R's description strings -> the displays D2R gives them
    if table == "itemstatcost":
        str_cols, desc_cols = ([d2r.header.index(c) for c in cs] for cs in (STAT_STRINGS, STAT_DISPLAY))
        for r in d2r.rows:
            if any(r[j] for j in str_cols):
                shown.setdefault(tuple(r[j] for j in str_cols), []).append([r[j] for j in desc_cols])
    for p, i, n in sorted(lines, key=lambda line: line[1]):
        row = out[recs[i]] if i < len(recs) else [""] * len(d2r.header)
        if i >= len(recs):
            out.append(row)
        pr = pd2_rows[p.lower()]
        for j, k in cols:
            if k is None:
                continue
            row[j] = spell(table, d2r.header[j], pr[k], ren)
        strs = tuple(row[j] for j in str_cols) if shown else ()
        if strs in shown:
            own = d2r.rows[recs[i]] if i < len(recs) else None
            same = [[own[j] for j in desc_cols]] if own and strs == tuple(own[j] for j in str_cols) else []
            pd2_func = [d for d in shown[strs] if d[0] == row[desc_cols[0]]]
            for j, v in zip(desc_cols, (same or pd2_func or shown[strs])[0]):
                row[j] = v
        for j, fn in derived.items():
            row[j] = fn(dict(zip(pd2.header, pr)))
        fix_missile_funcs(table, d2r.header, row)
        if props is not None:
            fix_props(d2r.header, row, ren, ids or {}, props)
        row[d2r.key] = n
        if id_col is not None:
            row[id_col] = str(i)
    merged = Table.__new__(Table)
    merged.header, merged.rows, merged.newline, merged.key = d2r.header, out, d2r.newline, d2r.key
    return merged, dropped


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
    return context_of(lists_for(merge_dir, pd2_data, d2r_data), pd2_data)


def context_of(lists: dict[str, list[tuple[str, int, str]]], pd2_data: Path) -> Context:
    """The Context of id lists already read (and checked)."""
    ids = {}
    for t in ("skills", "states", "itemstatcost"):
        if not lists[t]:  # nothing to remap (tests with no such table)
            continue
        mod = {p.lower(): i for p, i, _ in lists[t]}
        ids[t] = {str(k): str(mod[p.lower()]) for k, p in enumerate(ident(load(pd2_data, t).names()))}
    return Context(lists, renames(lists), ids, property_kinds(load(pd2_data, "properties")))


def property_kinds(properties: Table) -> dict[str, str]:
    """Property code -> what its parameter names: skill (the skill-event, charged and single-skill functions 11, 19,
    22), skillrange (12, skill-rand: min-max ids) or state."""
    h = {c: j for j, c in enumerate(properties.header)}
    kinds = {}
    for r in properties.rows:
        func, stat = r[h["func1"]], r[h["stat1"]]
        kind = "state" if stat == "state" else "skill" if func in ("11", "19", "22") else "skillrange" if func == "12" else ""
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


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-<table>.tsv: write that table."""
    table = rule.stem[len("ids-"):]
    ctx = context(rule.parent, pd2_data, d2r_data)
    write(table, *merge_table(table, load(pd2_data, table), load(d2r_data, table), ctx.lists[table], ctx.ren, ctx.ids,
                              ctx.props if table in PROPS else None), out_data)


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
