"""PD2's map system, cut from the merged tables (#964, epic #942): the Cartographer plugin makes maps now.

A pd2_merge rule. Its anchor is merge/z-maps-cut.txt, named to sort LAST (the driver runs rule files in name order, and the
cut reads monstats and superuniques, which tables.json merges). Two line kinds:

    type <tab> <item type> [<tab> why]    items of this type (or its descendants) are PD2 map items and are cut
    code <tab> <misc code> [<tab> why]    one misc row cut by its code (the Monastery Map `t41`: type `ubr` stays)

What it does to the merged tables in generated/data/global/excel:
  misc          every row of a cut type is deleted (keys, type `ubr`, and the native `cmap` items are not cut types).
  treasureclassex   the map items leave every class, their weight going to NoDrop (items.block_drops, exact for one
                player); a class holding only map entries empties; an emptied class no table names is deleted.
  cubemain      every recipe that names a cut item, a cut property, or a map heading is deleted.
  properties    every `map*` property on a `map_*` stat is deleted. The Catalyst's `map-force-event-l` is in the committed properties.txt.
  magicprefix, magicsuffix, uniqueitems   a row that uses a cut property is neutralised, never deleted: a save stores
                the row number (ADR 0003). Its affix never spawns and a unique never drops.
Rows of itemstatcost, itemtypes and levels stay: their ids are saved or reserved (ADR 0003), and nothing names them now.

The rule ends with a dangling check: nothing left names a cut item, property or class. A leftover fails the merge.
"""
from __future__ import annotations

import re
from pathlib import Path

import pd2_table
from pd2_table import IdError, Table, excel
from shared.classic_import import items

# A unique whose code was a cut map item keeps a code that exists (the row is neutral; a save may name its number).
NEUTRAL_CODE = "jew"
# The Act 1-5 (H) Good classes' Item9 held PD2's `Map Tier 1`; the committed treasureclassex.txt sets Item9 to the native pool and relies on
# the Prob9 PD2 gave it, so the cut empties Item9 only.
POOL_SLOT = "Item9"
POOL_ROWS = [f"Act {n} (H) Good" for n in range(1, 6)]
AFFIXES = {"magicprefix": "mod{n}code", "magicsuffix": "mod{n}code", "uniqueitems": "prop{n}"}
MOD_TAIL = {"mod{n}code": ("mod{n}param", "mod{n}min", "mod{n}max"), "prop{n}": ("par{n}", "min{n}", "max{n}")}
CUBE_ITEM_COLS = re.compile(r"^(input \d|output( b| c)?)$")
CUBE_MOD_COLS = re.compile(r"(^| )mod \d$")


def read_rule(path: Path, kind: str = "type") -> set[str]:
    """The second column of the rule's `kind` lines."""
    types = set()
    for n, line in enumerate(path.read_text("utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if parts[0] not in ("type", "code") or len(parts) < 2:
            raise IdError(f"{path.name} line {n}: want 'type<tab>item type' or 'code<tab>misc code'")
        if parts[0] == kind:
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


def cube_doomed(cube: Table, codes: set[str], props: set[str]) -> list[bool]:
    """Per recipe: names a cut item or property, or is a heading row that names a map."""
    item_cols = [i for i, c in enumerate(cube.header) if CUBE_ITEM_COLS.match(c)]
    mod_cols = [i for i, c in enumerate(cube.header) if CUBE_MOD_COLS.search(c)]
    desc = col(cube, "description")

    def doomed(r: list[str]) -> bool:
        if any(r[i].strip('"').split(",")[0].strip().lower() in codes for i in item_cols):
            return True
        if any(r[i].strip('"').lower() in props for i in mod_cols):
            return True
        return bool(r[0] != "Expansion" and not any(c for k, c in enumerate(r) if k != desc) and re.search(r"\bmaps?\b", r[desc], re.I))
    return [doomed(r) for r in cube.rows]


def cut_cube(cube: Table, codes: set[str], props: set[str]) -> int:
    """Delete the recipes that name a cut item or property, and a heading row that names a map."""
    gone = cube_doomed(cube, codes, props)
    kept = [r for r, g in zip(cube.rows, gone) if not g]
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


def cut_codes(types: set[str], misc: Table, itemtypes: Table) -> set[str]:
    """The misc codes whose type is a cut type or descends from one."""
    parents = {r[itemtypes.key].lower(): [r[col(itemtypes, c)].lower() for c in ("Equiv1", "Equiv2") if r[col(itemtypes, c)]]
               for r in itemtypes.rows}
    low = {x.lower() for x in types}
    if missing := sorted(t for t in types if t.lower() not in parents):
        raise IdError(f"no item type {', '.join(missing)}")

    def of_cut_type(t: str, seen: frozenset = frozenset()) -> bool:
        return t.lower() in low or any(of_cut_type(p, seen | {t}) for p in parents.get(t.lower(), []) if p not in seen)
    ty = col(misc, "type")
    return {r[misc.key].lower() for r in misc.rows if of_cut_type(r[ty])}


def named_codes(names: set[str], misc: Table) -> set[str]:
    """The `code` lines of the rule, lower case; a code misc does not have is a typo."""
    have = {r[misc.key].lower() for r in misc.rows}
    if missing := sorted(c for c in names if c.lower() not in have):
        raise IdError(f"no misc code {', '.join(missing)}")
    return {c.lower() for c in names}


def glob(label: str) -> str:
    """A row label as an fnmatch pattern part, lower case, `[` escaped."""
    if re.search(r"[|`]", label):
        raise IdError(f"row label {label!r} cannot sit in a Gate cell")
    return label.lower().replace("[", "[[]")


def gate_patterns(rule: Path, pd2_data: Path) -> list[str]:
    """The Gate patterns of docs/deviations.md for the cut, row by row, read off PD2's tables with the cut's own tests
    (the gate names PD2's rows). `py tools/pd2_cutmaps.py` prints the row; tests/test_maps_cut.py keeps it current."""
    types = read_rule(rule)
    load = lambda n: pd2_table.load(pd2_data, n)  # noqa: E731
    misc, itemtypes, props = load("misc"), load("itemtypes"), load("properties")
    codes, gone_props = cut_codes(types, misc, itemtypes) | {c.lower() for c in read_rule(rule, "code")}, cut_properties(props)
    out = [f"misc.{glob(c)}.*" for c in sorted(codes)]
    tc = load("treasureclassex")
    th = {c: i for i, c in enumerate(tc.header)}
    slots = [th[f"Item{k}"] for k in range(1, 11) if f"Item{k}" in th]
    is_map = lambda e: e.lower() in codes or "map" in e.lower()  # noqa: E731
    out.append("treasureclassex.*map*.*")
    for r in tc.rows:
        if "map" not in r[0].lower() and any(is_map(r[i]) for i in slots if r[i]):
            n = glob(r[0])
            out += [f"treasureclassex.{n}.item*", f"treasureclassex.{n}.prob*", f"treasureclassex.{n}.nodrop"]
    cube = load("cubemain")
    labels = pd2_table.ident(cube.names())
    gone = cube_doomed(cube, codes | {t.lower() for t in types}, gone_props)
    out += [f"cubemain.{glob(lab)}.*" for lab, i in zip(labels, cube.records()) if gone[i]]
    for name, slot in AFFIXES.items():
        if name == "uniqueitems":
            out.append("uniqueitems.* map.*")
            continue
        t = load(name)
        h = {c: i for i, c in enumerate(t.header)}
        cols = [h[c] for c in (slot.format(n=k) for k in range(1, 13)) if c in h]
        out += [f"{name}.{glob(lab)}.*" for lab, i in zip(pd2_table.ident(t.names()), t.records())
                if any(t.rows[i][c].lower() in gone_props for c in cols)]
    return out + ["properties.map-*.*", "properties.mapdefense.*"]


def gate_row(rule: Path, pd2_data: Path, name: str) -> str:
    return f"| {name} | " + " ".join(f"`{p}`" for p in gate_patterns(rule, pd2_data)) + " |"


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/z-maps-cut.txt."""
    types = read_rule(rule)
    itemtypes = read(out_data, "itemtypes", "Code")
    misc, tc = read(out_data, "misc", "code"), read(out_data, "treasureclassex")
    codes = cut_codes(types, misc, itemtypes) | named_codes(read_rule(rule, "code"), misc)

    # treasure classes: the cascade reuses the never-drop code, which also marks the items unspawnable
    th = {c: i for i, c in enumerate(tc.header)}
    item_slots = [th[f"Item{k}"] for k in range(1, 11) if f"Item{k}" in th]
    pool = {r[0]: list(r) for r in tc.rows if r[0] in POOL_ROWS}
    if len(pool) != len(POOL_ROWS) or any(r[th[POOL_SLOT]] != "Map Tier 1" for r in pool.values()):
        raise IdError(f"{rule.name}: {', '.join(POOL_ROWS)} no longer hold `Map Tier 1` in {POOL_SLOT}")
    undefined_maps(tc, item_slots, th)
    had = {r[0] for r in tc.rows if any(r[i] for i in item_slots)}
    items.block_drops(tc, {"misc": misc}, itemtypes, types)
    for r in tc.rows:
        if r[0] in pool:
            r[:] = pool[r[0]]
            r[th[POOL_SLOT]] = ""  # Prob9 and NoDrop stay: the committed treasureclassex.txt fills Item9 for the weight PD2 gave the maps
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


def deviations_row(root: Path) -> tuple[Path, str]:
    """docs/deviations.md and the cut's Gate row, as `gate_row` writes it."""
    path = root / "docs" / "deviations.md"
    name = next(ln for ln in path.read_text("utf-8").splitlines() if ln.startswith("| PD2's map system:")).split(" | ")[0][2:]
    return path, gate_row(root / "merge" / "z-maps-cut.txt", root / "pd2-data" / "data", name)


if __name__ == "__main__":
    # `py tools/pd2_cutmaps.py` rewrites the cut's Gate row in docs/deviations.md (the row a test keeps equal to this)
    root = Path(__file__).resolve().parents[1]
    path, row = deviations_row(root)
    text = path.read_text("utf-8")
    lines = text.split("\n")
    i = next(k for k, ln in enumerate(lines) if ln.startswith("| PD2's map system:") and "`misc." in ln)
    lines[i] = row
    path.write_text("\n".join(lines), "utf-8", newline="")
    print(f"rewrote the Gate row ({row.count('`') // 2} patterns)")
