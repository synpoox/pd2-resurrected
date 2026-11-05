"""Stand-ins: unbuilt engine rules point at the closest D2R function (W3, core-rules).

merge/standins.tsv has one line per row: `table <tab> row <tab> from <tab> columns <tab> rule`. It sets the listed
function columns of a merged row (its mod name) from `from`: `=` is D2R's own row at that id, a name is that D2R row,
`-` blanks them. `rule` names the core-rules row it stands in for; each is a "not yet" row on docs/unported.md. Delete
a row's line when its rule is built.
"""
from __future__ import annotations

from pathlib import Path

from pd2_ids import Table, excel


def _nums(*spans: int | range) -> set[str]:
    return {str(n) for s in spans for n in (s if isinstance(s, range) else [s])}


# Function numbers PD2 rows must not keep: D2R's slot is empty, past the table's end, or holds a different function
# (core-rules hook list; table sizes from docs/research/skill-function-tables.md). Not listed: numbers equal to D2R's
# (srvdo 41/143, cltdo 15/83), built rules (srvdo 189, W1; srvdo 188 and cltdo 117 become the missile columns,
# pd2_ids.MISSILE_FUNCS, W5; shit 63, cltdo 22 patched, W6; shit 61, W11; srvdo 182 / cltdo 111, W21; srvdo 176,
# chit 66, W17; cltdo 119, W22; srvdo 174 and 162, W18; chit 67, W30; srvdo 175, cltdo 110, srvst 77, W24; shit 62,
# W28; srvdo 178, W31; sdo 40 / cdo 72 move to 47 / 80, W20, like sdo 38 / cdo 70, W2; shit 34, chit 6, dmg 19, W32;
# srvst 73 and 75, W19; srvdo 173, W36; auraeventfunc 16 is D2R's crushing blow, Smite's roll is on srvdo 150, #165), `verify` slots that hold D2R's
# same-numbered function, which is the stand-in (shit 5, 30; chit 4, 12), the monster kits' cdo 12 (D2R's is 1.13c's
# screen shake, its length an approximated row) and chit 11 (empty in both games, W36), and temp1's sdo 39 / cdo 71 (no row
# reaches it). Cube ops 30/31 are not here: cubemain rows have no unique name, so the item tables' merge (P3) keeps
# those recipes off.
PD2_ONLY = {
    "skills": {
        "srvdofunc": _nums(109, 153, 155, 156, range(159, 162), 164, 165, 167, range(169, 171), 177, 179, 181, range(184, 187)),
        "cltdofunc": _nums(97, range(100, 104), 105, 109, 113, 114, 118),
        "srvstfunc": _nums(64, range(67, 70), 71, 74, 76, 79),
        "cltstfunc": _nums(54, 58, 82),
        **{f"srvprgfunc{i}": _nums(171, 172) for i in (1, 2, 3)},
        **{f"cltprgfunc{i}": _nums(106, 107) for i in (1, 2, 3)},
    },
    "missiles": {
        "pSrvDoFunc": _nums(38, 40),
        "pCltDoFunc": _nums(range(69, 71), 72),
        "pSrvHitFunc": _nums(60, 64, 65) | {"*12"},
        "pCltHitFunc": _nums(65) | {"*16"},  # *12/*16: PD2 text that parses as no function
        "pSrvDmgFunc": _nums(15, 17, 18),
    },
    "misc": {"pSpell": _nums(12, 13)},
    "properties": {f"func{i}": _nums(25) for i in range(1, 8)},
    "states": {"remfunc": _nums(12)},
    "objects": {"OperateFn": _nums(range(75, 81))},  # PD2's portals, altars and tomes (LP1)
}


class StandinError(ValueError):
    pass


def apply_lines(table: str, mod: Table, d2r: Table, lines: list[tuple[str, str, list[str], str]]) -> None:
    """Set each line's columns in the merged table `mod`."""
    rows = {r[0].lower(): r for r in mod.rows}
    d2r_rows = {r[0].lower(): r for r in d2r.rows}
    for name, src, cols, _rule in lines:
        row = rows.get(name.lower())
        if row is None:
            raise StandinError(f"{table}: no row '{name}' in the merged table")
        n = sum(r[0].lower() == name.lower() for r in mod.rows)
        if n > 1:
            raise StandinError(f"{table}: '{name}' names {n} rows")
        if src == "=":
            # shortcut: matches D2R's row by position and fails on a name mismatch; name the D2R row if a table's order drifts
            i = mod.rows.index(row)
            if i >= len(d2r.rows) or d2r.rows[i][0].lower() != name.lower():
                raise StandinError(f"{table}: '{name}' is not a D2R row, so '=' has nothing to copy: name a D2R row")
            from_row = d2r.rows[i]
        elif src == "-":
            from_row = None
        elif src.lower() in d2r_rows:
            from_row = d2r_rows[src.lower()]
        else:
            raise StandinError(f"{table}: no D2R row '{src}'")
        for c in cols:
            if c not in mod.header or c not in d2r.header:
                raise StandinError(f"{table}: no column '{c}'")
            row[mod.header.index(c)] = from_row[d2r.header.index(c)] if from_row else ""


def check(table: str, mod: Table, d2r: Table) -> None:
    """No PD2 row keeps a PD2_ONLY number. A D2R row with its own value is not PD2's (the Warlock's 155 Death Mark)."""
    left = []
    for c, bad in PD2_ONLY.get(table, {}).items():
        if c not in mod.header:
            continue
        if c not in d2r.header:
            raise StandinError(f"{table}: no column '{c}' in D2R's table")
        j, k = mod.header.index(c), d2r.header.index(c)
        for i, r in enumerate(mod.rows):
            if r[j] in bad and not (i < len(d2r.rows) and d2r.rows[i][k] == r[j]):
                left.append(f"{r[0]} {c} {r[j]}")
    if left:
        raise StandinError(f"{table}: {', '.join(left)}: no stand-in; add a merge/standins.tsv line")


def read_lines(path: Path) -> dict[str, list[tuple[str, str, list[str], str]]]:
    """table -> lines of merge/standins.tsv."""
    out: dict[str, list[tuple[str, str, list[str], str]]] = {}
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if line and not line.startswith("#"):
            parts = line.split("\t")
            if len(parts) != 5 or not all(parts):
                raise StandinError(f"line {n}: want 'table<tab>row<tab>from<tab>columns<tab>rule'")
            t, row, src, cols, rule = parts
            out.setdefault(t, []).append((row, src, [c.strip() for c in cols.split(",")], rule))
    return out


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/standins.tsv: edit the tables the id and item rules wrote, then check them."""
    lines = read_lines(rule)
    if unknown := sorted(set(lines) - set(PD2_ONLY)):
        raise StandinError(f"no stand-ins for table {', '.join(unknown)}: add it to PD2_ONLY")
    for t in sorted(set(lines) | set(PD2_ONLY)):
        path = excel(out_data) / f"{t}.txt"
        if not path.is_file():  # D2R's own table is built, so no PD2 row needs a stand-in yet
            if t in lines:
                print(f"  standins: {t}.txt is not merged yet; its {len(lines[t])} lines wait for the rule that merges it")
            continue
        mod, d2r = Table(path), Table(excel(d2r_data) / f"{t}.txt")
        apply_lines(t, mod, d2r, lines.get(t, []))
        check(t, mod, d2r)
        path.write_bytes(mod.text().encode("latin-1"))
