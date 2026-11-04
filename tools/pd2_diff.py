"""The table-diff gate: compare the merged tables to the PD2 baseline by table, row and column name.

    python tools/pd2_diff.py --scope P2|P3|P4|M1|alpha [--root DIR] [--d2r DIR]

Reads PD2's tables from pd2-data/ and the mod's from generated/ (D2R's own table where generated/ has none yet).
Exit 0: every deviation is on docs/unported.md; 1: some are not (listed in the report); 2: the list or an id list is
broken.
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import pd2_ids
import pd2_merge
from pd2_ids import COLUMN_MAP, Table
from pd2_tables import row_keys

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
    entries: dict[str, int] = field(default_factory=dict)  # unported.md entries by status, whatever the scope

    @property
    def ok(self) -> bool:
        return not self.open()

    def open(self) -> list[tuple[str, str, str]]:
        return [d for r in self.results for d in r.open]


def same(a: str, b: str) -> bool:
    return a == b or {a, b} == {"", "0"}  # the loader reads a blank number as 0


def keyed(rows: list[list[str]], key: int = 0) -> dict[str, list[str]]:
    """Rows by lower-case key (Excel's quote marks dropped); a repeated key (Expansion) gets #2, #3, ... in order."""
    out: dict[str, list[str]] = {}
    for r in rows:
        k, n = r[key].replace('"', "").lower(), 1
        while (k if n == 1 else f"{k}#{n}") in out:
            n += 1
        out[k if n == 1 else f"{k}#{n}"] = r
    return out


def pairs(name: str, pd2: Table, mod: Table, ren: dict[str, dict[str, str]] | None,
          lists: dict[str, list[tuple[str, int, str]]], rows: str | list[str] = "name"
          ) -> list[tuple[str, list[str], list[str] | None]]:
    """(row label, PD2 row, mod row or None) as the merge placed them: an item table with an id list by its list, one
    merged by row by position, a tables.json table by its `rows` (pd2_tables.row_keys), the rest by key."""
    if rows != "name":
        mod_rows = {k.lower(): r for k, r in zip(row_keys(mod, rows), mod.rows)}
        return [(k, pr, mod_rows.get(k.lower())) for k, pr in zip(row_keys(pd2, rows), pd2.rows)]
    if name in pd2_ids._BY_ROW or (name in ITEM_TABLES and lists.get(name)):
        where = ({p.lower(): i for p, i, _ in lists[name]} if name not in pd2_ids._BY_ROW else
                 {p.lower(): k for k, p in enumerate(pd2_ids.ident(pd2.names()))})
        recs = mod.records()
        return [(p, pd2.rows[k], mod.rows[recs[where[p.lower()]]] if where.get(p.lower(), len(recs)) < len(recs)
                 else None) for p, k in zip(pd2_ids.ident(pd2.names()), pd2.records())]
    row_ren = ren.get(name, {}) if ren else {}
    mod_rows = keyed(mod.rows, mod.key)
    out = []
    for k, pr in keyed(pd2.rows, pd2.key).items():
        name_ = pr[pd2.key].replace('"', "")
        repeat = k[len(name_):]  # "#n" on a repeated name
        out.append((name_ + repeat, pr, mod_rows.get(row_ren.get(name_.lower(), name_).lower() + repeat)))  # PD2's spelling
    return out


def compare(name: str, pd2: Table, mod: Table, ren: dict[str, dict[str, str]] | None = None,
            lists: dict[str, list[tuple[str, int, str]]] | None = None, ctx: pd2_ids.Context | None = None,
            rows: str | list[str] = "name") -> Result:
    """Deviations of `mod` from `pd2`: missing columns (table.*.col), missing rows (table.row.*), changed cells.
    PD2's comment columns and eol are not data; a column D2R keeps only as a comment (*Id) is the row's position, and a
    repeated column is read once, as the merge reads it. PD2's rows, columns and cells are read in the mod's spelling:
    renamed by `ren` (pd2_ids.renames) and pd2_ids.COLUMN_MAP, with item properties' skill and state parameters in the
    mod's ids (`ctx`). `rows` pairs a tables.json table's rows as its merge did. PD2's cells carry Excel's quote marks, which the classic loader skipped: they are not data."""
    res = Result(name)
    mod_cols = {c.lower(): i for i, c in enumerate(mod.header)}
    to_mod = {p.lower(): d.lower() for d, p in COLUMN_MAP.get(name, {}).items()}
    cols, seen = [], set()
    for i, c in enumerate(pd2.header):
        mc = to_mod.get(c.lower(), c.lower())
        if c.startswith("*") or c.lower() == "eol" or f"*{mc}" in mod_cols or (c and c.lower() in seen):
            continue
        seen.add(c.lower())
        if mc in mod_cols:
            if i != pd2.key or rows != "name":  # the key column is the row's name, matched as the key
                cols.append((c, i, mod_cols[mc]))
        else:
            res.open.append((f"{name}.*.{c}", "", ""))
    for row, pr, mr in pairs(name, pd2, mod, ren, lists or {}, rows):
        if mr is None:
            res.open.append((f"{name}.{row}.*", "", ""))
            continue
        want = list(mr)
        for _, i, j in cols:
            want[j] = pd2_ids.spell(name, mod.header[j], pr[i], ren) if ren else pr[i].replace('"', "")
        if ctx:
            pd2_ids.fix_props(mod.header, want, ctx.ren, ctx.ids, ctx.props)
        res.open += [(f"{name}.{row}.{c}", pr[i], mr[j]) for c, i, j in cols if not same(want[j], mr[j])]
    return res


def unported(path: Path) -> tuple[list[str], dict[str, int]]:
    """The Gate patterns of docs/unported.md (`table.row.column`, fnmatch, lower case) and its entry count by status.
    Fails on a row whose status is not one of STATUSES."""
    patterns, cols, entries = [], None, dict.fromkeys(STATUSES, 0)
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
        entries[cells[cols[0]]] += 1
        patterns += [p.lower() for p in re.findall(r"`([^`]+)`", cells[cols[1]])]
    return patterns, entries


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
    patterns, entries = unported(root / "docs" / "unported.md")
    lists = {t: pd2_ids.read_list(pd2_ids.list_path(root / "merge", t)) for t in pd2_ids.TABLES}
    ren, ctx = pd2_ids.renames(lists), None
    tables = root / "merge" / "tables.json"
    opts = json.loads(tables.read_text("utf-8")) if tables.is_file() else {}
    results = []
    for p in sorted(p for p in pd2_excel.glob("*.txt") if SCOPES[scope](p.stem.lower())):
        src = next(((s, f / p.name) for s, f in (("merged", mod_excel), ("D2R", d2r / "global" / "excel"))
                    if (f / p.name).is_file()), None)
        if src is None:
            res = Result(p.stem, [(f"{p.stem}.*.*", "", "")], source="none")
        else:
            if p.stem in pd2_ids.PROPS and ctx is None:
                ctx = pd2_ids.context_of(lists, root / "pd2-data/data")
            key = pd2_ids.KEY.get(p.stem)
            rows = opts.get(p.stem, {}).get("rows", "name") if src[0] == "merged" else "name"
            res = compare(p.stem, Table(p, key), Table(src[1], key), ren, lists,
                          ctx if p.stem in pd2_ids.PROPS else None, rows)
            res.source = src[0]
            if src[0] == "merged" and f"{p.stem.lower()}.*" in patterns:
                raise GateError(f"unported.md still covers all of {p.stem}, which is merged now: narrow its row")
        results.append(triage(res, patterns))
    return Report(results, entries)


def escape_pipes(v: str) -> str:
    return v.replace("|", r"\|")


def report(rep: Report, scope: str) -> str:
    """The gate's result as markdown, for pasting into the PR."""
    opened = rep.open()
    lines = [f"Table-diff gate, scope {scope}: {'pass' if rep.ok else 'FAIL'}, {len(opened)} open, "
             f"{sum(r.covered for r in rep.results)} table cells on the unported list",
             "Unported list: " + ", ".join(f"{rep.entries.get(s, 0)} {s}" for s in ("not yet", "approximated", "cut")) + " entries "
             "(the cell count is large because one entry can cover a whole table)", "",
             "| Table | Mod table | Open | Unported |", "|---|---|---|---|"]
    lines += [f"| {r.table} | {r.source} | {len(r.open)} | {r.covered} |" for r in rep.results]
    if opened:
        lines += ["", "| Open deviation | PD2 | Mod |", "|---|---|---|"]
        lines += [f"| `{k}` | {escape_pipes(a)} | {escape_pipes(b)} |" for k, a, b in opened[:SHOWN]]
        if len(opened) > SHOWN:
            lines += ["", f"{len(opened) - SHOWN} more not shown."]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scope", required=True, choices=SCOPES)
    ap.add_argument("--root", type=Path, default=ROOT, help="repo root holding pd2-data/, generated/ and docs/")
    ap.add_argument("--d2r", type=Path, default=pd2_merge.D2R_DATA, help="D2R data root (default: %(default)s)")
    a = ap.parse_args(argv)
    try:
        rep = gate(a.root, a.d2r, a.scope)
    except (GateError, pd2_ids.IdError) as e:
        print(f"pd2_diff: {e}", file=sys.stderr)
        return 2
    print(report(rep, a.scope))
    return 0 if rep.ok else 1


if __name__ == "__main__":
    sys.exit(main())
