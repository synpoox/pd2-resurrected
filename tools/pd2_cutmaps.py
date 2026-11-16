"""PD2's map system, cut from the merged tables (#964, epic #942): the Cartographer plugin makes maps now.

A pd2_merge rule. Its anchor is merge/z-maps-cut.txt, named to sort LAST (the driver runs rule files in name order, and the
cut reads monstats and superuniques, which tables.json merges). One line kind:

    type <tab> <item type> [<tab> why]    items of this type (or its descendants) are PD2 map items and are cut

What it does to the merged tables in generated/data/global/excel:
  misc          every row of a cut type is deleted (keys, type `ubr`, and the native `cmap` items are not cut types).
  treasureclassex   the map items leave every class, their weight going to NoDrop (pd2_items.block_drops, exact for one
                player); a class holding only map entries empties; an emptied class no table names is deleted.
  cubemain      every recipe that names a cut item, a cut property, or a map heading is deleted.
  properties    every `map*` property on a `map_*` stat is deleted. The Catalyst's `map-force-event-l` is patch 072's.
  magicprefix, magicsuffix, uniqueitems   a row that uses a cut property is neutralised, never deleted: a save stores
                the row number (ADR 0003). Its affix never spawns and a unique never drops.
Rows of itemstatcost, itemtypes and levels stay: their ids are saved or reserved (ADR 0003), and nothing names them now.

The rule ends with a dangling check: nothing left names a cut item, property or class. A leftover fails the merge.
"""
from __future__ import annotations

import re
from pathlib import Path

import pd2_items
from pd2_table import IdError, Table, excel

# A unique whose code was a cut map item keeps a code that exists (the row is neutral; a save may name its number).
NEUTRAL_CODE = "jew"
# The Act 1-5 (H) Good classes' Item9 held PD2's `Map Tier 1`; patches/070 and 071 set Item9 to the native pool and rely on
# the Prob9 PD2 gave it, so the cut empties Item9 only.
POOL_SLOT = "Item9"
POOL_ROWS = [f"Act {n} (H) Good" for n in range(1, 6)]
AFFIXES = {"magicprefix": "mod{n}code", "magicsuffix": "mod{n}code", "uniqueitems": "prop{n}"}
MOD_TAIL = {"mod{n}code": ("mod{n}param", "mod{n}min", "mod{n}max"), "prop{n}": ("par{n}", "min{n}", "max{n}")}
CUBE_ITEM_COLS = re.compile(r"^(input \d|output( b| c)?)$")
CUBE_MOD_COLS = re.compile(r"(^| )mod \d$")


def read_rule(path: Path) -> set[str]:
    types = set()
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if parts[0] != "type" or len(parts) < 2:
            raise IdError(f"{path.name} line {n}: want 'type<tab>item type'")
        types.add(parts[1])
    return types


def read(out_data: Path, name: str, key: str | None = None) -> Table:
    return Table(excel(out_data) / f"{name}.txt", key)


def write(out_data: Path, name: str, t: Table) -> None:
    (excel(out_data) / f"{name}.txt").write_bytes(t.text().encode("latin-1"))


def col(t: Table, name: str) -> int:
    return t.header.index(name)


def cut_properties(props: Table) -> set[str]:
    stats = [i for i, c in enumerate(props.header) if re.fullmatch(r"stat\d", c)]
    return {r[0].lower() for r in props.rows if r[0].lower().startswith("map")
            and any(r[i].startswith("map_") for i in stats)}


def cut_cube(cube: Table, codes: set[str], props: set[str]) -> int:
    """Delete the recipes that name a cut item or property, and a heading row that names a map."""
    item_cols = [i for i, c in enumerate(cube.header) if CUBE_ITEM_COLS.match(c)]
    mod_cols = [i for i, c in enumerate(cube.header) if CUBE_MOD_COLS.search(c)]
    desc = col(cube, "description")

    def doomed(r: list[str]) -> bool:
        if any(r[i].split(",")[0].strip().lower() in codes for i in item_cols):
            return True
        if any(r[i].lower() in props for i in mod_cols):
            return True
        return bool(r[0] != "Expansion" and not any(c for k, c in enumerate(r) if k != desc) and re.search(r"\bmaps?\b", r[desc], re.I))
    kept = [r for r in cube.rows if not doomed(r)]
    n = len(cube.rows) - len(kept)
    cube.rows = kept
    return n


def neutralise(t: Table, slot: str, props: set[str], unique: bool) -> int:
    """Blank every cut-property slot of a row that has one, and stop the row spawning. Returns the rows changed."""
    h = {c: i for i, c in enumerate(t.header)}
    n = 0
    for r in t.rows:
        hit = False
        for k in range(1, 13):
            c = slot.format(n=k)
            if c in h and r[h[c]].lower() in props:
                hit = True
                for cc in (c, *(x.format(n=k) for x in MOD_TAIL[slot])):
                    r[h[cc]] = ""
        if not hit:
            continue
        n += 1
        if unique:
            r[h["disabled"]], r[h["spawnable"]], r[h["rarity"]], r[h["code"]] = "1", "0", "0", NEUTRAL_CODE
        else:
            r[h["spawnable"]], r[h["rare"]] = "0", "0"
            for c in t.header:
                if re.fullmatch(r"[ie]type\d", c):
                    r[h[c]] = ""
    return n


def references(out_data: Path, names: set[str], skip: dict[str, set[str]]) -> dict[str, list[str]]:
    """table -> the cells (lower case) that equal one of names, outside the skipped columns and rows."""
    found: dict[str, list[str]] = {}
    for p in sorted(excel(out_data).glob("*.txt")):
        t = Table(p)
        ignore = {i for i, c in enumerate(t.header) if c in skip.get(p.stem, set()) or c in ("description", "*ItemName")}
        for r in t.rows:
            for i, c in enumerate(r):
                if i not in ignore and c.lower() in names:
                    found.setdefault(p.stem, []).append(f"{r[0]}: {c}")
    return found


def undefined_maps(tc: Table, slots: list[int], th: dict[str, int]) -> None:
    """PD2's `Map Miniboss t3` names `Map Uitem`, a class PD2 never defines (D2RLoader asserts on it): drop that entry the way
    block_drops drops a cut one (its weight to NoDrop)."""
    defined = {r[0] for r in tc.rows}
    for r in tc.rows:
        pairs = [(r[i], r[i + 1]) for i in slots if r[i]]
        kept = [(a, b) for a, b in pairs if not (a.startswith("Map ") and a not in defined)]
        if len(kept) == len(pairs):
            continue
        lost = sum(int(b or 0) for a, b in pairs if (a, b) not in kept)
        r[th["NoDrop"]] = str(int(r[th["NoDrop"]] or 0) + lost)
        for k, i in enumerate(slots):
            r[i], r[i + 1] = kept[k] if k < len(kept) else ("", "")


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/z-maps-cut.txt."""
    types = read_rule(rule)
    itemtypes = read(out_data, "itemtypes", "Code")
    misc, tc = read(out_data, "misc", "code"), read(out_data, "treasureclassex")
    ty = col(misc, "type")
    parents = {r[itemtypes.key].lower(): [r[col(itemtypes, c)].lower() for c in ("Equiv1", "Equiv2") if r[col(itemtypes, c)]]
               for r in itemtypes.rows}

    def of_cut_type(t: str, seen: frozenset = frozenset()) -> bool:
        return t.lower() in {x.lower() for x in types} or any(
            of_cut_type(p, seen | {t}) for p in parents.get(t.lower(), []) if p not in seen)
    codes = {r[misc.key].lower() for r in misc.rows if of_cut_type(r[ty])}
    if missing := sorted(t for t in types if t.lower() not in parents):
        raise IdError(f"{rule.name}: no item type {', '.join(missing)}")

    # treasure classes: the cascade reuses the never-drop code, which also marks the items unspawnable
    th = {c: i for i, c in enumerate(tc.header)}
    item_slots = [th[f"Item{k}"] for k in range(1, 11) if f"Item{k}" in th]
    pool = {r[0]: list(r) for r in tc.rows if r[0] in POOL_ROWS}
    if len(pool) != len(POOL_ROWS) or any(r[th[POOL_SLOT]] != "Map Tier 1" for r in pool.values()):
        raise IdError(f"{rule.name}: {', '.join(POOL_ROWS)} no longer hold `Map Tier 1` in {POOL_SLOT}")
    undefined_maps(tc, item_slots, th)
    had = {r[0] for r in tc.rows if any(r[i] for i in item_slots)}
    pd2_items.block_drops(tc, {"misc": misc}, itemtypes, types)
    for r in tc.rows:
        if r[0] in pool:
            r[:] = pool[r[0]]
            r[th[POOL_SLOT]] = ""  # Prob9 and NoDrop stay: patches/070 fills Item9 for the weight PD2 gave the maps
    empty = {r[0] for r in tc.rows if not any(r[i] for i in item_slots)}
    # cut: held only cut entries (had some, none now), or was an empty class PD2 named for maps
    cut_classes = {n for n in empty - set(POOL_ROWS) if n in had or re.search(r"map", n, re.I)}
    write(out_data, "treasureclassex", tc)

    misc.rows = [r for r in misc.rows if r[misc.key].lower() not in codes]
    write(out_data, "misc", misc)

    props = read(out_data, "properties")
    gone_props = cut_properties(props)
    props.rows = [r for r in props.rows if r[0].lower() not in gone_props]
    write(out_data, "properties", props)

    cube = read(out_data, "cubemain")
    cut = cut_cube(cube, codes | {t.lower() for t in types}, gone_props)
    write(out_data, "cubemain", cube)

    changed = {}
    for name, slot in AFFIXES.items():
        t = read(out_data, name)
        changed[name] = neutralise(t, slot, gone_props, name == "uniqueitems")
        write(out_data, name, t)
    if NEUTRAL_CODE not in {r[misc.key].lower() for r in misc.rows}:
        raise IdError(f"{rule.name}: no misc row '{NEUTRAL_CODE}' for the neutral unique code")

    # classes no table names go; the rest stay as empty rows
    named = references(out_data, {n.lower() for n in cut_classes}, {"treasureclassex": {"Treasure Class"}})
    used = {c for lines in named.values() for c in (x.split(": ", 1)[1].lower() for x in lines)}
    tc = read(out_data, "treasureclassex")
    deleted = [r[0] for r in tc.rows if r[0] in cut_classes and r[0].lower() not in used]
    tc.rows = [r for r in tc.rows if r[0] not in deleted]
    write(out_data, "treasureclassex", tc)

    left = references(out_data, codes | gone_props | {n.lower() for n in deleted},
                      {"treasureclassex": {"Treasure Class"}, "properties": {"code"}, "uniqueitems": {"index"}})
    for kept in ("itemstatcost", "itemtypes"):  # rows kept for their ids (module doc)
        left.pop(kept, None)
    if left:
        raise IdError(f"{rule.name}: cut rows are still named: " + "; ".join(f"{t} {v[:3]}" for t, v in left.items()))
    print(f"  maps cut: {len(codes)} misc rows, {cut} cube rows, {len(gone_props)} properties, {len(deleted)} treasure "
          f"classes ({len(cut_classes) - len(deleted)} emptied), affixes neutralised {changed}")
