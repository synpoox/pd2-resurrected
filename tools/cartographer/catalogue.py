"""Cartographer's internal tileset catalogue (#939, epic #935; docs/plans/cartographer-outdoor-builder.md section 4 on
branch docs/map-kit-plan).

    python tools/cartographer/catalogue.py            write catalogue.tsv, tilesets.tsv and catalogue-report.txt here
    python tools/cartographer/catalogue.py --check    write nothing; exit 1 if the committed files differ (the gate)

Input: vanilla D2R's lvlprest.txt, the DS1s and DT1s of the same build (the d2rAI-toolkit's `d2r-data/93847`, or
MODKIT_CACHE), and overrides.tsv. One row per piece, with the columns of COLUMNS.

How a row is made, in the order the later step wins:

1. Name: a mask-named family (`Act 3 - Jungle NSE`, `Act 1 - Cave NW`, `Act 3 - Clearing Boggy S`) gets its exits from
   the name. Exit bits: W 1, E 2, S 4, N 8 (D2's own order: a maze or jungle piece is `base + mask`).
2. DS1: every piece with DS1s also gets its sockets from the tiles. A tile's 25 subtiles each block walking or not
   (DT1 flag bit 0); a side is `p` (open) where at least OPEN subtiles of the outermost subtile row of a macro-cell's
   span are walkable, else `c` (a cliff DT1 closes it) or `x` (closed). A family with no mask in its name (Barricade
   1-16) takes its sockets from this alone. For a named piece the DS1 mask is only a cross-check: `check` is `ok`,
   or `ds1=<mask>` where the DS1 disagrees (the name's edges stay), or `variants` where the piece's DS1 files differ.
3. overrides.tsv: hand-written cells win over both. A row names a `lvlprest` and any other column to set; a blank
   cell leaves the cell alone. `source` of an overridden row is `override`.

Which builder a tileset uses (`maze` or `outdoor`) is a property of the tileset (TILESETS), never of the author's map.
Sockets: `x` closed, `p` open (path or river: see the PR for #939), `c` cliff. Edge strings run west to east
(N and S) and north to south (W and E), one character per macro-cell.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from typing import NamedTuple

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE.parent))

from pd2_table import load, toolkit  # noqa: E402
from presets.dt1 import Tiles, header_dt1s  # noqa: E402
from presets.layout import Layout, layout  # noqa: E402

BUILD = "93847"
COLUMNS = ["tileset", "builder", "leveltype", "lvlprest", "name", "w", "h", "edgeN", "edgeE", "edgeS", "edgeW",
           "role", "weight", "tags", "source", "check", "ds1", "hd"]
TILESET_COLUMNS = ["tileset", "builder", "leveltype", "group", "cell", "pieces", "ready"]
BITS = {"W": 1, "E": 2, "S": 4, "N": 8}
MASK_RE = "(?P<mask>(?:N?S?E?W?))"  # the letters in D2's order; checked for non-empty below
OPEN = 5  # the walkable subtiles of a macro-cell's edge that make the side open: one tile
UNWALKABLE = 0x20000  # presets.consts.UNWALKABLE: the cell flag that marks a tile unwalkable
ROLES = ("path", "clearing", "entry", "boss", "event", "filler", "border")


class Spec(NamedTuple):
    """One tileset. `rows` are (lvlprest Name regex with {m} for the exit letters, role, tags)."""
    name: str
    builder: str  # "maze" or "outdoor"
    leveltype: int  # lvltypes Id
    group: str  # plan cartographer-plugin.md 3.5
    cell: int  # a macro-cell's side in tiles; 0 = the piece's own size
    rows: tuple


def maze(name: str, leveltype: int, prefix: str, group: str) -> Spec:
    return Spec(name, "maze", leveltype, group, 0, (
        (rf"^{prefix} {{m}}$", "path", ()),
        (rf"^{prefix} Theme {{m}}$", "filler", ("theme",)),
    ))


TILESETS: tuple[Spec, ...] = (
    maze("Cave", 3, "Act 1 - Cave", "maze, ready"),
    maze("Crypt", 4, "Act 1 - Crypt", "maze, ready"),
    maze("Lava", 28, "Act 4 - Lava", "maze, ready"),
    maze("Barracks", 7, "Act 1 - Barracks", "maze, one extra hook"),
    maze("Jail", 8, "Act 1 - Jail", "maze, one extra hook"),
    maze("Lair", 18, "Act 2 - Lair", "maze, one extra hook"),
    maze("Durance", 22, "Act 3 - Mephisto", "maze, one extra hook"),
    maze("Dungeon", 24, "Act 3 - Dungeon", "maze, one extra hook"),
    maze("Sewer", 25, "Act 3 - Sewer", "maze, one extra hook"),
    maze("Ice", 33, "Act 5 - Ice", "maze, one extra hook"),
    maze("Act 5 Lava", 35, "Act 5 - Lava", "maze, one extra hook"),
    maze("Arcane", 19, "Act 2 - Arcane", "maze, own growers"),
    maze("Catacombs", 10, "Act 1 - Catacombs", "maze, own growers"),
    maze("Tombs", 17, "Act 2 - Tomb", "maze, own growers"),
    maze("Act 2 Sewer", 13, "Act 2 - Sewer", "maze, own growers"),
    Spec("Jungle", "outdoor", 21, "outdoor, connectable pieces", 32, (
        (r"^Act 3 - Jungle {m}$", "path", ("river",)),
        (r"^Act 3 - Clearing (?P<kind>Webby|Boggy|Pygmy) {m}$", "clearing", ("clearing",)),
        (r"^Act 3 - Jungle (?P<kind>[NSEW]{{1,4}}) (?P<to>[NSEW])$", "border", ("transition",)),
        (r"^Act 3 - Jungle (?P<kind>Head|Tail)$", "border", ("transition",)),
    )),
    # No mask in the names: the sockets come from the DS1s alone (the 16 variants look like a 4-bit mask, to verify)
    Spec("Barricade", "outdoor", 31, "outdoor, connectable pieces", 16, (
        (r"^Act 5 - Barricade (?P<n>\d+)$", "path", ()),
    )),
    Spec("Snow", "outdoor", 31, "outdoor, connectable pieces", 16, (
        (r"^Act 5 - Barricade (?P<n>\d+) Snow$", "path", ()),
    )),
    # Listed, no pieces yet (plan 3.5): the tileset exists, a later phase makes its rows
    Spec("Siege", "outdoor", 30, "outdoor, connectable pieces", 16, ()),
    Spec("Kurast", "outdoor", 22, "outdoor, connectable pieces", 8, ()),
    Spec("Wilderness", "outdoor", 2, "outdoor, open fields", 0, ()),
    Spec("Desert", "outdoor", 16, "outdoor, open fields", 0, ()),
    Spec("Mesa", "outdoor", 27, "outdoor, open fields", 0, ()),
    Spec("Harem", "maze", 14, "maze, one extra hook", 0, ()),  # own small piece tables, not lvlprest runs
    Spec("Basement", "maze", 15, "maze, one extra hook", 0, ()),
    Spec("Spider", "maze", 23, "maze, one extra hook", 0, ()),
    Spec("Temple", "maze", 32, "maze, one extra hook", 0, ()),
)


# ---- inputs -----------------------------------------------------------------------------------------------------

def data_root() -> Path:
    return Path(os.environ.get("MODKIT_CACHE", toolkit(ROOT) / "d2r-data")) / BUILD / "data"


class Piece(NamedTuple):
    id: int
    name: str
    w_tiles: int
    h_tiles: int
    ds1: tuple[str, ...]  # lvlprest File1-6, lower case, forward slashes


def read_lvlprest(data: Path) -> dict[int, Piece]:
    t = load(data, "lvlprest")
    h = t.header
    out: dict[int, Piece] = {}
    for r in t.rows:
        if r[0] == "Expansion":
            continue
        files = tuple(r[h.index(f"File{k}")].replace("\\", "/").lower() for k in range(1, 7)
                      if r[h.index(f"File{k}")] not in ("", "0"))
        out[int(r[h.index("Def")])] = Piece(int(r[h.index("Def")]), r[0], int(r[h.index("SizeX")]),
                                            int(r[h.index("SizeY")]), files)
    return out


def hd_path(ds1: str) -> str:
    """The HD preset of a DS1, relative to the data folder: `act3/jungle/rivw.ds1` -> `hd/env/preset/act3/jungle/rivw.json`."""
    return "hd/env/preset/" + ds1.rsplit(".", 1)[0] + ".json"


# ---- reading a DS1 ----------------------------------------------------------------------------------------------

def ds1_sockets(lay: Layout, wt: int, ht: int, cell_w: int, cell_h: int, dt1_of=None) -> dict[str, str]:
    """The four sides of a piece of wt x ht tiles, cut into macro-cells of cell_w x cell_h tiles. A cell's side is
    `p` where OPEN or more subtiles of its outermost subtile row are walkable, else `c` where a cliff DT1 forms its
    edge tiles, else `x`."""
    def span(side: str, k: int, n: int):
        """The (tile, subtile index of the 5 in the tile) pairs of macro-cell k of n on a side."""
        if side in "NS":
            y, sy = (0, 0) if side == "N" else (ht - 1, 4)
            return [((x, y), [sy * 5 + sx for sx in range(5)]) for x in range(k * cell_w, (k + 1) * cell_w)]
        x, sx = (0, 0) if side == "W" else (wt - 1, 4)
        return [((x, y), [sy * 5 + sx for sy in range(5)]) for y in range(k * cell_h, (k + 1) * cell_h)]
    out = {}
    for side in "NESW":
        n = max(1, (wt // cell_w) if side in "NS" else (ht // cell_h))
        s = ""
        for k in range(n):
            open_ = 0
            cliff = False
            for tile, idx in span(side, k, n):
                key = lay.keys.get(tile)
                if key is None or tile in lay.unwalkable:
                    continue
                f = lay.flags.get(tile, bytes(25))
                open_ += sum(1 for i in idx if not f[i] & 1)
            for tile, _ in span(side, k, n):
                key = lay.keys.get(tile)
                if key and any(t[0] and "cliff" in t[0] for t in key[1]):
                    cliff = True
            s += "p" if open_ >= OPEN else ("c" if cliff else "x")
        out[side] = s
    return out


class Reader:
    """DS1 files under D2R's tiles folder -> Layout, with the DT1 flags of the DT1s their header lists."""
    def __init__(self, data: Path):
        self.tiles_root = data / "global" / "tiles"
        self.dt1 = Tiles(self.tiles_root)

    def layout(self, ds1: str) -> Layout | None:
        f = self.tiles_root / ds1
        if not f.is_file():
            return None
        data = f.read_bytes()
        return layout(data, self.dt1.of(header_dt1s(data)))


# ---- the rows ---------------------------------------------------------------------------------------------------

def mask_of(letters: str) -> int:
    return sum(BITS[c] for c in letters)


def letters_of(mask: int) -> str:
    return "".join(c for c in "NSEW" if mask & BITS[c])


def valid_mask(s: str) -> bool:
    """Exit letters in D2's order N S E W, each once, at least one."""
    return bool(s) and re.fullmatch("N?S?E?W?", s) is not None


def named_mask(spec_row: str, name: str) -> tuple[re.Match | None, str]:
    """(match, letters) of a Name against a spec's regex: `{m}` stands for the exit letters."""
    m = re.match(spec_row.replace("{m}", "(?P<m>[NSEW]{{1,4}})").replace("{{", "{").replace("}}", "}"), name)
    if m is None:
        return None, ""
    letters = m.groupdict().get("m") or ""
    return (m, letters) if not letters or valid_mask(letters) else (None, "")


def name_edges(mask: int) -> dict[str, str]:
    return {side: "p" if mask & BITS[side] else "x" for side in "NESW"}


def shape_tag(mask: int) -> str:
    n = bin(mask).count("1")
    if n == 1:
        return "deadend"
    if n == 4:
        return "cross"
    if n == 3:
        return "tee"
    return "straight" if mask in (3, 12) else "corner"


def build_rows(pieces: dict[int, Piece], reader: Reader | None, hd_exists=lambda p: True) -> list[dict]:
    """The catalogue rows before overrides, in tileset then lvlprest order."""
    rows = []
    for spec in TILESETS:
        for pid in sorted(pieces):
            p = pieces[pid]
            for pattern, role, extra in spec.rows:
                m, letters = named_mask(pattern, p.name)
                if m is None:
                    continue
                rows.append(make_row(spec, p, m, letters, role, extra, reader, hd_exists))
                break
    return rows


def make_row(spec: Spec, p: Piece, m: re.Match, letters: str, role: str, extra: tuple, reader: Reader | None,
             hd_exists) -> dict:
    cw = spec.cell or p.w_tiles or 1
    ch = spec.cell or p.h_tiles or 1
    w, h = max(1, p.w_tiles // cw), max(1, p.h_tiles // ch)
    tags = list(extra)
    row = {"tileset": spec.name, "builder": spec.builder, "leveltype": spec.leveltype, "lvlprest": p.id,
           "name": p.name, "w": w, "h": h, "role": role, "weight": 1, "source": "", "check": ""}
    ds1_edges = None
    if reader is not None and p.ds1:
        socks = []
        for f in p.ds1:
            lay = reader.layout(f)
            if lay is not None:
                socks.append(ds1_sockets(lay, p.w_tiles, p.h_tiles, cw, ch))
        if socks:
            ds1_edges = socks[0]
            if any(s != socks[0] for s in socks):
                row["check"] = "variants"
    if letters:
        mask = mask_of(letters)
        edges = name_edges(mask)
        row["source"] = "name"
        if w * h == 1:
            if ds1_edges is None:
                row["check"] = row["check"] or "no-ds1"
            else:
                got = sum(BITS[s] for s in "NESW" if ds1_edges[s] == "p")
                if got != mask:
                    row["check"] = f"ds1={letters_of(got) or '-'}"
                elif not row["check"]:
                    row["check"] = "ok"
        tags.append(shape_tag(mask))
        tags.append(f"exits={len(letters)}")
        kind = m.groupdict().get("kind")
        if kind:
            tags.append(kind.lower())
    else:
        edges = ds1_edges or {s: "x" * (w if s in "NS" else h) for s in "NESW"}
        row["source"] = "ds1" if ds1_edges else "none"
        row["check"] = "" if ds1_edges else "no-ds1"
        if m.groupdict().get("kind") in ("Head", "Tail"):
            tags.append(m.group("kind").lower())
        if m.groupdict().get("to"):
            tags.append(f"{m.group('kind').lower()}o{m.group('to').lower()}")
        if m.groupdict().get("n"):
            tags.append(f"variant={m.group('n')}")
    row.update(edgeN=edges["N"], edgeE=edges["E"], edgeS=edges["S"], edgeW=edges["W"])
    row["tags"] = ",".join(tags)
    row["ds1"] = ";".join(p.ds1)
    row["hd"] = ";".join(hd_path(f) if hd_exists(hd_path(f)) else "MISSING:" + hd_path(f) for f in p.ds1)
    return row


# ---- overrides ----------------------------------------------------------------------------------------------------

def read_overrides(path: Path) -> list[dict]:
    if not path.is_file():
        return []
    lines = [ln for ln in path.read_text(encoding="utf-8").split("\n") if ln.strip() and not ln.startswith("#")]
    if not lines:
        return []
    head = lines[0].split("\t")
    return [dict(zip(head, (ln.split("\t") + [""] * len(head))[:len(head)])) for ln in lines[1:]]


def apply_overrides(rows: list[dict], overrides: list[dict]) -> list[str]:
    """Set the overridden cells in place; returns the problems (an unknown piece or column, a bad role)."""
    by_id = {r["lvlprest"]: r for r in rows}
    problems = []
    for o in overrides:
        try:
            r = by_id[int(o["lvlprest"])]
        except (KeyError, ValueError):
            problems.append(f"overrides.tsv: lvlprest {o.get('lvlprest')!r} is not a catalogue row")
            continue
        for col, val in o.items():
            if col == "lvlprest" or val == "":
                continue
            if col not in COLUMNS or col in ("source", "check"):
                problems.append(f"overrides.tsv: lvlprest {r['lvlprest']}: column {col!r} cannot be set")
            elif col == "role" and val not in ROLES:
                problems.append(f"overrides.tsv: lvlprest {r['lvlprest']}: role {val!r} is not one of {ROLES}")
            else:
                r[col] = int(val) if col in ("w", "h", "weight", "leveltype") else val
                r["source"] = "override"
    return problems


# ---- output -------------------------------------------------------------------------------------------------------

def tsv(columns: list[str], rows: list[dict]) -> str:
    return "\n".join("\t".join(str(r[c]) for c in columns) for r in [dict(zip(columns, columns)), *rows]) + "\n"


def tileset_rows(rows: list[dict]) -> list[dict]:
    out = []
    for s in TILESETS:
        n = sum(1 for r in rows if r["tileset"] == s.name)
        out.append({"tileset": s.name, "builder": s.builder, "leveltype": s.leveltype, "group": s.group,
                    "cell": s.cell or "piece", "pieces": n, "ready": "pieces" if n else "no-pieces"})
    return out


def report(rows: list[dict]) -> str:
    lines = [f"{len(rows)} pieces in {len({r['tileset'] for r in rows})} tilesets"]
    bad = [r for r in rows if r["check"] not in ("ok", "")]
    lines.append(f"{len(bad)} pieces whose check is not ok:")
    lines += [f"  {r['lvlprest']:>5} {r['name']}: {r['check']}" for r in bad]
    miss = [(r["lvlprest"], r["name"], h) for r in rows for h in r["hd"].split(";") if h.startswith("MISSING:")]
    lines.append(f"{len(miss)} missing HD presets:")
    lines += [f"  {i:>5} {n}: {h[8:]}" for i, n, h in miss]
    jun = [r for r in rows if r["tileset"] == "Jungle" and 530 <= r["lvlprest"] <= 604]
    ok = sum(1 for r in jun if r["check"] == "ok")
    lines.append(f"LvlPrest 530-604: {len(jun)} rows, {ok} where the DS1 mask equals the name's")
    return "\n".join(lines) + "\n"


def generate(data: Path | None = None) -> tuple[dict[str, str], list[str]]:
    """({file name: text}, problems) for the data folder (D2R's by default)."""
    data = data or data_root()
    pieces = read_lvlprest(data)
    rows = build_rows(pieces, Reader(data), lambda p: (data / p).is_file())
    problems = apply_overrides(rows, read_overrides(HERE / "overrides.tsv"))
    return {"catalogue.tsv": tsv(COLUMNS, rows), "tilesets.tsv": tsv(TILESET_COLUMNS, tileset_rows(rows)),
            "catalogue-report.txt": report(rows)}, problems


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="write nothing; fail if the committed files differ")
    ap.add_argument("--data", type=Path, help="the data folder (default: D2R's, from the toolkit)")
    args = ap.parse_args(argv)
    files, problems = generate(args.data)
    for p in problems:
        print(p, file=sys.stderr)
    if problems:
        return 1
    if args.check:
        stale = [n for n, text in files.items() if not (HERE / n).is_file() or (HERE / n).read_bytes().decode() != text]
        if stale:
            print(f"stale: {', '.join(stale)}: run python tools/cartographer/catalogue.py and commit the result",
                  file=sys.stderr)
            return 1
        return 0
    for n, text in files.items():
        (HERE / n).write_bytes(text.encode())
    print(files["catalogue-report.txt"], end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
