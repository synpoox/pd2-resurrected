"""The table-diff gate: compare the merged tables to the PD2 baseline by table, row and column name.

    python tools/pd2_diff.py --scope P2|P3|P4|M1|alpha [--d2r DIR]

Reads PD2's tables from pd2-data/ and the mod's from generated/ (D2R's own table where generated/ has none yet).
"""
from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import pd2_ids
from pd2_ids import COLUMN_MAP, FORMULAS, REFS, Table

ROOT = Path(__file__).resolve().parent.parent
SHOWN = 200  # open deviations listed in the report; the counts cover the rest


STATUSES = ("approximated", "cut", "not yet")
# the roadmap's work items that merge tables (P2 the id-listed ones, P3 the item tables, P4 the rest); M1 and alpha all
ITEM_TABLES = {"armor", "weapons", "misc", "itemtypes", "magicprefix", "magicsuffix", "automagic", "rareprefix",
               "raresuffix", "uniqueitems", "sets", "setitems", "runes", "gems", "cubemain", "cubemod", "cubetype",
               "treasureclassex", "treasureclass", "qualityitems", "lowqualityitems", "itemratio", "gamble",
               "properties", "books", "belts", "bodylocs"}
SCOPES = {"P2": lambda t: t in pd2_ids.TABLES, "P3": lambda t: t in ITEM_TABLES,
          "P4": lambda t: t not in ITEM_TABLES and t not in pd2_ids.TABLES, "M1": lambda t: True, "alpha": lambda t: True}


class GateError(Exception):
    pass


@dataclass
class Result:
    table: str
    open: list[tuple[str, str, str]] = field(default_factory=list)  # (table.row.column, PD2 value, mod value)
    covered: int = 0  # deviations the unported list names
    source: str = "merged"  # the mod's table: merged (generated/), D2R (not merged yet) or none


@dataclass
class Report:
    results: list[Result]

    @property
    def ok(self) -> bool:
        return not self.open()

    def open(self) -> list[tuple[str, str, str]]:
        return [d for r in self.results for d in r.open]


def same(a: str, b: str) -> bool:
    return a == b or {a, b} == {"", "0"}  # the loader reads a blank number as 0


def keyed(rows: list[list[str]]) -> dict[str, list[str]]:
    """Rows by lower-case name; a repeated name (Expansion, cube recipes) gets #2, #3, ... in order."""
    out: dict[str, list[str]] = {}
    for r in rows:
        k, n = r[0].lower(), 1
        while (k if n == 1 else f"{k}#{n}") in out:
            n += 1
        out[k if n == 1 else f"{k}#{n}"] = r
    return out


def spelled(table: str, col: str, v: str, ren: dict[str, dict[str, str]]) -> str:
    """A PD2 cell as the P2 merge writes it into the mod's column `col`: quotes gone, renamed references and formulas
    in D2R's spelling (pd2_ids.merge_table)."""
    v = v.replace('"', "")
    if col in REFS.get(table, {}):
        return ren[REFS[table][col]].get(v.lower(), v)
    if col in FORMULAS.get(table, []):
        return pd2_ids.rewrite_formula(pd2_ids.fix_formula(v), ren)
    return v


def compare(name: str, pd2: Table, mod: Table, ren: dict[str, dict[str, str]] | None = None) -> Result:
    """Deviations of `mod` from `pd2`: missing columns (table.*.col), missing rows (table.row.*), changed cells.
    PD2's comment columns and eol are not data; a column D2R keeps only as a comment (*Id) is the row's position.
    With `ren` (pd2_ids.renames, for the P2 tables) PD2's rows, columns and cells are read in the mod's spelling."""
    res = Result(name)
    mod_cols = {c.lower(): i for i, c in enumerate(mod.header)}
    to_mod = {p.lower(): d.lower() for d, p in COLUMN_MAP.get(name, {}).items()} if ren else {}
    cols = []
    for i, c in enumerate(pd2.header):
        mc = to_mod.get(c.lower(), c.lower())
        if c.startswith("*") or c.lower() == "eol" or f"*{mc}" in mod_cols:
            continue
        if mc in mod_cols:
            if i:  # column 0 is the row name, matched as the key
                cols.append((c, i, mod_cols[mc]))
        else:
            res.open.append((f"{name}.*.{c}", "", ""))
    rows = ren.get(name, {}) if ren else {}
    mod_rows = keyed(mod.rows)
    for k, pr in keyed(pd2.rows).items():
        row = pr[0] + k[len(pr[0]):]  # keeps PD2's spelling, plus any #n
        mr = mod_rows.get(rows.get(pr[0].lower(), pr[0]).lower() + k[len(pr[0]):])
        if mr is None:
            res.open.append((f"{name}.{row}.*", "", ""))
            continue
        res.open += [(f"{name}.{row}.{c}", pr[i], mr[j]) for c, i, j in cols
                     if not same(spelled(name, mod.header[j], pr[i], ren) if ren else pr[i], mr[j])]
    return res


def unported(path: Path) -> list[str]:
    """The Gate patterns of docs/unported.md (`table.row.column`, fnmatch, lower case). Fails on a row whose status is
    not one of STATUSES."""
    patterns, cols = [], None
    for line in path.read_text("utf-8").splitlines():
        if not line.startswith("|") or set(line) <= set("|-: "):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if "Gate" in cells and "Status" in cells:
            cols = cells.index("Status"), cells.index("Gate")
            continue
        if cols is None:
            continue
        if cells[cols[0]] not in STATUSES:
            raise GateError(f"unported.md: status {cells[cols[0]]!r} is not one of {', '.join(STATUSES)}: {line}")
        patterns += [p.lower() for p in re.findall(r"`([^`]+)`", cells[cols[1]])]
    return patterns


def triage(res: Result, patterns: list[str]) -> Result:
    """Moves the deviations the unported list names from open to covered."""
    kept = [d for d in res.open if not any(fnmatch.fnmatchcase(d[0].lower(), p) for p in patterns)]
    res.covered += len(res.open) - len(kept)
    res.open = kept
    return res


def gate(root: Path, d2r: Path, scope: str) -> Report:
    if scope not in SCOPES:
        raise GateError(f"scope {scope!r} is not one of {', '.join(SCOPES)}")
    pd2_excel, mod_excel = root / "pd2-data/data/global/excel", root / "generated/data/global/excel"
    patterns = unported(root / "docs" / "unported.md")
    ren = pd2_ids.renames({t: pd2_ids.read_list(pd2_ids.list_path(root / "merge", t)) for t in pd2_ids.TABLES})
    results = []
    for p in sorted(p for p in pd2_excel.glob("*.txt") if SCOPES[scope](p.stem.lower())):
        src = next(((s, f / p.name) for s, f in (("merged", mod_excel), ("D2R", d2r / "global" / "excel"))
                    if (f / p.name).is_file()), None)
        if src is None:
            res = Result(p.stem, [(f"{p.stem}.*.*", "", "")], source="none")
        else:
            res = compare(p.stem, Table(p), Table(src[1]), ren if p.stem in pd2_ids.TABLES else None)
            res.source = src[0]
        results.append(triage(res, patterns))
    return Report(results)


def cell(v: str) -> str:
    return v.replace("|", r"\|")


def report(rep: Report, scope: str) -> str:
    """The gate's result as markdown, for pasting into the PR."""
    opened = rep.open()
    lines = [f"Table-diff gate, scope {scope}: {'pass' if rep.ok else 'FAIL'}, {len(opened)} open, "
             f"{sum(r.covered for r in rep.results)} on the unported list", "",
             "| Table | Mod table | Open | Unported |", "|---|---|---|---|"]
    lines += [f"| {r.table} | {r.source} | {len(r.open)} | {r.covered} |" for r in rep.results]
    if opened:
        lines += ["", "| Open deviation | PD2 | Mod |", "|---|---|---|"]
        lines += [f"| `{k}` | {cell(a)} | {cell(b)} |" for k, a, b in opened[:SHOWN]]
        if len(opened) > SHOWN:
            lines += ["", f"{len(opened) - SHOWN} more not shown."]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scope", required=True, choices=SCOPES)
    ap.add_argument("--root", type=Path, default=ROOT)
    ap.add_argument("--d2r", type=Path, help="D2R data root (default: pd2_merge's)")
    a = ap.parse_args(argv)
    if a.d2r is None:
        import pd2_merge
        a.d2r = pd2_merge.D2R_DATA
    try:
        rep = gate(a.root, a.d2r, a.scope)
    except (GateError, pd2_ids.IdError) as e:
        print(f"pd2_diff: {e}", file=sys.stderr)
        return 2
    print(report(rep, a.scope))
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
