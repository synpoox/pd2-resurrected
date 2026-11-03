"""Item tables with no id list (P3): merged by key around D2R's rows, the cube, map items kept from dropping, and the
HD inventory pictures new items need. The rule file is merge/item-tables.txt:

    merge <tab> <table>                 merge PD2's table onto D2R's by key (pd2_ids.KEY / VANILLA)
    no-drop <tab> <type or code> ...    items of this type (or its descendants) or code never drop

A PD2 row takes the D2R row with the same key (or, for the _BY_ROW tables, the same row); D2R-only rows stay where D2R
has them and PD2-only rows are appended, as pd2_ids does for the listed tables. The cube keeps PD2's recipes in PD2's
order, then D2R's recipes PD2 has no recipe with the same inputs and outputs for.

A never-dropping item leaves every treasure class: its weight goes to NoDrop, so every other item drops at PD2's rate
(exact for one player; NoDrop shrinks with more players), and a counted (negative Picks) entry is removed. A treasure
class holding only such items counts as one. Its items.txt row is made unspawnable too.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import pd2_ids as I

ITEM_TABLES = ["weapons", "armor", "misc"]
# named, never numbered, and a row must come after the rows it names: PD2's rows in PD2's order, then D2R-only ones
PD2_ORDER = {"treasureclassex"}
_SIGNATURE = [*(f"input {k}" for k in range(1, 8)), "output", "output b", "output c"]


def read_rule(path: Path) -> tuple[list[str], set[str]]:
    tables, no_drop = [], set()
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        parts = line.split("\t")
        if not line or line.startswith("#"):
            continue
        if len(parts) < 2 or parts[0] not in ("merge", "no-drop"):
            raise I.IdError(f"{path.name} line {n}: want 'merge<tab>table' or 'no-drop<tab>type or code'")
        if parts[0] == "merge" and parts[1] not in I.VANILLA and parts[1] != "cubemain":
            raise I.IdError(f"{path.name} line {n}: no merge rule for table '{parts[1]}'")
        (tables.append if parts[0] == "merge" else no_drop.add)(parts[1])
    return tables, no_drop


def merge_keyed(table: str, pd2: I.Table, d2r: I.Table, ctx: I.Context) -> I.Table:
    lines = I.seed(table, pd2.names(), d2r.names(), [])
    merged, dropped = I.merge_table(table, pd2, d2r, lines, ctx.ren, ctx.ids, ctx.props if table in I.PROPS else None)
    merged.dropped = dropped
    if table in PD2_ORDER:
        rank = {n: k for k, (_, _, n) in enumerate(lines)}
        merged.rows.sort(key=lambda r: rank.get(r[merged.key], len(rank)))  # stable: D2R-only rows keep D2R's order
    return merged


def merge_cube(pd2: I.Table, d2r: I.Table, ctx: I.Context) -> I.Table:
    def signature(t: I.Table, row: list[str]) -> tuple[str, ...]:
        return tuple(row[t.header.index(c)].replace('"', "").lower() if c in t.header else "" for c in _SIGNATURE)
    pd2_sigs = {signature(pd2, r) for r in pd2.rows}
    d2r_only = [r for r in d2r.rows if signature(d2r, r) not in pd2_sigs]
    empty = I.Table.__new__(I.Table)
    empty.header, empty.rows, empty.newline, empty.key = d2r.header, [], d2r.newline, 0
    lines = [(p, k, I.base(p).replace('"', "")) for k, p in enumerate(I.ident(pd2.names()))]
    merged, dropped = I.merge_table("cubemain", pd2, empty, lines, ctx.ren, ctx.ids, ctx.props)
    merged.rows += [list(r) for r in d2r_only]
    merged.dropped = dropped
    return merged


def block_drops(tc: I.Table, items: dict[str, I.Table], itemtypes: I.Table, no_drop: set[str]) -> set[str]:
    """Take the no_drop items out of every treasure class (module docstring). Returns the item codes blocked."""
    h = {c: j for j, c in enumerate(itemtypes.header)}
    parents = {r[itemtypes.key]: [r[h[c]] for c in ("Equiv1", "Equiv2") if c in h and r[h[c]]] for r in itemtypes.rows}

    def blocked_type(t: str, seen: frozenset = frozenset()) -> bool:
        return t in no_drop or any(blocked_type(p, seen | {t}) for p in parents.get(t, []) if p not in seen)
    codes = set()
    for t in items.values():
        ty, sp = t.header.index("type"), t.header.index("spawnable")
        for r in t.rows:
            if r[t.key] and (r[t.key] in no_drop or blocked_type(r[ty])):
                codes.add(r[t.key])
                r[sp] = "0"
    th = {c: j for j, c in enumerate(tc.header)}
    slots = [(th[f"Item{k}"], th[f"Prob{k}"]) for k in range(1, 11) if f"Item{k}" in th]
    gone = set(codes)
    while True:  # treasure classes holding only blocked entries count as blocked
        more = {r[0] for r in tc.rows if r[0] not in gone and (es := [r[i] for i, _ in slots if r[i]])
                and all(e in gone for e in es)}
        if not more:
            break
        gone |= more
    for r in tc.rows:
        kept = [(r[i], r[p]) for i, p in slots if r[i] and r[i] not in gone]
        lost = [r[p] for i, p in slots if r[i] and r[i] in gone]
        if not lost:
            continue
        picks = int(r[th["Picks"]] or 0)
        if picks < 0:  # counted picks: each entry's Prob is how many of it drop
            r[th["Picks"]] = str(picks + sum(int(v or 0) for v in lost))
        else:
            r[th["NoDrop"]] = str(int(r[th["NoDrop"]] or 0) + sum(int(v or 0) for v in lost))
        for k, (i, p) in enumerate(slots):
            r[i], r[p] = kept[k] if k < len(kept) else ("", "")
    return codes


def index_key(index: str) -> str:
    """uniques.json / sets.json key of a uniqueitems / setitems index (modkit hd.index_key)."""
    return re.sub(r"[ \-]", "_", index.lower()).replace("'", "")


def _entries(path: Path) -> list[dict]:
    return json.loads(path.read_text("utf-8-sig"))


def hd_items(hd: Path, pd2: dict[str, I.Table], d2r: dict[str, I.Table], uniques: list[tuple[str, str, str | None]],
             sets: list[tuple[str, str, str | None]]) -> dict[str, list[dict]]:
    """D2R's items.json, uniques.json and sets.json (from hd) plus an entry for every item code, unique and set item
    that has none. A new code shows the picture of the D2R item in the same table with the same invfile, else the
    table's first; a unique or set item shows the D2R row it was merged onto, else its base item.
    uniques / sets: (mod name, base code, D2R name at its id or None)."""
    files = {f: _entries(hd / f) for f in ("items.json", "uniques.json", "sets.json")}
    asset = {k: v["asset"] for e in files["items.json"] for k, v in e.items()}
    for t, ptab in pd2.items():
        dtab = d2r[t]
        di = dtab.header.index("invfile")
        by_inv = {r[di]: asset[r[dtab.key]] for r in dtab.rows if r[dtab.key] in asset}
        first = next(asset[r[dtab.key]] for r in dtab.rows if r[dtab.key] in asset)
        pi = ptab.header.index("invfile")
        for r in ptab.rows:
            code = r[ptab.key]
            if code and code not in asset:
                asset[code] = by_inv.get(r[pi], first)
                files["items.json"].append({code: {"asset": asset[code]}})
    for f, rows in (("uniques.json", uniques), ("sets.json", sets)):
        have = {k: v for e in files[f] for k, v in e.items()}
        for name, code, d2r_name in rows:
            key = index_key(name)
            if key in have:
                continue
            entry = have.get(index_key(d2r_name)) if d2r_name else None
            if entry is None and code in asset:
                entry = {"normal": asset[code], "uber": asset[code], "ultra": asset[code]}
            if entry is not None:
                have[key] = entry
                files[f].append({key: entry})
    return files


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/item-tables.txt."""
    tables, no_drop = read_rule(rule)
    ctx = I.context(rule.parent, pd2_data, d2r_data)
    out = {}
    for t in tables:
        pd2, d2r = I.load(pd2_data, t), I.load(d2r_data, t)
        out[t] = merge_cube(pd2, d2r, ctx) if t == "cubemain" else merge_keyed(t, pd2, d2r, ctx)
    if no_drop:
        if not {"treasureclassex", *ITEM_TABLES} <= set(out):
            raise I.IdError(f"{rule.name}: no-drop needs treasureclassex and {', '.join(ITEM_TABLES)} merged here")
        itemtypes = I.merge_table("itemtypes", I.load(pd2_data, "itemtypes"), I.load(d2r_data, "itemtypes"),
                                  ctx.lists["itemtypes"], ctx.ren)[0]
        blocked = block_drops(out["treasureclassex"], {t: out[t] for t in ITEM_TABLES}, itemtypes, no_drop)
        print(f"  never drop: {len(blocked)} items ({', '.join(sorted(no_drop))})")
    for t, merged in out.items():
        I.write(t, merged, merged.dropped, out_data)
    if set(ITEM_TABLES) <= set(out):
        def named(t: str) -> list[tuple[str, str, str | None]]:
            pd2, d2r = I.load(pd2_data, t), I.load(d2r_data, t).names()
            col = pd2.header.index("code" if "code" in pd2.header else "item")
            code = dict(zip(I.ident(pd2.names()), (pd2.rows[k][col] for k in pd2.records())))
            return [(n, code[p], d2r[i] if i < len(d2r) else None) for p, i, n in ctx.lists[t]]
        files = hd_items(d2r_data / "hd" / "items", {t: I.load(pd2_data, t) for t in ITEM_TABLES},
                         {t: I.load(d2r_data, t) for t in ITEM_TABLES}, named("uniqueitems"), named("setitems"))
        for f, entries in files.items():
            dest = out_data / "hd" / "items" / f
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(entries, indent=1, ensure_ascii=False), "utf-8")
