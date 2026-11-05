"""The tables with no id list (P3 (https://github.com/synpoox/pd2-resurrected/issues/47), P4 (https://github.com/synpoox/pd2-resurrected/issues/48)): merge/tables.json names each table the merge takes over from D2R.

    {"<table>": {"rows": "name" | "position" | [column, ...], "pd2_only_rows": "append" | "leave out",
                 "repeats": "pair" | "replace group", "d2r_columns": [column, ...]}, ...}   (defaults first)

A PD2 row merges onto the D2R row of the same name (lower case; a repeated name pairs by its order, #1, #2, ...):
D2R's columns take PD2's value where PD2 has the column (or pd2_ids.COLUMN_MAP maps it), and keep D2R's value
otherwise, comment columns (*...) included. D2R-only rows keep their place and numbers. PD2-only rows append after
D2R's last row, D2R-only columns blank, so extra rows that collide with D2R's (events) port by name; "leave out" drops
them (new PD2-only areas are not yet in the port). "rows": "position" pairs rows by their place instead, for tables
whose row number is the id and whose name columns differ between the games (shrines); a list of columns
pairs rows by those columns' values (hireling: Version, Id, Difficulty, Level; objects: Name). "move": {column: {PD2
value: mod value}} renumbers PD2's rows before pairing, for ids D2R gave other rows (hireling 30-38, https://github.com/synpoox/pd2-resurrected/issues/201); a moved row
appends even under "leave out" and takes PD2's d2r_columns too, as D2R has no row of that meaning. "repeats": "replace group" puts PD2's
whole group of a repeated name where D2R's group was, for lists the game reads in order (monequip: a monster's gear by
level). "d2r_columns" keep D2R's values where the games give a column different meanings (objects Token: D2R's
graphics). Cells are spelled as pd2_ids.spell writes them: quotes gone, references to the id-listed tables renamed.
skilldesc's description lines then move onto D2R's line functions (pd2_skilldesc).
"""
from __future__ import annotations

import json
from pathlib import Path

import pd2_ids
import pd2_skilldesc
from pd2_ids import Table

OPTIONS = {"rows": ("name", "position"), "pd2_only_rows": ("append", "leave out"), "repeats": ("pair", "replace group")}


def row_keys(t: Table, rows: str | list[str] = "name") -> list[str]:
    """Each row's key, as the merge and the gate pair rows: its name, its number, or its key columns' values joined
    by "/"; a repeated key gets #2, #3, ... in order. Compare keys in lower case."""
    # shortcut: repeats pair by order, so one inserted repeat shifts the rest; key cube rows by content when P3 (https://github.com/synpoox/pd2-resurrected/issues/47) merges cubemain
    if rows == "position":
        return [str(i) for i in range(len(t.rows))]
    ix = [0] if rows == "name" else [[c.lower() for c in t.header].index(c.lower()) for c in rows]
    out, seen = [], set()
    for r in t.rows:
        k, n = "/".join(r[i] for i in ix), 1
        while (key := k if n == 1 else f"{k}#{n}").lower() in seen:
            n += 1
        seen.add(key.lower())
        out.append(key)
    return out


def regroup(pd2: Table, d2r: Table) -> Table:
    """D2R's table with each group of a name PD2 also has cut or padded (blank rows) to PD2's length, in place."""
    want = {}
    for r in pd2.rows:
        want[r[0].lower()] = want.get(r[0].lower(), 0) + 1
    out, done = [], set()
    for r in d2r.rows:
        k = r[0].lower()
        if k not in want:
            out.append(r)
        elif k not in done:
            done.add(k)
            group = [g for g in d2r.rows if g[0].lower() == k][:want[k]]
            out += group + [[r[0]] + [""] * (len(d2r.header) - 1) for _ in range(want[k] - len(group))]
    t = Table.__new__(Table)
    t.header, t.rows, t.newline = d2r.header, out, d2r.newline
    return t


def merge_table(table: str, src: Table, d2r: Table, ren: dict[str, dict[str, str]],
                rows: str | list[str] = "name", pd2_only_rows: str = "append", repeats: str = "pair",
                d2r_columns: tuple[str, ...] = (), move: dict[str, dict[str, str]] | None = None) -> Table:
    """D2R's table with PD2's rows merged in, by the table's tables.json options (see the module docstring)."""
    pd2, pd2_cols = src, {}
    for i, c in enumerate(pd2.header):
        if c:  # an unnamed column matches nothing; a repeated column's first holds the values
            pd2_cols.setdefault(c.lower(), i)
    cmap = {d.lower(): p.lower() for d, p in pd2_ids.COLUMN_MAP.get(table, {}).items()}
    keep = {c.lower() for c in d2r_columns}
    cols = [(j, None if c.startswith("*") else pd2_cols.get(cmap.get(c.lower(), c.lower())), c.lower() in keep)
            for j, c in enumerate(d2r.header)]
    moved = set()
    if move:
        pd2 = Table.__new__(Table)
        pd2.header, pd2.rows, pd2.newline = list(src.header), [list(r) for r in src.rows], src.newline
        for c, m in move.items():
            j = [h.lower() for h in pd2.header].index(c.lower())
            for i, r in enumerate(pd2.rows):
                if r[j] in m:
                    r[j] = m[r[j]]
                    moved.add(i)
    if repeats == "replace group":
        d2r = regroup(pd2, d2r)
    out = [list(r) for r in d2r.rows]
    at = {k.lower(): i for i, k in enumerate(row_keys(d2r, rows))}
    for i, k in enumerate(row_keys(pd2, rows)):
        if k.lower() in at:
            row = out[at[k.lower()]]
        elif pd2_only_rows == "leave out" and i not in moved:
            continue
        else:
            row = [""] * len(d2r.header)
            out.append(row)
        for j, s, kept in cols:
            if s is not None and (not kept or i in moved):
                row[j] = pd2_ids.spell(table, d2r.header[j], pd2.rows[i][s], ren)
    merged = Table.__new__(Table)
    merged.header, merged.rows, merged.newline = d2r.header, out, d2r.newline
    return merged


def monster_numbers(pd2_data: Path, out_data: Path) -> dict[str, str]:
    """PD2's monster number -> the merged monstats' number of the same monster (the game skips `Expansion` rows)."""
    files = [pd2_ids.excel(d) / "monstats.txt" for d in (pd2_data, out_data)]
    if not files[0].is_file():  # no PD2 monstats (the tool tests): PD2's numbers stand
        return {}
    if not files[1].is_file():
        raise ValueError("hireling names monstats rows: merge monstats (merge/tables.json) too")
    pd2 = Table(files[0]).names()
    merged = {n.lower(): i for i, n in enumerate(Table(files[1]).names())}
    return {str(i): str(merged[n.lower()]) for i, n in enumerate(pd2) if n.lower() in merged}


def hire_descs(pd2_data: Path) -> dict[str, str]:
    """PD2's hiredesc code -> its description (English)."""
    f = pd2_ids.excel(pd2_data) / "hiredesc.txt"
    return {r[1].lower(): r[0] for r in Table(f).rows if r[1]} if f.is_file() else {}


def hire_keys(pd2_data: Path, d2r_data: Path) -> dict[str, str]:
    """PD2's hiredesc code -> the string key a moved hireling row names: D2R's hire string of the same English
    (strhirespecial1, Fire Arrow), else hiredesc_<code>, whose text pd2_strings reads from PD2's table."""
    ui = d2r_data / "local/lng/strings/ui.json"
    d2r = {}
    if ui.is_file():
        d2r = {e["enUS"]: e["Key"] for e in json.loads(ui.read_text("utf-8-sig")) if e["Key"].startswith("strhirespecial")}
    return {code: d2r.get(text, f"hiredesc_{code}") for code, text in hire_descs(pd2_data).items()}


def rule(rule_file: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/tables.json."""
    cfg = json.loads(rule_file.read_text("utf-8"))
    ren = pd2_ids.renames({t: pd2_ids.read_list(pd2_ids.list_path(rule_file.parent, t)) for t in pd2_ids.TABLES})
    for table, opts in sorted(cfg.items(), key=lambda kv: kv[0] == "hireling"):  # after monstats: it names its rows
        if table == "hireling":
            ren.update(monster=monster_numbers(pd2_data, out_data), hiredesc=hire_keys(pd2_data, d2r_data))
        bad = [f"{k}: {v}" for k, v in opts.items() if v not in OPTIONS.get(k, ()) and not (k in ("rows", "d2r_columns") and isinstance(v, list))
               and not (k == "move" and isinstance(v, dict))]
        if bad:
            raise ValueError(f"{table}: unknown option {', '.join(bad)} (options: {OPTIONS})")
        d2r = Table(pd2_ids.excel(d2r_data) / f"{table}.txt")
        merged = merge_table(table, Table(pd2_ids.excel(pd2_data) / f"{table}.txt"), d2r, ren, **opts)
        if table == "skilldesc":
            fmt = pd2_skilldesc.formatted(d2r_data)
            skills = pd2_ids.excel(out_data) / "skills.txt"  # merged by ids-skills.tsv, which runs first
            skills = Table(skills if skills.is_file() else pd2_ids.excel(d2r_data) / "skills.txt")
            col = {c.lower(): j for j, c in enumerate(skills.header)}
            elements = {r[col["skilldesc"]].lower(): r[col["etype"]] for r in skills.rows}
            texts = pd2_skilldesc.texts(d2r_data, out_data)  # strings.json runs first
            pd2_skilldesc.rewrite(merged, d2r, fmt, elements, texts)
            pd2_skilldesc.hide_off_tree(merged)
            if bad := pd2_skilldesc.clashes(merged, d2r, fmt, texts):
                raise ValueError(f"skilldesc lines D2R would print wrongly: {'; '.join(bad)}")
        dest = pd2_ids.excel(out_data) / f"{table}.txt"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(merged.text().encode("latin-1"))
