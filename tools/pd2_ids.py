"""Remapped ids: skills, states, itemstatcost and missiles merged by the append-only id lists in merge/ (ADR 0003).

merge/ids-<table>.tsv has one line per PD2 row: `PD2 name <tab> id <tab> name`, the row's id and key name in the mod.
An id at or below D2R's last row merges the PD2 row onto that D2R row and keeps D2R's name, so D2R's own files that
name it stay valid; a higher id appends the row. Lines are only ever added (`pd2_merge.py --seed-ids`), never renumbered.

Seeding a new PD2 row picks, in order:
  1. the same id, below the end of vanilla 1.13c (VANILLA), where both games hold the same vanilla row, often
     renamed on one side (PD2 reworks: Impale -> Javelin and Spear Mastery; D2R fixes: nehlithak -> nihlathak);
  2. a D2R row of the same name that no other line took (rows both games added after vanilla, e.g. MonLeap);
  3. the next free id after D2R's last row, under its PD2 name (+ "_pd2" if the mod already has that name).

Merging a row: D2R's row, with every column PD2 also has (or COLUMN_MAP maps) set to PD2's value. Columns D2R added
keep D2R's value on a D2R row and stay blank on an appended one (blank is the vanilla behaviour PD2 has), except the
DERIVED ones. References to these four tables, including names inside formulas, are rewritten to the mod's names.
PD2's cells carry Excel's quote marks, which the classic loader skipped; they are dropped, and formulas get D2R's
spelling of negative numbers.
"""
from __future__ import annotations

import re
from pathlib import Path

TABLES = ["skills", "states", "itemstatcost", "missiles"]
# rows below this id are the same vanilla 1.13c row in both games. itemstatcost: none, because both games filled
# vanilla's unused stat slots (183-213, 256-285) with different stats.
VANILLA = {"skills": 357, "states": 185, "itemstatcost": 0, "missiles": 684}
COLUMN_MAP = {"skills": {"localdelay": "delay"}, "missiles": {"EMax": "Emax"}}  # D2R column -> PD2 column
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
}
FORMULAS = {
    "skills": ["prgcalc1", "prgcalc2", "prgcalc3", "auralencalc", "aurarangecalc", *[f"aurastatcalc{i}" for i in range(1, 7)],
               *[f"passivecalc{i}" for i in range(1, 6)], "petmax", *[f"sumsk{i}calc" for i in range(1, 6)], "sumumod",
               "cltcalc1", "cltcalc2", "cltcalc3", "skpoints", "perdelay", "calc1", "calc2", "calc3", "calc4",
               "ToHitCalc", "DmgSymPerCalc", "EDmgSymPerCalc", "ELenSymPerCalc", "localdelay"],
    "missiles": ["SrvCalc1", "CltCalc1", "SHitCalc1", "CHitCalc1", "DmgCalc1", "Range", "DmgSymPerCalc", "EDmgSymPerCalc"],
}
# D2R columns computed from the PD2 row. rightskill (right-mouse list): D2R sets it to "not passive" on every class skill;
# taken-over rows need it too, since PD2 turned some vanilla skills passive (Impale -> Javelin and Spear Mastery).
DERIVED = {"skills": {"rightskill": lambda pd2: "0" if pd2.get("passive") == "1" else "1"}}
KEY_LIMIT = {"missiles": 31}  # bytes, from D2R's data guide; the other three allow 47
NEGATIVE = re.compile(r"(?<![\w)\]'.])-(\d+)")  # modkit tsv.NEGATIVE: D2R rejects 'x*-5'
FORMULA_REF = re.compile(r"\b(skill|miss|stat)\('([^']*)'")
FORMULA_TABLE = {"skill": "skills", "miss": "missiles", "stat": "itemstatcost"}


class IdError(Exception):
    pass


class Table:
    def __init__(self, path: Path):
        text = path.read_bytes().decode("latin-1")  # keeps CRLF, which read_text would turn into LF
        self.newline = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(self.newline)
        while lines and not lines[-1]:
            lines.pop()
        self.header = lines[0].split("\t")
        self.rows = [(line.split("\t") + [""] * len(self.header))[:len(self.header)] for line in lines[1:]]

    def names(self) -> list[str]:
        return [r[0] for r in self.rows]

    def text(self) -> str:
        return self.newline.join("\t".join(r) for r in [self.header, *self.rows]) + self.newline


def list_path(merge_dir: Path, table: str) -> Path:
    return merge_dir / f"ids-{table}.tsv"


def read_list(path: Path) -> list[tuple[str, int, str]]:
    if not path.exists():
        return []
    out = []
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
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
    todo = [(i, n) for i, n in enumerate(pd2) if n.lower() not in listed]
    new: dict[int, tuple[str, int, str]] = {}
    for i, n in todo:
        if i < VANILLA[table] and i < len(d2r) and i not in taken:
            new[i] = (n, i, d2r[i])
            taken.add(i)
    by_name = {n.lower(): i for i, n in enumerate(d2r)}
    for i, n in todo:
        j = by_name.get(n.lower())
        if i not in new and j is not None and j not in taken:
            new[i] = (n, j, d2r[j])
            taken.add(j)
    next_id = max([len(d2r) - 1, *(i for _, i, _ in existing)]) + 1
    for i, n in todo:
        if i not in new:
            name = n if n.lower() not in names else f"{n}_pd2"
            name = name[:KEY_LIMIT.get(table, 47)]  # a clash after cutting fails check(); name that line by hand
            new[i] = (n, next_id, name)
            names.add(name.lower())
            next_id += 1
    return [new[i] for i, _ in todo]


def check(table: str, lines: list[tuple[str, int, str]], pd2: list[str], d2r: list[str]) -> None:
    """The list covers every PD2 row once, matches D2R's names, and appends without gaps."""
    f = f"ids-{table}.tsv"
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
        if i < len(d2r) and d2r[i] != n:
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
    out_names = [n.lower() for _, i, n in lines if i >= len(d2r)] + [n.lower() for n in d2r]
    if len(set(out_names)) != len(out_names):
        raise IdError(f"{f}: a name is used twice in the mod")


def renames(lists: dict[str, list[tuple[str, int, str]]]) -> dict[str, dict[str, str]]:
    """table -> {PD2 name (lower case): mod name} for the rows whose name changes."""
    return {t: {p.lower(): n for p, _, n in ls if p != n} for t, ls in lists.items()}


def fix_formula(text: str) -> str:
    """A PD2 formula in D2R's spelling: '-"min(a,b)"' -> '0-min(a,b)', 'x*-5' -> 'x*(0-5)'."""
    if re.fullmatch(r"\s*-?\d+\s*", text):
        return text
    # a minus after an operand, spaces allowed ('lvl -1'), is a subtraction, not a negative number
    text = NEGATIVE.sub(lambda m: m[0] if re.search(r"[\w)\]'.]\s*$", text[:m.start()]) else f"(0-{m[1]})", text)
    return "0" + text.lstrip() if text.lstrip().startswith("-") else text


def rewrite_formula(text: str, ren: dict[str, dict[str, str]]) -> str:
    return FORMULA_REF.sub(lambda m: f"{m[1]}('{ren[FORMULA_TABLE[m[1]]].get(m[2].lower(), m[2])}'", text)


def spell(table: str, col: str, v: str, ren: dict[str, dict[str, str]]) -> str:
    """A PD2 cell as the mod's column `col` holds it: quotes gone, references renamed, formulas in D2R's spelling."""
    v = v.replace('"', "")
    if col in REFS.get(table, {}):
        return ren[REFS[table][col]].get(v.lower(), v)
    if col in FORMULAS.get(table, []):
        return rewrite_formula(fix_formula(v), ren)
    return v


def merge_table(table: str, pd2: Table, d2r: Table, lines: list[tuple[str, int, str]],
                ren: dict[str, dict[str, str]]) -> tuple[Table, list[str]]:
    """The mod's table, and the PD2 columns it drops."""
    src = {c: i for i, c in enumerate(pd2.header)}
    cmap = COLUMN_MAP.get(table, {})
    cols = [(j, src.get(c, src.get(cmap.get(c, "")))) for j, c in enumerate(d2r.header)]
    used = {i for _, i in cols if i is not None}
    dropped = [c for i, c in enumerate(pd2.header) if i not in used and c.lower() not in ("id", "eol")]
    pd2_rows = {r[0].lower(): [v.replace('"', "") for v in r] for r in pd2.rows}
    derived = {d2r.header.index(c): fn for c, fn in DERIVED.get(table, {}).items() if c in d2r.header}
    out = [list(r) for r in d2r.rows]
    for p, i, n in sorted(lines, key=lambda line: line[1]):
        row = out[i] if i < len(out) else [""] * len(d2r.header)
        if i >= len(out):
            out.append(row)
        pr = pd2_rows[p.lower()]
        for j, k in cols:
            if k is None:
                continue
            row[j] = spell(table, d2r.header[j], pr[k], ren)
        for j, fn in derived.items():
            row[j] = fn(dict(zip(pd2.header, pr)))
        row[0] = n
        if len(d2r.header) > 1 and d2r.header[1].lower() == "*id":
            row[1] = str(i)
    merged = Table.__new__(Table)
    merged.header, merged.rows, merged.newline = d2r.header, out, d2r.newline
    return merged, dropped


def excel(root: Path) -> Path:
    return root / "global" / "excel"


def lists_for(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, list[tuple[str, int, str]]]:
    lists = {}
    for t in TABLES:
        lines = read_list(list_path(merge_dir, t))
        check(t, lines, Table(excel(pd2_data) / f"{t}.txt").names(), Table(excel(d2r_data) / f"{t}.txt").names())
        lists[t] = lines
    return lists


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-<table>.tsv: write that table."""
    table = rule.stem[len("ids-"):]
    lists = lists_for(rule.parent, pd2_data, d2r_data)
    merged, dropped = merge_table(table, Table(excel(pd2_data) / f"{table}.txt"), Table(excel(d2r_data) / f"{table}.txt"),
                                  lists[table], renames(lists))
    dest = excel(out_data) / f"{table}.txt"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(merged.text().encode("latin-1"))
    if dropped:
        print(f"  {table}: PD2 columns D2R has no place for: {', '.join(dropped)}")


def seed_lists(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> dict[str, int]:
    """Append lines for PD2 rows the lists lack. Returns lines added per table."""
    added = {}
    for t in TABLES:
        path = list_path(merge_dir, t)
        existing = read_list(path)
        new = seed(t, Table(excel(pd2_data) / f"{t}.txt").names(), Table(excel(d2r_data) / f"{t}.txt").names(), existing)
        if new:
            text = path.read_text("utf-8") if path.exists() else "# PD2 name\tid\tname (append-only, ADR 0003)\n"
            path.write_text(text + "".join(f"{p}\t{i}\t{n}\n" for p, i, n in new), "utf-8")
        added[t] = len(new)
    return added
