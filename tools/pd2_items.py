"""Item tables with no id list (P3): merged by key around D2R's rows, the cube, map items kept from dropping, and the
HD inventory pictures new items need. The rule file is merge/item-tables.txt:

    merge <tab> <table>                 merge PD2's table onto D2R's by key (pd2_ids.KEY / VANILLA)
    no-drop <tab> <type or code> ...    items of this type (or its descendants) or code never drop

A PD2 row takes the D2R row with the same key (or, for the _BY_ROW tables, the same row); D2R-only rows stay where D2R
has them and PD2-only rows are appended, as pd2_ids does for the listed tables. The cube keeps PD2's recipes in PD2's
order, then D2R's recipes PD2 has no recipe with the same inputs and outputs for.

PD2's own item pictures (an invfile no D2R item uses) are upscaled into HD sprites (pd2_art) and drawn by every item,
unique and set item that names them; the others borrow a D2R picture (hd_items).

A never-dropping item leaves every treasure class: its weight goes to NoDrop, so every other item drops at PD2's rate
(exact for one player; NoDrop shrinks with more players), and a counted (negative Picks) entry is removed. A treasure
class holding only such items counts as one. Its items.txt row is made unspawnable too.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

import pd2_art
import pd2_ids as I

ITEM_TABLES = ["weapons", "armor", "misc"]
# named, never numbered, and a row must come after the rows it names: PD2's rows in PD2's order, then D2R-only ones
PD2_ORDER = {"treasureclassex"}
CORRUPTED_NEW = "100"  # `corrupt` value no PD2 outcome uses (1-92); the tooltip shows only "Corrupted" (descval 0)
CORRUPTOR_DONE = 3001  # PD2's own finished-map corruptor: above every outcome threshold (items 1000, maps 3000)
CUBE_CELLS = 16  # the cube's 4 x 4 grid, PD2's (#299)
HD_PER_CLASSIC = 3.5  # D2R's 98 px inventory cell over the classic 28 px
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
    mark_new_corrupted(merged, next((str(i) for p, i, _ in ctx.lists.get("itemstatcost", []) if p == "corruptor"), None))
    count_each_input(merged)
    remake_rerolls(merged)
    open_key_portal(merged)
    merged.rows += [list(r) for r in d2r_only]
    merged.dropped = dropped
    return merged


def count_each_input(cube: I.Table) -> None:
    """PD2's numinputs counts stacks ("t1me,qty=3" is 1); D2R's recipe search wants numinputs == items in the cube
    (#261), so loose inputs matched no row. Each row gets the sum of its inputs' qty, except op-30 rows (core-maps
    matches them without the count, as PD2 does) and sums past the cube's cells, which only stacks can meet (IR6, #241:
    D2R matched a stack of 3 runes under neither count)."""
    h = {c: j for j, c in enumerate(cube.header)}
    if "numinputs" not in h:
        return
    inputs = [h[c] for c in (f"input {k}" for k in range(1, 8)) if c in h]
    for r in cube.rows:
        if not r[h["numinputs"]].strip() or ("op" in h and r[h["op"]] == "30"):
            continue
        total = sum(int(m[1]) if (m := re.search(r"qty=(\d+)", r[i])) else 1 for i in inputs if r[i].strip('"'))
        if total <= CUBE_CELLS:
            r[h["numinputs"]] = str(total)


def remake_rerolls(cube: I.Table) -> None:
    """PD2's op 31 rows (Demonic Cube + a unique or set item) have no outputs: PD2's cube code remakes the item and
    takes one Demonic Cube off the stack (IR6, #241). core-rules does that through an SDK item transaction. The rows get
    `usetype,reg` only so they are complete rows to D2R, and ship switched off (`enabled` 0): D2R never runs that
    output itself, and core-rules switches the rows on when its hook and services are in."""
    h = {c: j for j, c in enumerate(cube.header)}
    for r in cube.rows if "op" in h else []:
        if r[h["op"]] == "31":
            r[h["output"]] = "usetype,reg"
            if "enabled" in h:
                r[h["enabled"]] = "0"


def open_key_portal(cube: I.Table) -> None:
    """PD2's cube walk (0x10366f40) opens the mini-uber portal for the row with op 28, param 1 and min diff 2 (the three
    Pandemonium keys), through 0x10366740: a copy of vanilla's portal (a random one of levels 133-135 not opened yet in
    this game). Its output `useitem` is a placeholder; D2R's own `Pandemonium Portal` output does the same (#485)."""
    h = {c: j for j, c in enumerate(cube.header)}
    if not all(c in h for c in ("op", "param", "min diff", "output")):
        return
    for r in cube.rows:
        if (r[h["op"]], r[h["param"]], r[h["min diff"]], r[h["output"]]) == ("28", "1", "2", "useitem"):
            r[h["output"]] = "Pandemonium Portal"


def mark_new_corrupted(cube: I.Table, corruptor: str | None) -> None:
    """A corruption outcome that makes a new item ("destroyed -> rare") gets the corrupted mark (CORRUPTED_NEW) and
    corruptor CORRUPTOR_DONE, above every outcome's threshold, so no corruption outcome or roll row matches it again.
    PD2's cube code marks it (its rows have no mods for it); D2R only applies a row's mods."""
    h = {c: j for j, c in enumerate(cube.header)}
    outcomes = [r for r in cube.rows if corruptor and r[h["op"]] == "16" and r[h["param"]] == corruptor]
    if any(int(r[h["value"]]) >= CORRUPTOR_DONE for r in outcomes):
        raise I.IdError(f"cubemain: a corruption outcome's threshold reaches the finished corruptor {CORRUPTOR_DONE}")
    final = str(CORRUPTOR_DONE)
    for r in outcomes:
        mods = [r[h[f"mod {k}"]] for k in range(1, 6) if f"mod {k}" in h]
        if r[h["output"]] == "useitem" or "corruptnum" in mods:  # changes the item, or marks the new one itself
            continue
        free = [k for k, m in enumerate(mods, 1) if not m]
        for k, (mod, v) in zip(free[:2], (("corrupt", CORRUPTED_NEW), ("corruptnum", final))):
            r[h[f"mod {k}"]], r[h[f"mod {k} chance"]], r[h[f"mod {k} min"]], r[h[f"mod {k} max"]] = mod, "100", v, v


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


def set_entry(entries: list[dict], key: str, value) -> None:
    """items.json-style list of single-key objects: replace key's entry or append it (modkit hd.set_list_entry)."""
    for e in entries:
        if key in e:
            e[key] = value
            return
    entries.append({key: value})


def _entries(path: Path) -> list[dict]:
    return json.loads(path.read_text("utf-8-sig"))


def item_pictures(art: Path, pd2_data: Path, d2r_data: Path, out_data: Path, uses: dict[str, set[str]],
                  d2r_inv: set[str]) -> dict[str, str]:
    """PD2's own pictures, an invfile in uses (-> the item tables drawing it) that no D2R item uses and pd2-data has a
    DC6 for: an HD sprite upscaled from it in each table's folder, and the DC6 for legacy mode.
    Returns {invfile: items.json asset}."""
    dc6, hd, images = pd2_art.modkit()
    classic = Path("global") / "items"
    if not (pd2_data / classic).is_dir():  # #351: else every PD2 picture is silently dropped
        raise I.IdError(f"{pd2_data / classic}: no item pictures; run tools/pd2_extract.py again")
    names =sorted(n for n in uses if n not in d2r_inv and (pd2_data / classic / f"{n}.dc6").is_file())
    pal = dc6.palette((d2r_data / "global" / "palette" / "units" / "pal.dat").read_bytes())
    frames = {n: dc6.decode(dc6.read(pd2_data / classic / f"{n}.dc6")[0], pal) for n in names}
    big = pd2_art.upscale(art, {f"items/{n}": f for n, f in frames.items()})
    (out_data / classic).mkdir(parents=True, exist_ok=True)
    for n in names:
        w, h, _ = frames[n]
        sprite = images.scale(big[f"items/{n}"], round(w * HD_PER_CLASSIC), round(h * HD_PER_CLASSIC))
        for t in sorted(uses[n]):
            images.write_sprite(out_data / "hd" / "global" / "ui" / "items" / hd.item_folder(t) / "pd2" / f"{n}.sprite",
                                sprite)
        (out_data / classic / f"{n}.dc6").write_bytes((pd2_data / classic / f"{n}.dc6").read_bytes())
    return {n: f"pd2/{n}" for n in names}


def hd_items(hd: Path, pd2: dict[str, I.Table], d2r: dict[str, I.Table],
             uniques: list[tuple[str, str, str | None, str]], sets: list[tuple[str, str, str | None, str]],
             pictures: dict[str, str] | None = None, models: dict[str, str] | None = None) -> dict[str, list[dict]]:
    """D2R's items.json, uniques.json and sets.json (from hd) plus an entry for every item code, unique and set item
    that has none. An item whose invfile is in pictures (item_pictures) shows it; else a new code shows the picture
    of the D2R item in the same table with the same invfile, else the table's first; a unique or set item shows the
    D2R row it was merged onto, else its base item.
    uniques / sets: (mod name, base code, D2R name at its id or None, invfile).
    models gets {picture asset: D2R asset}: the ground model the picture's first item would borrow without it (D2R
    draws a dropped item only from its asset's model, so a sprite alone is invisible on the ground, #351)."""
    pictures = pictures or {}
    models = {} if models is None else models
    files = {f: _entries(hd / f) for f in ("items.json", "uniques.json", "sets.json")}
    asset = {k: v["asset"] for e in files["items.json"] for k, v in e.items()}
    for t, ptab in pd2.items():
        dtab = d2r[t]
        di, dt = dtab.header.index("invfile"), dtab.header.index("type")
        by_inv = {r[di]: asset[r[dtab.key]] for r in dtab.rows if r[dtab.key] in asset}
        by_type = {}
        for r in dtab.rows:
            if r[dtab.key] in asset:
                by_type.setdefault(r[dt], asset[r[dtab.key]])
        first = next(asset[r[dtab.key]] for r in dtab.rows if r[dtab.key] in asset)
        pi, pt = ptab.header.index("invfile"), ptab.header.index("type")
        for r in ptab.rows:
            code = r[ptab.key]
            if len(code) == 4 and not ord(code[3]) & 0x20:  # core-ui sets bit 0x20 of the key's 4th byte (#310)
                raise I.IdError(f"{t} {code}: core-ui keys items.json by {code[:3]}{chr(ord(code[3]) | 0x20)}, "
                                "so this code would draw no picture; give it a 4th character with bit 0x20 set")
            if code and r[pi].lower() in pictures:
                models.setdefault(pictures[r[pi].lower()],
                                  asset.get(code) or by_inv.get(r[pi]) or by_type.get(r[pt]) or first)
                asset[code] = pictures[r[pi].lower()]
                set_entry(files["items.json"], code, {"asset": asset[code]})
            elif code and code not in asset:
                asset[code] = by_inv.get(r[pi], first)
                files["items.json"].append({code: {"asset": asset[code]}})
    for f, rows in (("uniques.json", uniques), ("sets.json", sets)):
        have = {k: v for e in files[f] for k, v in e.items()}
        for name, code, d2r_name, inv in rows:
            key = index_key(name)
            if inv.lower() in pictures:
                src = (have.get(index_key(d2r_name), {}).get("normal") if d2r_name else None) or asset.get(code)
                if src:
                    models.setdefault(pictures[inv.lower()], models.get(src, src))
                have[key] = dict.fromkeys(("normal", "uber", "ultra"), pictures[inv.lower()])
                set_entry(files[f], key, have[key])
                continue
            if key in have:
                continue
            entry = have.get(index_key(d2r_name)) if d2r_name else None
            if entry is None and code in asset:
                entry = {"normal": asset[code], "uber": asset[code], "ultra": asset[code]}
            if entry is not None:
                have[key] = entry
                files[f].append({key: entry})
    return files


def ground_models(hd: Path, out: Path, pictures: dict[str, str], models: dict[str, str],
                  uses: dict[str, set[str]]) -> None:
    """Each PD2 picture's unit file (<folder>/pd2/<invfile>.json), a copy of its stand-in D2R asset's from hd: D2R
    draws a dropped item only from that file's model, so without one the item is invisible on the ground (#351).
    A picture no item draws (a unique row with no base code) gets none."""
    item_folder = pd2_art.modkit()[1].item_folder
    for n, a in pictures.items():
        if a not in models:
            continue
        src = next((p for f in ("weapon", "armor", "misc") for p in [hd / f / f"{models[a]}.json"] if p.is_file()), None)
        if src is None:
            raise I.IdError(f"{n}: D2R has no unit file for {models[a]}")
        for t in uses[n]:
            dest = out / item_folder(t) / f"{a}.json"
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src.read_bytes())


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
        def named(t: str) -> list[tuple[str, str, str | None, str]]:
            pd2, d2r = I.load(pd2_data, t), I.load(d2r_data, t).names()
            col, inv = pd2.header.index("code" if "code" in pd2.header else "item"), pd2.header.index("invfile")
            row = dict(zip(I.ident(pd2.names()), (pd2.rows[k] for k in pd2.records())))
            return [(n, row[p][col], d2r[i] if i < len(d2r) else None, row[p][inv]) for p, i, n in ctx.lists[t]]
        pd2, d2r = {t: I.load(pd2_data, t) for t in ITEM_TABLES}, {t: I.load(d2r_data, t) for t in ITEM_TABLES}
        uniques, sets = named("uniqueitems"), named("setitems")
        table_of = {r[tab.key]: t for t, tab in pd2.items() for r in tab.rows}
        uses: dict[str, set[str]] = {}
        for t, tab in pd2.items():
            for r in tab.rows:
                uses.setdefault(r[tab.header.index("invfile")].lower(), set()).add(t)
        for _, code, _, inv in uniques + sets:
            if code in table_of:
                uses.setdefault(inv.lower(), set()).add(table_of[code])
        d2r_inv = {r[k].lower() for t in [*ITEM_TABLES, "uniqueitems", "setitems"] for tab in [I.load(d2r_data, t)]
                   for k, c in enumerate(tab.header) if c.endswith("invfile") for r in tab.rows}
        pictures = item_pictures(rule.parent / "art", pd2_data, d2r_data, out_data, uses, d2r_inv)
        print(f"  item pictures: {len(pictures)} upscaled from PD2's")
        models: dict[str, str] = {}
        files = hd_items(d2r_data / "hd" / "items", pd2, d2r, uniques, sets, pictures, models)
        ground_models(d2r_data / "hd" / "items", out_data / "hd" / "items", pictures, models, uses)
        for f, entries in files.items():
            dest = out_data / "hd" / "items" / f
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(json.dumps(entries, indent=1, ensure_ascii=False), "utf-8")
