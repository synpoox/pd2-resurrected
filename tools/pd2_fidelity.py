"""Fidelity scores of the generated HD presets against their legacy DS1s, per edge type and floor (#732, part of #707).

The #707 bar is "matches legacy", not coverage %. For each PD2-only map (the DS1s the HD preset generator builds, #213)
this compares the HD preset with the DS1/DT1 tile by tile and scores, per edge type and for floors:

- missing: legacy has the edge (or floor), the HD preset has no piece there;
- wrong type: a piece stands there, but not of the legacy type (an ice cliff where legacy has a low wall);
- wrong height: the right type, but of the wrong height class (low: a fence, a stone wall; tall: a cliff, a wall, a
  pillar, a tree);
- spacing: along an edge run, bare holes between pieces and pieces piled on each other.

Edge types and how legacy tiles are classified (classify, the shared classifier), from the PD2 install's DT1 files, measured over their
headers (96-byte tile records, as pd2_presets.dt1_tiles reads them: orientation at +20, the tile's pixel height at +8):

- walls, cliffs, fences: a wall orientation (WALL_ORIENT: cliff1/cliff2/final_cliff*/a5_cliff_parts hold only 1-7, 10,
  11 and the lower variants 16-19; fence, stonewall, stone_fence and sewerwalls hold 1-7; 10 and 11 also appear in
  warp, waypoint and roof tiles) told apart by the DT1's name
  (INVISIBLE, FENCE, CLIFF; the rest are walls), and a cell with an upper over a lower wall tile is a cliff (cliff_cell);
- pillars: orientation 12 (pd2_presets.LIKE: "a PD2 DT1's pillar"; stone_fence, sewerchamb and seweritems hold their
  columns there);
- treelines: orientation 14 (pd2_presets.TREE_LAYER) or a wall tile of a tree DT1 (pd2_presets.draws_trees names them
  by "tree"); orientation 13 is a tree DT1's shadow (91-120 tiles at height 0 to -128) and is left out;
- water and lava edges: a wall layer of a river/pond/rivbank/pool DT1 or lava's extwalls/intwalls/rocks (classify), or
  a liquid floor tile (a mostly blocked floor, pd2_painter.blocked_floor, of a liquid DT1) with a 4-neighbour that is
  not liquid.

Heights: a wall's height class comes from its DT1 (wall_px: the median pixel height of the DT1's wall tiles, weighted
by how often the map places each; the tile's own height when the DT1 is mixed, MIXED): low when that is under LOW_PX
(see there) or the DT1 is a fence/stone wall by name (pd2_coverage.LOW_EDGE: low by design), else tall. Cliffs and
trees are always tall (384-480, 256-480 px). A piece's height is its model box top (pd2_coverage.boxes): flat below
pd2_coverage.LOW, low below TALL, else tall.

HD pieces are typed by their model's file name (model_type). A tall piece of no edge type is "other": it counts as
present, and as the wrong type for every legacy edge.

Floors: the DS1's walkable floor (pd2_coverage.walkable_floor). Missing: under no terrain box; wrong type: under a
structure (stairs, a wall; pd2_coverage.wrong_tiles). Height and spacing are not scored for floors: the terrain box of a
pasted room is the room's own, not a per-tile surface, so a floor tile's height cannot be told from it.

Outputs, from pd2_coverage.run: <out>/fidelity.md, <out>/fidelity-baseline.tsv (main's scores; `map type n score
spacing`, percentages), and per map <name>_fidelity.png. Needs the same inputs as pd2_coverage; no game files change.
"""
from __future__ import annotations

import hashlib
import inspect
import json
import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

import pd2_coverage as coverage
import pd2_painter as painter
import pd2_presets as presets
from pd2_tiles import (  # noqa: F401  (moved there, #855: the generator imports it; re-exported)
    Tile, EDGES, TYPES, WALL_ORIENT, HEIGHT_ORIENT, UPPER_ORIENT, LOWER_ORIENT, PILLAR_ORIENT, TREE_ORIENT, INVISIBLE,
    FENCE, TALL_FENCE, CLIFF, TREE, WATER_EDGE, LIQUID, LOW_PX, MIXED, LOW_NAME, HOLE_PAIRS, STACK, STEPS8, HD_MODEL,
    GROUND_COVER, folder_of, model_type, model_types, dt1_heights, Heights, classify, cliff_cell, tile_type, wall_px,
    expected_class, liquid_floor, legacy_edges, Hd, hd_pieces, hd_class, runs, TypeScore, score_type,
)

CULPRITS = 8  # the folders listed under the all-maps table, per type
TOLERANCE = coverage.TOLERANCE  # the gate's one limit, in pd2_coverage
TILES_FILE = "fidelity-tiles-baseline.tsv"  # main's per-tile state, beside fidelity-baseline.tsv (#800)
HOLE_KIND = "hole beside new correct piece"
SPILL_KIND = "spill-only cover on main"
OTHER_KIND = "other"
MIN_TILES = 20  # a (map, type) of fewer legacy tiles never counts a spacing drop: one hole moves it by whole points
# The stock vote (stock_vote) types a folder by the commonest legacy edge type it stands on (stock_types), when at
# least STOCK_SHARE of its edge tiles are that type and there are STOCK_MIN of them.
STOCK_MIN, STOCK_SHARE, STOCK_EDGE = 10, 0.5, 0.1
VOTES_FILE = Path(__file__).with_name("hd-folder-types.tsv")  # the stock vote, committed (stock_vote; `--write-votes`)



def stock_vote(d2r: Path, box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]]) -> dict:
    """For each HD model folder, what D2R's own stock presets stand it on (#822): folder -> {legacy edge type or "none":
    tiles}. Over every stock preset with a DS1, hd_pieces finds the pieces that stand as edges (typed by name, or tall)
    and the tallest piece's folder on each tile votes for that tile's legacy edge type (legacy_edges; the first of
    EDGES on a tile holding several), or "none" on a tile with no edge. A prop kit under 4 units never votes: it
    stands beside walls without being one. Needs the D2R install (`d2r`: its data folder)."""
    tiles, heights = presets.Tiles(d2r / "global" / "tiles"), Heights(d2r / "global" / "tiles")
    votes: dict[str, dict[str, int]] = {}
    root = d2r / presets.PRESETS
    for p in sorted(root.rglob("*.json")):
        ds1 = d2r / "global" / "tiles" / p.relative_to(root).with_suffix(".ds1")
        if not ds1.is_file():
            continue
        data = ds1.read_bytes()
        try:
            lay = presets.layout(data, tiles.of(presets.header_dt1s(data)))
            hd = hd_pieces(json.loads(p.read_text("utf-8-sig")).get("entities", []), box, prefab)
        except (ValueError, struct.error, KeyError):  # DS1 versions below 4, a preset that is no entity list
            continue
        edges: dict[Tile, str] = {}
        for k, cs in legacy_edges(lay, heights).items():
            for c in cs:
                edges.setdefault(c, k)
        for c, (_, folder) in hd.who.items():
            v = votes.setdefault(folder, {})
            k = edges.get(c, "none")
            v[k] = v.get(k, 0) + 1
    return votes


def stock_types(votes: dict) -> dict[str, str]:
    """The folders the stock vote types (stock_vote's counts): folder -> the commonest legacy edge type under it, for
    those with STOCK_MIN edge tiles, STOCK_EDGE of their tiles on edges, and STOCK_SHARE of the edge tiles that type.
    A prop kit that hugs walls (under STOCK_EDGE) stays untyped. A low piece of a voted folder gets edge credit by
    its folder with no height check (only name-typed pieces are height-scored)."""
    out = {}
    for f, v in votes.items():
        edge = {k: n for k, n in v.items() if k in EDGES}
        if edge and (e := sum(edge.values())) >= STOCK_MIN and e >= STOCK_EDGE * sum(v.values()):
            k = max(sorted(edge), key=edge.get)
            if edge[k] >= STOCK_SHARE * e:
                out[f] = k
    return out


def votes_input_hash(d2r: Path) -> str:
    """A hash of what the stock vote is made from: the typing code (the folder and model typing, the legacy edges, the
    vote and the piece stand) and D2R's stock preset list. A changed hash means hd-folder-types.tsv is stale."""
    code = [inspect.getsource(f) for f in (folder_of, model_type, model_types, legacy_edges, liquid_floor, stock_vote,
                                           Hd.add, hd_pieces)]
    code += [repr([(t, rx.pattern) for t, rx in HD_MODEL]), GROUND_COVER.pattern, repr(coverage.TALL)]
    root = d2r / presets.PRESETS
    names = sorted(p.relative_to(root).as_posix() for p in root.rglob("*.json"))
    return hashlib.sha256("\n".join(code + names).encode("utf-8")).hexdigest()[:16]


def votes_text(votes: dict, digest: str = "") -> str:
    head = ("# HD folder tiles by the legacy edge type under them in D2R's stock presets (tools/pd2_fidelity.py "
            "--write-votes): folder, then " + ", ".join(("none",) + EDGES))
    return "\n".join([head, f"# input: {digest}"] + [f"{f}\t" + "\t".join(str(v.get(k, 0)) for k in ("none",) + EDGES)
                                                     for f, v in sorted(votes.items())]) + "\n"


def votes_stale(text: str, digest: str) -> str | None:
    """A warning when the vote file was made from other typing code or stock presets than `digest` (votes_input_hash)."""
    have = next((l.split(":", 1)[1].strip() for l in text.splitlines() if l.startswith("# input:")), "")
    if have == digest:
        return None
    return (f"{VOTES_FILE.name} is stale (made from input {have or 'unknown'}, now {digest}): the typing code or the stock "
            f"presets changed; rerun `py -3 tools/pd2_fidelity.py --write-votes`")


def read_votes(text: str) -> dict:
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            f, *counts = line.split("\t")
            out[f] = {k: int(n) for k, n in zip(("none",) + EDGES, counts) if int(n)}
    return out



def wall_cover(legacy: dict[str, dict[Tile, str | None]], hd: Hd, tall: set[Tile]) -> set[Tile]:
    """The DS1 wall tiles the coverage wall score counts as drawn (#787), by the fidelity classes: a legacy edge tile
    (legacy_edges) is drawn when an HD piece of the height class it asks for ("low", "tall"; any piece for water)
    stands on it, whatever the piece's type; of a tile holding several edge types, one right is enough. A wall tile
    with no edge type (invisible blockers, a layer classify() skips) keeps the old rule: under a `tall` tile."""
    classes: dict[Tile, set[str]] = {}
    for tops in list(hd.nametops.values()) + [hd.other, hd.stocktops]:
        for c, top in tops.items():
            classes.setdefault(c, set()).add(hd_class(top))
    typed = {c for tiles in legacy.values() for c in tiles}
    right = {c for tiles in legacy.values() for c, want in tiles.items()
             if (want in classes.get(c, ())) or (want is None and classes.get(c, set()) - {"flat"})}
    return right | (tall - typed)


def score_map(lay: presets.Layout, heights: dict[str, dict], hd: Hd, floors: int, bare: set, wrong: set
              ) -> dict[str, TypeScore]:
    """Every type of one map that the legacy DS1 has (floor first-class, but without height and spacing)."""
    out = {k: score_type(k, tiles, hd) for k, tiles in legacy_edges(lay, heights).items() if tiles}
    if floors:
        out["floor"] = TypeScore(floors, set(bare), set(wrong - bare), scored=False)
    return out


COLOURS = {"ok": (60, 210, 60), "missing": (230, 40, 40), "type": (240, 150, 30), "height": (235, 220, 40),
           "hole": (230, 40, 200)}


def image(lay: presets.Layout, legacy: dict[str, dict[Tile, str | None]], scores: dict[str, TypeScore],
          name: str) -> dict[str, bytes]:
    """<name>_fidelity.png: the legacy edge tiles green where right, red missing, orange the wrong type, yellow the
    wrong height, magenta a hole in a run; the rest of the DS1 dim."""
    px = {c: (45, 42, 36) for c in lay.keys}
    for kind, tiles in legacy.items():
        s = scores.get(kind)
        if not s:
            continue
        for c in tiles:
            px[c] = (COLOURS["hole"] if c in s.holes else COLOURS["missing"] if c in s.missing
                     else COLOURS["type"] if c in s.wrong_type else COLOURS["height"] if c in s.wrong_height
                     else COLOURS["ok"])
    return {f"{name}_fidelity.png": coverage.png(lay.width, lay.height, px, zoom=4)}


# --- reports -----------------------------------------------------------------------------------------------------

Rows = dict[str, dict[str, TypeScore]]  # map -> type -> score


def pct(v: float | None) -> float | None:
    return None if v is None else round(100 * v, 1)


def baseline_text(rows: Rows) -> str:
    lines = ["# HD fidelity on main (tools/pd2_fidelity.py): map, type, legacy tiles, score %, spacing % or -"]
    for name in sorted(rows):
        for kind in TYPES:
            if s := rows[name].get(kind):
                sp = pct(s.spacing)
                lines.append(f"{name}\t{kind}\t{s.n}\t{pct(s.score)}\t{'-' if sp is None else sp}")
    return "\n".join(lines) + "\n"


def read_baseline(text: str) -> dict[tuple[str, str], tuple[float, float | None]]:
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            name, kind, _n, score, spacing = line.split("\t")
            out[name, kind] = (float(score), None if spacing == "-" else float(spacing))
    return out


@dataclass
class Change:
    name: str
    kind: str
    score: tuple[float | None, float | None]  # (baseline, now)
    spacing: tuple[float | None, float | None]
    drop: bool
    within: bool = False  # fell, but by no more than the tolerance: listed, passes
    small: bool = False  # under MIN_TILES legacy tiles: its spacing is not checked


def changes(rows: Rows, base: dict, tolerance: float = TOLERANCE) -> list[Change]:
    """The (map, type) pairs whose score or spacing differ from the baseline, worst drop first. A pair the baseline
    has and the run no longer scores is a drop; a pair of under MIN_TILES legacy tiles is exempt for its spacing, not
    its score."""
    now = {(n, k): (pct(s.score), pct(s.spacing)) for n, r in rows.items() for k, s in r.items()}
    small = {(n, k) for n, r in rows.items() for k, s in r.items() if s.n < MIN_TILES}
    out = []
    for key in sorted(set(now) | set(base)):
        (s0, p0), (s1, p1) = base.get(key, (None, None)), now.get(key, (None, None))
        if key in base and key in now and (s0, p0) == (s1, p1):
            continue
        # a small pair is exempt from spacing only (one hole moves it by whole points), never from its score
        pairs = ((s0, s1),) if key in small else ((s0, s1), (p0, p1))
        fell = any(o is not None and n is not None and o - n > tolerance for o, n in pairs)
        drop = key not in now or fell or (key not in small and p0 is not None and p1 is None)
        within = not drop and any(o is not None and n is not None and o > n for o, n in pairs)  # a small pair's spacing is not checked
        out.append(Change(*key, (s0, s1), (p0, p1), drop, within, key in small))

    def worst(c: Change) -> float:
        return min((n - o for o, n in (c.score, c.spacing) if o is not None and n is not None), default=0.0)
    return sorted(out, key=lambda c: (not c.drop, worst(c), c.name, c.kind))


def by_type(found: list[Change]) -> dict[str, int]:
    return {k: sum(c.drop for c in found if c.kind == k) for k in TYPES if any(c.drop and c.kind == k for c in found)}


def changes_md(found: list[Change], tolerance: float = TOLERANCE) -> str:
    def cell(old, new) -> str:
        if old is None and new is None:
            return "-"
        if old is None:
            return f"new {new}%"
        if new is None:
            return f"{old}% -> gone"
        return f"{old}% -> {new}% ({new - old:+.1f})" if old != new else f"{new}%"
    lines = ["## Fidelity changes against the baseline", ""]
    if not found:
        return "\n".join(lines + ["No map's fidelity changed.", ""])
    drops = by_type(found)
    inside = sum(c.within for c in found)
    lines += [f"{len(found)} (map, type) scores changed; {sum(drops.values())} dropped by more than the "
              f"{tolerance:g}-point tolerance or are no longer scored"
              + (": " + ", ".join(f"{k} {n}" for k, n in drops.items()) if drops else "")
              + (f"; {inside} fell within the tolerance" if inside else "") + ".", "",
              "| Map | Type | Score | Spacing | |", "|---|---|---|---|---|"]
    lines += [f"| {c.name} | {c.kind} | {cell(*c.score)} | {cell(*c.spacing)} | {'**drop**' if c.drop else 'within tolerance' if c.within else ''} |"
              for c in found]
    return "\n".join(lines) + "\n"


def report(rows: Rows, commit: str = "", found: list[Change] | None = None, tolerance: float = TOLERANCE) -> str:
    p = lambda v: "-" if v is None else f"{100 * v:.1f}%"  # noqa: E731
    lines = ["# HD fidelity against legacy" + (f" ({commit})" if commit else ""), "",
             f"{len(rows)} generated maps. Per edge type and floor: share of legacy tiles the HD preset gets right "
             "(score), the tiles it misses, draws as another type, or at the wrong height class, and spacing (holes "
             "in an edge run and piled pieces). Floors score missing and wrong type only. Overlays: "
             "`<map>_fidelity.png`, green right, red missing, orange wrong type, yellow wrong height, magenta a hole.",
             ""]
    if found is not None:
        lines += [changes_md(found, tolerance), ""]
    lines += ["## All maps", "", "| Type | Legacy tiles | Score | Missing | Wrong type | Wrong height | Spacing |",
              "|---|---|---|---|---|---|---|"]
    for kind in TYPES:
        ss = [r[kind] for r in rows.values() if kind in r]
        if n := sum(s.n for s in ss):
            wrong = sum(len(s.missing | s.wrong_type | s.wrong_height) for s in ss)
            sp = sum(len(s.holes) + s.piled for s in ss)
            lines.append(f"| {kind} | {n} | {p(1 - wrong / n)} | {sum(len(s.missing) for s in ss)} | "
                         f"{sum(len(s.wrong_type) for s in ss)} | {sum(len(s.wrong_height) for s in ss)} | "
                         + (p(max(0.0, 1 - sp / n)) if ss[0].scored else "-") + " |")
    for kind in EDGES:
        total: dict[str, int] = {}
        for r in rows.values():
            for f, k in (r[kind].culprits if kind in r else {}).items():
                total[f] = total.get(f, 0) + k
        if total:
            top = sorted(total.items(), key=lambda kv: (-kv[1], kv[0]))[:CULPRITS]
            lines += ["", f"Wrong {kind}: " + ", ".join(f"`{f}` {k}" for f, k in top)]
    lines += ["", "## Per map", "", "| Map | Type | Legacy tiles | Score | Missing | Wrong type | Wrong height | Spacing |",
              "|---|---|---|---|---|---|---|---|"]
    for name in sorted(rows, key=lambda n: (min((s.score for s in rows[n].values() if s.score is not None), default=1), n)):
        for kind in TYPES:
            if s := rows[name].get(kind):
                lines.append(f"| {name} | {kind} | {s.n} | {p(s.score)} | {len(s.missing)} | {len(s.wrong_type)} | "
                             f"{len(s.wrong_height)} | {p(s.spacing)} |")
    return "\n".join(lines) + "\n"


def tiles_text(rows: Rows) -> str:
    """Per (map, type), the tiles that are bad (missing, wrong type or height), the holes and the spill-only right
    tiles, one line each: `map <tab> type <tab> field <tab> x,y x,y ...`. main's copy lets a PR tell what a dropped
    tile is (classify_drop)."""
    lines = ["# HD fidelity tiles on main (tools/pd2_fidelity.py): map, type, bad|holes|spill, tiles x,y"]
    for name in sorted(rows):
        for kind in TYPES:
            if s := rows[name].get(kind):
                for field_, tiles in (("bad", s.missing | s.wrong_type | s.wrong_height), ("holes", s.holes),
                                      ("spill", s.spill)):
                    if tiles:
                        lines.append(f"{name}\t{kind}\t{field_}\t" + " ".join(f"{x},{y}" for x, y in sorted(tiles)))
    return "\n".join(lines) + "\n"


def read_tiles(text: str) -> dict[tuple[str, str], dict[str, set]]:
    out: dict = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            name, kind, field_, tiles = line.split("\t")
            out.setdefault((name, kind), {})[field_] = {tuple(map(int, t.split(","))) for t in tiles.split()}
    return out


def classify_drop(spacing: bool, now: TypeScore, main: dict | None) -> dict[str, int]:
    """What the tiles that newly dropped are against main's tiles `main` (read_tiles' entry of the pair; None: main's
    tiles are not known, no answer). A dropped tile is a new hole (spacing) or a newly bad tile (score). It is
    HOLE_KIND when it was already wrong on main and a neighbour went from bad to right (a new correct piece stands
    beside it), else SPILL_KIND when main covered it right only through a neighbouring box's
    spill, else OTHER_KIND."""
    if main is None:
        return {}
    bad = now.missing | now.wrong_type | now.wrong_height
    dropped = now.holes - main.get("holes", set()) if spacing else bad - main.get("bad", set())
    fixed = main.get("bad", set()) - bad  # right now, bad on main: the new correct pieces
    out: dict[str, int] = {}
    for c in dropped:
        near = c in main.get("bad", set()) and any((c[0] + dx, c[1] + dy) in fixed for dx, dy in STEPS8)
        k = HOLE_KIND if near else SPILL_KIND if c in main.get("spill", set()) else OTHER_KIND
        out[k] = out.get(k, 0) + 1
    return out


def kinds_text(kinds: dict[str, int]) -> str:
    return "-" if not kinds else ", ".join(f"{n} {k}" for k, n in sorted(kinds.items(), key=lambda kv: (-kv[1], kv[0])))


def write(out: Path, rows: Rows, commit: str = "", base: dict | None = None, tolerance: float = TOLERANCE
          ) -> list[Change] | None:
    """<out>/fidelity.md, fidelity-baseline.tsv and fidelity-tiles-baseline.tsv; the changes against `base`
    (read_baseline) when given."""
    found = None if base is None else changes(rows, base, tolerance)
    (out / TILES_FILE).write_text(tiles_text(rows), "utf-8", newline="\n")
    (out / "fidelity.md").write_text(report(rows, commit, found, tolerance), "utf-8")
    (out / "fidelity-baseline.tsv").write_text(baseline_text(rows), "utf-8", newline="\n")
    if found is not None:
        (out / "fidelity-changes.md").write_text(changes_md(found, tolerance), "utf-8")
    return found


def main(argv: list[str] | None = None) -> int:
    import argparse
    ap = argparse.ArgumentParser(description="Rebuild the committed stock vote (hd-folder-types.tsv) from D2R's stock presets.")
    ap.add_argument("--write-votes", action="store_true", help=f"write {VOTES_FILE.name} (needs the D2R install and model_lod_desc.bin)")
    ap.add_argument("--check-votes", action="store_true", help=f"exit 1 when {VOTES_FILE.name} is stale (the gate runs this, #891)")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA, help=f"D2R data root (default: {coverage.D2R_DATA})")
    a = ap.parse_args(argv)
    if a.check_votes:
        warn = votes_stale(VOTES_FILE.read_text("utf-8"), votes_input_hash(a.d2r))
        print(warn or f"pd2_fidelity: {VOTES_FILE.name} is current")
        return 1 if warn else 0
    if not a.write_votes:
        ap.print_help()
        return 0
    box, prefab = coverage.model_lookup(a.d2r)
    votes = stock_vote(a.d2r, box, prefab)
    VOTES_FILE.write_text(votes_text(votes, votes_input_hash(a.d2r)), "utf-8", newline="\n")
    print(f"pd2_fidelity: {len(votes)} folders voted, {len(stock_types(votes))} typed, {VOTES_FILE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
