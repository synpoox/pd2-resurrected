"""The PD2-only areas (LP1, #151): monster presets, level types, object presets, and their DS1 and DT1 files.

Two rule kinds, after pd2_ids has merged levels, lvlprest, lvlwarp, objects, objtype and objgroup:

merge/ids-monpreset.tsv: monpreset's append-only list (ADR 0003), one line per PD2 row, `<act>/<Place> <tab> index
<tab> Place`: the row's index within its act, the number a DS1 names it by. Below VANILLA the index is the same row in
both games (D2R's act 5 rows 51+ are D2R's own); higher PD2 rows append to their act.

merge/level-files.tsv: writes lvltypes, objpreset and the files of the PD2-only presets (the lvlprest rows the id
list appends). lvltypes takes PD2's new types (ids kept: D2R's end before them) and, in D2R's types, the DT1 files the
PD2-only presets select; a file whose slot D2R already fills moves to a free slot, and those presets' Dt1Mask bit with
it. The three DT1 PD2 ships over vanilla paths move to OVERRIDES/<path>, so D2R's own areas keep D2R's. Each DS1 is
rewritten: its monster ids to the mod's monpreset indices, its object ids to objpreset rows of its act, appended where
the act has none for that object. The file lists the DS1 objects the rewrite leaves out, with why; any other object
it can't resolve fails the merge. PD2's automap rows for its new level types go to core-ui's custom table (LP2): D2R's
automap knows only types 0-35; PD2's own automap cels go to D2R's HD tiles (pd2_automap).

A DS1 object id in PD2 (1.13c D2Common 0x6fd5b560, checked in Ghidra for #211): type 1 is a monpreset index of the
DS1's act; type 2 below 150 indexes 1.13c's object preset table as one flat array, act * 150 + id, so a negative id
reaches an earlier act's rows; 150 and up is objects row id - 150. D2R's objpreset.txt holds that table (its 580 rows
below 150 match 1.13c's); ProjectDiablo.dll fills act 1's empty slots 135-149 at start-up (ACT1_SLOTS).
"""
from __future__ import annotations

import struct
from pathlib import Path

import pd2_automap
import pd2_ids
from pd2_ids import IdError, Table, excel, ident, load, read_list

MONPRESET_VANILLA = {"1": 47, "2": 59, "3": 39, "4": 28, "5": 51}
# ProjectDiablo.dll 0x10115820 writes these objects ids into act 1's preset slots: the boss and teleport blockers,
# then preset_placeholder_A-J
ACT1_SLOTS = {135: 616, 136: 617, 137: 613, 138: 614, 139: 615, **{140 + k: 601 + k for k in range(10)}}
OVERRIDES = "pd2assets/v"  # where the DT1 PD2 ships over vanilla paths go (short: see PATH_MAX)
PATH_MAX = 41  # characters D2R keeps of a lvltypes File (it cut pd2assets/vanilla/expansion/town/waypt.dt1)
OVERRIDDEN = {"act4/fort/foregate.dt1", "expansion/town/waypt.dt1", "act1/barracks/warp.dt1"}  # those DT1 (#146)
# 1.13c's preset table names these objects ids for the objects its engine places (random shrines and chests, the Arcane
# Sanctuary's portal); PD2's objects.txt keeps a blank <reserved> row at each, D2R a row of its own at the same id
ENGINE_OBJECTS = range(573, 584)
SLOTS = 32  # lvltypes File 1-32


def acts(t: Table) -> dict[str, list[str]]:
    """monpreset's places by act, in file order (a DS1's monster id is the index in its act)."""
    out: dict[str, list[str]] = {}
    for r in t.rows:
        out.setdefault(r[0], []).append(r[1])
    return out


def seed_monpreset(pd2: dict[str, list[str]], d2r: dict[str, list[str]],
                   existing: list[tuple[str, int, str]]) -> list[tuple[str, int, str]]:
    listed = {p.lower() for p, _, _ in existing}
    new = []
    for act, places in pd2.items():
        nxt = max([len(d2r.get(act, [])) - 1, *(i for p, i, _ in existing if p.split("/")[0] == act)]) + 1
        for i, name in enumerate(ident(places)):
            key = f"{act}/{name}"
            if key.lower() in listed:
                continue
            if i < MONPRESET_VANILLA.get(act, 0) and i < len(d2r.get(act, [])):
                new.append((key, i, d2r[act][i]))
            else:
                new.append((key, nxt, places[i]))
                nxt += 1
    return new


def check_monpreset(lines: list[tuple[str, int, str]], pd2: dict[str, list[str]], d2r: dict[str, list[str]]) -> None:
    want = {f"{act}/{n}".lower() for act, places in pd2.items() for n in ident(places)}
    seen, ids = set(), {}
    for p, i, n in lines:
        act = p.split("/")[0]
        if p.lower() in seen or p.lower() not in want or i < 0:
            raise IdError(f"ids-monpreset.tsv: '{p}' is listed twice, is not a PD2 row or has no index")
        seen.add(p.lower())
        if i < len(d2r.get(act, [])) and d2r[act][i] != n:
            raise IdError(f"ids-monpreset.tsv: act {act} index {i} is D2R's '{d2r[act][i]}', the list says '{n}'")
        ids.setdefault(act, []).append(i)
    if want - seen:
        raise IdError(f"ids-monpreset.tsv: PD2 rows with no index ({', '.join(sorted(want - seen)[:5])}): "
                      "run tools/pd2_merge.py --seed-ids")
    for act, got in ids.items():
        added = sorted(i for i in got if i >= len(d2r.get(act, [])))
        if len(set(got)) != len(got) or added != list(range(len(d2r.get(act, [])), len(d2r.get(act, [])) + len(added))):
            raise IdError(f"ids-monpreset.tsv: act {act}'s indices repeat or append with gaps")


def merge_monpreset(d2r: Table, lines: list[tuple[str, int, str]]) -> Table:
    """D2R's monpreset with the appended PD2 rows after each act's last row."""
    per = acts(d2r)
    for p, i, n in sorted(lines, key=lambda line: line[1]):
        act = p.split("/")[0]
        if i >= len(per.setdefault(act, [])):
            per[act].append(n)
    t = Table.__new__(Table)
    t.header, t.newline, t.key = d2r.header, d2r.newline, 0
    t.rows = [[act, n] + [""] * (len(d2r.header) - 2) for act, places in per.items() for n in places]
    return t


def monpreset_lines(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> list[tuple[str, int, str]]:
    lines = read_list(merge_dir / "ids-monpreset.tsv")
    check_monpreset(lines, acts(load(pd2_data, "monpreset")), acts(load(d2r_data, "monpreset")))
    return lines


def apply_monpreset(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/ids-monpreset.tsv."""
    pd2_ids.write("monpreset", merge_monpreset(load(d2r_data, "monpreset"), monpreset_lines(rule.parent, pd2_data, d2r_data)),
                  [], out_data)


def seed_list(merge_dir: Path, pd2_data: Path, d2r_data: Path) -> int:
    path = merge_dir / "ids-monpreset.tsv"
    existing = read_list(path)
    new = seed_monpreset(acts(load(pd2_data, "monpreset")), acts(load(d2r_data, "monpreset")), existing)
    if new:
        text = path.read_text("utf-8") if path.exists() else "# act/PD2 place\tindex in act\tplace (append-only, ADR 0003)\n"
        path.write_text(text + "".join(f"{p}\t{i}\t{n}\n" for p, i, n in new), "utf-8")
    return len(new)


# DS1 (version 18 and older), as 1.13c's loader reads it: header, file names, tile layers, then the object list
def object_list(data: bytes) -> tuple[int, int, int, int]:
    """(act 0-4, offset of the object count, object count, bytes per object)."""
    version, width, height = struct.unpack_from("<3i", data, 0)
    o, act, tag = 12, 0, 0
    if version >= 8:
        act = min(struct.unpack_from("<i", data, o)[0], 4)
        o += 4
    if version >= 10:
        tag = struct.unpack_from("<i", data, o)[0]
        o += 4
    if version >= 3:
        files = struct.unpack_from("<i", data, o)[0]
        o += 4
        for _ in range(files):
            o = data.index(b"\0", o) + 1
    if 9 <= version <= 13:
        o += 8
    walls, floors = 1, 1
    if version >= 4:
        walls = struct.unpack_from("<i", data, o)[0]
        o += 4
        if version >= 16:
            floors = struct.unpack_from("<i", data, o)[0]
            o += 4
    o += (2 * walls + floors + 1 + (tag in (1, 2))) * (width + 1) * (height + 1) * 4
    if version < 2:
        return act, o, 0, 0
    return act, o, struct.unpack_from("<i", data, o)[0], 20 if version >= 5 else 16


def rewrite_ds1(data: bytes, fix) -> bytes:
    """The DS1 with each object's (type, id) replaced by fix(act, type, id): a new id, or None to leave it out."""
    act, at, count, size = object_list(data)
    keep = []
    for k in range(count):
        rec = bytearray(data[at + 4 + k * size: at + 4 + (k + 1) * size])
        kind, oid = struct.unpack_from("<2i", rec)
        new = fix(act, kind, oid)
        if new is not None:
            struct.pack_into("<i", rec, 4, new)
            keep.append(bytes(rec))
    return data[:at] + struct.pack("<i", len(keep)) + b"".join(keep) + data[at + 4 + count * size:]


def norm(path: str) -> str:
    return path.replace("\\", "/").lower()


def merge_types(pd2: Table, d2r: Table, wanted: dict[int, int], overrides: set[str]) -> tuple[Table, dict[int, dict[int, int]]]:
    """D2R's lvltypes with PD2's new types appended and, in D2R's types, the files the PD2-only presets select
    (wanted: type -> union of their Dt1Mask). Returns it and the moved slots: type -> {PD2 slot: mod slot} (0-based)."""
    cols = [d2r.header.index(f"File {k}") for k in range(1, SLOTS + 1)]
    pcol = {c.lower(): j for j, c in enumerate(pd2.header)}
    out = [list(r) for r in d2r.rows]
    recs, precs = d2r.records(), pd2.records()
    moved: dict[int, dict[int, int]] = {}

    def target(v: str) -> str:
        return f"{OVERRIDES}/{norm(v)}" if norm(v) in overrides else v
    for t, k in enumerate(precs):
        prow = pd2.rows[k]
        if t >= len(recs):  # a new type: PD2's row, at PD2's id, with only the files in-scope presets select
            # (D2R loads every file a type names at start-up; the left-out areas' files are not shipped)
            row = [(target(prow[pcol[c.lower()]]) if wanted.get(t, 0) >> cols.index(j) & 1 else "0") if j in cols
                   else prow[pcol[c.lower()]] if c.lower() in pcol else "" for j, c in enumerate(d2r.header)]
            if "Id" in d2r.header:
                row[d2r.header.index("Id")] = str(t)
            out.append(row)
            continue
        row = out[recs[t]]
        mask = wanted.get(t, 0)
        free = [s for s in range(SLOTS) if row[cols[s]] in ("", "0") and not mask >> s & 1]
        for s in range(SLOTS):
            v = prow[pcol[f"file {s + 1}"]]
            if not mask >> s & 1 or v in ("", "0"):
                continue
            v = target(v)
            if row[cols[s]] in ("", "0"):
                row[cols[s]] = v
            elif norm(row[cols[s]]) != norm(v):
                if not free:
                    raise ValueError(f"lvltypes {t}: no free File slot for {v}")
                to = free.pop(0)
                row[cols[to]] = v
                moved.setdefault(t, {})[s] = to
    if long := sorted({row[c] for row in out for c in cols if len(row[c]) > PATH_MAX}):
        raise ValueError(f"lvltypes: paths past D2R's {PATH_MAX} characters: {', '.join(long)}")
    merged = Table.__new__(Table)
    merged.header, merged.rows, merged.newline, merged.key = d2r.header, out, d2r.newline, 0
    return merged, moved


def move_bits(mask: int, moves: dict[int, int]) -> int:
    for s, to in moves.items():
        if mask >> s & 1:
            mask = (mask & ~(1 << s)) | (1 << to)
    return mask


def col(t: Table, name: str) -> int:
    return t.header.index(name)


AUTOMAP_TYPES = 36  # D2R's AutoMap.txt loader (0x32cae0) knows level types 0-35; core-ui answers the rest
# D2R's tile names in its order (table 0x1cfe350): the loader stores a row's TileName as its index here
TILES = ["fl", "wl", "wr", "wtlr", "wtll", "wtr", "wbl", "wbr", "wld", "wrd", "wle", "wre", "co", "sh", "tr", "rf",
         "ld", "rd", "fd", "fi"]
AUTOMAP_TABLE = "d2rloader/core-ui/automap.txt"  # the custom table core-ui registers (under global/excel)
ANY = "255"  # AutoMap.txt's -1 in Style or a sequence; core-ui's automap::Any
BLANK = ("", "-1")  # an AutoMap.txt cell with no value: no cel, or "any" in Style or a sequence


def automap_rows(pd2: Table, cels: int) -> list[list[str]]:
    """PD2's AutoMap.txt rows for level types 36 and up (LevelName is the type id), as core-ui's custom table (columns as
    in plugin/core-ui/automap.cpp Columns): numbers only, -1 ("any") as 255, and the cels packed and counted. A cel
    at or past `cels`, the automap tiles' frame count (pd2_automap), fails: stored in a character's map memory, it
    hangs the next save."""
    c = {n: col(pd2, n) for n in ("LevelName", "TileName", "Style", "StartSequence", "EndSequence")}
    cel_cols = [col(pd2, f"Cel{n}") for n in range(1, 5)]
    out = [["LevelType", "TileType", "Style", "StartSequence", "EndSequence", "CelCount", "Cel1", "Cel2", "Cel3", "Cel4"]]
    for r in pd2.rows:
        if not r[c["LevelName"]].isdigit() or int(r[c["LevelName"]]) < AUTOMAP_TYPES:
            continue
        if r[c["TileName"]] not in TILES:
            raise ValueError(f"automap: tile name {r[c['TileName']]!r} is not one of D2R's")
        drawn = [v for v in (r[i] for i in cel_cols) if v not in BLANK]
        if past := [v for v in drawn if int(v) >= cels]:
            raise ValueError(f"automap: cel {past[0]} is past the {cels} automap tiles")
        out.append([r[c["LevelName"]], str(TILES.index(r[c["TileName"]]))]
                   + [ANY if r[c[k]] in BLANK else r[c[k]] for k in ("Style", "StartSequence", "EndSequence")]
                   + [str(len(drawn))] + (drawn + ["0"] * 4)[:4])
    return out


def apply_files(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/level-files.tsv (see the module docstring)."""
    merge_dir = rule.parent
    drops = {}
    for line in rule.read_text("utf-8").splitlines():
        if line and not line.startswith("#"):
            ds1, kind, oid, _why = line.split("\t")
            drops[(norm(ds1), int(kind), int(oid))] = False
    lists = {t: read_list(pd2_ids.list_path(merge_dir, t)) for t in ("levels", "lvlprest", "objects")}
    mod_id = {t: {p.lower(): i for p, i, _ in ls} for t, ls in lists.items()}
    pd2_pr, d2r_pr = load(pd2_data, "lvlprest"), load(d2r_data, "lvlprest")
    pd2_lv = load(pd2_data, "levels")
    lvtype = {k: int(pd2_lv.rows[i][col(pd2_lv, "LevelType")] or 0) for k, i in enumerate(pd2_lv.records())}
    # the PD2-only presets: PD2 rows the lvlprest list appends
    presets = []
    for name, k in zip(ident(pd2_pr.names()), pd2_pr.records()):
        i = mod_id["lvlprest"][name.lower()]
        if i >= len(d2r_pr.records()):
            r = pd2_pr.rows[k]
            presets.append((i, lvtype[int(r[col(pd2_pr, "LevelId")])], int(r[col(pd2_pr, "Dt1Mask")] or 0),
                            [r[col(pd2_pr, f"File{n}")] for n in range(1, 7) if r[col(pd2_pr, f"File{n}")] not in ("", "0")]))
    tiles = pd2_data / "global" / "tiles"
    extracted = {norm(str(p.relative_to(tiles))) for p in tiles.rglob("*.dt1")} if tiles.is_dir() else set()
    if missing := OVERRIDDEN - extracted:
        raise ValueError(f"pd2assets.mpq no longer holds {sorted(missing)}: update OVERRIDDEN")
    overrides = OVERRIDDEN

    # lvltypes, and the presets' Dt1Mask bits that follow a moved file
    wanted: dict[int, int] = {}
    for _, t, mask, _ in presets:
        wanted[t] = wanted.get(t, 0) | mask
    d2r_types = load(d2r_data, "lvltypes")
    types, moved = merge_types(load(pd2_data, "lvltypes"), d2r_types, wanted, overrides)
    pd2_ids.write("lvltypes", types, [], out_data)
    automap = automap_rows(load(pd2_data, "automap"), pd2_automap.apply(pd2_data, d2r_data, out_data))
    dest = excel(out_data) / AUTOMAP_TABLE
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes("".join("\t".join(r) + "\n" for r in automap).encode("ascii"))  # D2RCore wants a final LF
    lp = Table(excel(out_data) / "lvlprest.txt")
    recs = lp.records()
    for i, t, mask, _ in presets:
        if t >= len(types.records()):
            raise ValueError(f"lvlprest {i}: level type {t} is past lvltypes")
        lp.rows[recs[i]][col(lp, "Dt1Mask")] = str(move_bits(mask, moved.get(t, {})))
    pd2_ids.write("lvlprest", lp, [], out_data)

    # the DT1 the presets load that D2R doesn't ship: PD2's own and the overrides at their new paths
    files = set()
    for _, t, mask, _ in presets:
        row = types.rows[types.records()[t]]
        for s in range(SLOTS):
            v = norm(row[col(types, f"File {s + 1}")])
            if move_bits(mask, moved.get(t, {})) >> s & 1 and v not in ("", "0"):
                files.add(v)
    for v in sorted(files):
        src = v[len(OVERRIDES) + 1:] if v.startswith(OVERRIDES + "/") else v
        if src in extracted:
            dest = out_data / "global" / "tiles" / v
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes((tiles / src).read_bytes())
        elif v.startswith("pd2assets/"):
            raise ValueError(f"lvltypes names {v}, which pd2assets.mpq does not hold")

    # object presets: D2R's, plus a row per object a rewritten DS1 places in an act that has none for it
    op = load(d2r_data, "objpreset")
    oc = {c: col(op, c) for c in ("Index", "Act", "ObjectClass")}
    d2r_obj = load(d2r_data, "objects")
    d2r_class = [n.lower() for n in d2r_obj.names()]
    flat = {}  # 1.13c's preset table: act * 150 + index -> PD2 objects id
    at = {}  # (act 1-5, mod objects Class) -> index
    nxt: dict[int, int] = {}
    for r in op.rows:
        a, idx, cls = int(r[oc["Act"]]), int(r[oc["Index"]]), r[oc["ObjectClass"]]
        if idx < 150:
            flat[(a - 1) * 150 + idx] = d2r_class.index(cls.lower())
        at.setdefault((a, cls.lower()), idx)
        nxt[a] = max(nxt.get(a, 0), idx + 1)
    flat |= {k: v for k, v in ACT1_SLOTS.items()}
    mod_class = [n for n in Table(excel(out_data) / "objects.txt").names()]
    pd2_objects = ident(load(pd2_data, "objects").names())
    pd2_mon = {act: ident(places) for act, places in acts(load(pd2_data, "monpreset")).items()}
    start, flat_mon = {}, []  # PD2's monpreset as one array in file order, as a negative monster id reads it
    for act, places in pd2_mon.items():
        start[act] = len(flat_mon)
        flat_mon += [(act, n) for n in places]
    mon = {p.lower(): i for p, i, _ in monpreset_lines(merge_dir, pd2_data, d2r_data)}
    mod_mon = acts(Table(excel(out_data) / "monpreset.txt"))
    pd2_monstats = load(pd2_data, "monstats").names()
    added, mon_added = [], 0

    def place_in(act: str, place: str) -> int:
        """The index of a monpreset row of `act` that places `place`, appended if the act has none."""
        nonlocal mon_added
        low = [n.lower() for n in mod_mon[act]]
        if place.lower() in low:
            return low.index(place.lower())
        mod_mon[act].append(place)
        mon_added += 1
        return len(low)

    def fix_for(ds1: str):
        def fix(act: int, kind: int, oid: int) -> int | None:
            if (ds1, kind, oid) in drops:
                drops[(ds1, kind, oid)] = True
                return None
            a = str(act + 1)
            if kind == 1:  # an index of the act's rows; below 0 an earlier act's (one array); past them a monstats row
                if 0 <= oid < len(pd2_mon[a]):
                    return mon[f"{a}/{pd2_mon[a][oid]}".lower()]
                if oid < 0 and start[a] + oid >= 0:
                    a2, name = flat_mon[start[a] + oid]
                    return place_in(a, mod_mon[a2][mon[f"{a2}/{name}".lower()]])
                if oid >= 0 and oid < len(pd2_monstats):
                    return place_in(a, pd2_monstats[oid])
                raise ValueError(f"{ds1}: monster {oid} (act {a}) names no row: list it in level-files.tsv")
            if kind != 2:
                return oid
            pd2_obj = oid - 150 if oid >= 150 else flat.get(act * 150 + oid)
            if pd2_obj in ENGINE_OBJECTS:
                mod = pd2_obj
            else:
                mod = mod_id["objects"].get(pd2_objects[pd2_obj].lower()) if pd2_obj is not None and pd2_obj < len(pd2_objects) else None
            if mod is None or mod == pd2_ids.LEFT_OUT:
                raise ValueError(f"{ds1}: object {oid} (act {a}) names no ported objects row: list it in level-files.tsv")
            key = (act + 1, mod_class[mod].lower())
            if key not in at:
                at[key] = nxt.get(act + 1, 0)
                nxt[act + 1] = at[key] + 1
                added.append([str(at[key]), str(act + 1), mod_class[mod]])
            return at[key]
        return fix

    for ds1 in sorted({norm(f) for *_, fs in presets for f in fs}):
        src = pd2_data / "global" / "tiles" / ds1
        if not src.is_file():
            raise ValueError(f"lvlprest names {ds1}, which pd2maps.mpq does not hold")
        dest = out_data / "global" / "tiles" / ds1
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(rewrite_ds1(src.read_bytes(), fix_for(ds1)))
    if unused := [k for k, used in drops.items() if not used]:
        raise ValueError(f"level-files.tsv lines no DS1 object matches: {unused}")
    for a, i, c in added:
        row = [""] * len(op.header)
        row[oc["Index"]], row[oc["Act"]], row[oc["ObjectClass"]] = a, i, c
        op.rows.append(row)
    pd2_ids.write("objpreset", op, [], out_data)
    mp = Table(excel(out_data) / "monpreset.txt")
    mp.rows = [[act, n] + [""] * (len(mp.header) - 2) for act, places in mod_mon.items() for n in places]
    pd2_ids.write("monpreset", mp, [], out_data)

    # PD2's new warps keep their Id (levels' Warp0-7 name it): it must not be one of D2R's
    d2r_warp, warp = load(d2r_data, "lvlwarp"), Table(excel(out_data) / "lvlwarp.txt")
    taken = {r[col(d2r_warp, "Id")] for r in d2r_warp.rows}
    clash = {r[col(warp, "Id")] for r in warp.rows[len(d2r_warp.rows):]} & taken
    if clash:
        raise ValueError(f"PD2's new lvlwarp rows reuse D2R's Id {sorted(clash)}: give them an id map")
    print(f"  level-files: {len(presets)} presets, {len({f for *_, fs in presets for f in fs})} DS1, {len(files)} DT1 "
          f"named, {len(added)} object and {mon_added} monster presets added, slots moved {moved}, "
          f"{len(automap) - 1} automap rows for types {AUTOMAP_TYPES}+")
