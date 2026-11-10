"""Edge tour (#733): warp points along every PD2-only map's edges and floors, and HD/legacy screenshot pairs there.

#707 compares the generated HD presets of the PD2-only areas with legacy along each map's edges and over each floor
type. Walking there is slow, so this plans warp points from a map's DS1 and `levels.txt`, then drives a game slot to
each and saves the HD shot beside the legacy shot (`g` toggles).

Edge types (a run is a 8-connected group of edge tiles; a point stands on the walkable tile 4-next to one):
  wall    DS1 wall tiles (not the low fence and shore kinds below)
  fence   tiles whose every layer draws a fence, palisade, picket, stone wall or graveyard DT1 (pd2_coverage.low_edge)
  shore   the other low_edge tiles: river banks, pools, swamp, ponds, puddles
  water   blocked floor tiles (water, and the ground past a map's edge walls) next to walkable floor
  ring    for a DS1 that draws trees: the tiles 4-next to and outside pd2_painter.playable (the border ring)
Floor types: `floor:<stem>`, the walkable tiles whose first floor tile is in DT1 `<stem>`, interior ones first.

A point's command is `cheat goto <levelId>,<x>,<y>` with x = OffsetX*5 + tileX*5 + 2 (y alike), OffsetX/OffsetY the
level's row in the merged levels.txt (generated/data/global/excel/). A map whose preset lists several DS1 rolls one per
game: the plan covers each, named by DS1, and the shot lands on the wanted tiles only when that variant loaded.

    py -3 tools/pd2_edge_tour.py list  --map "Torture Halls" [--type wall] [--points 4]
    py -3 tools/pd2_edge_tour.py shoot --slot 7 --map "Torture Halls" --type wall [--points 4]

`list` needs the merged data (pd2_merge.py) and D2R's tiles in modkit's cache (like pd2_coverage.py). `shoot` needs the
slot locked and its game running in a game (Hell, level 90+); see docs/agents/game-slots.md. It reuses d2rAI-toolkit's
testing/game_ctl.py for the console, keys and window screenshots, and writes <out>/edge-tour/<map>/<name>.png: HD on
the left, legacy on the right. Not done: weighting the points by the fidelity scores' worst tiles.
"""
from __future__ import annotations

import argparse
import dataclasses
import math
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import pd2_coverage as coverage
import pd2_painter as painter
import pd2_presets as presets
from pd2_ids import Table, excel, load, toolkit
from pd2_levels import col

ROOT = Path(__file__).resolve().parent.parent
Tile = tuple[int, int]

FENCE = re.compile(r"fence|palisade|picket|stonewall|graveyrd")  # the low_edge DT1 kinds that are a fence
EDGES = ("wall", "fence", "shore", "water", "ring")
AROUND = [(i, j) for i in (-1, 0, 1) for j in (-1, 0, 1) if i or j]
SETTLE = 3.0  # seconds after a warp before the HD shot (the level loads; a same-level warp is quick)
TOGGLE = 1.5  # seconds after `g` before the shot


# ---------------------------------------------------------------------------------------------------- warp points

def subtile(offset: int, tile: int) -> int:
    """The world subtile at the middle of a tile (+2: the middle of its 5 subtiles)."""
    return offset * 5 + tile * 5 + 2


def goto(level: int, offset: tuple[int, int], tile: Tile) -> str:
    """The console line that warps to a DS1 tile of a level whose levels.txt row has `offset` (OffsetX, OffsetY)."""
    return f"cheat goto {level},{subtile(offset[0], tile[0])},{subtile(offset[1], tile[1])}"


def offsets(out_data: Path) -> dict[int, tuple[str, int, int]]:
    """levels.txt: Id -> (Name, OffsetX, OffsetY)."""
    t = Table(excel(out_data) / "levels.txt")
    i, n, x, y = (col(t, c) for c in ("Id", "Name", "OffsetX", "OffsetY"))
    return {int(r[i]): (r[n], int(r[x] or 0), int(r[y] or 0)) for r in t.rows if r[i].strip().isdigit()}


# ---------------------------------------------------------------------------------------------------- edges and floors

def components(tiles: set[Tile]) -> list[list[Tile]]:
    """The 8-connected groups of tiles, longest first (ties by position), each sorted."""
    left, out = set(tiles), []
    while left:
        stack, group = [min(left)], []
        left.discard(stack[0])
        while stack:
            x, y = stack.pop()
            group.append((x, y))
            for dx, dy in AROUND:
                if (n := (x + dx, y + dy)) in left:
                    left.discard(n)
                    stack.append(n)
        out.append(sorted(group))
    return sorted(out, key=lambda g: (-len(g), g[0]))


def spread(tiles: list[Tile], k: int) -> list[Tile]:
    """Up to k of the tiles, spread out: the one nearest the centre, then each time the one farthest from those taken."""
    if not tiles:
        return []
    cx, cy = sum(t[0] for t in tiles) / len(tiles), sum(t[1] for t in tiles) / len(tiles)
    picked = [min(tiles, key=lambda t: (math.hypot(t[0] - cx, t[1] - cy), t))]
    while len(picked) < min(k, len(tiles)):
        picked.append(max((t for t in tiles if t not in picked),
                          key=lambda t: (min(max(abs(t[0] - p[0]), abs(t[1] - p[1])) for p in picked), t)))
    return picked


def edge_tiles(lay: presets.Layout, wet: frozenset = frozenset()) -> dict[str, set[Tile]]:
    """The edge tiles of each type (EDGES) that have walkable floor 4-next to them."""
    walk = presets.walkable(lay)
    low = {c for c in lay.keys if coverage.low_edge(lay.keys[c])}
    found = {
        "wall": {c for c, k in lay.keys.items() if k[0] == "w"} - low,
        "fence": {c for c in low if any(FENCE.search((t[0] or "").rsplit("/", 1)[-1]) for t in lay.keys[c][1])},
    }
    found["shore"] = low - found["fence"]
    found["water"] = {c for c, k in lay.keys.items() if k[0] == "f" and painter.blocked_floor(lay.flags.get(c, bytes(25)))
                      and c not in low}
    if presets.draws_trees(lay):
        play = painter.playable(lay, wet)
        found["ring"] = ({(x + dx, y + dy) for x, y in play for dx, dy in painter.STEPS} - play) & set(lay.keys)
    return {k: {c for c in v if any((c[0] + dx, c[1] + dy) in walk for dx, dy in painter.STEPS)}
            for k, v in found.items()}


def stands(lay: presets.Layout, run: list[Tile]) -> list[Tile]:
    """The walkable tiles 4-next to a run of edge tiles."""
    walk = presets.walkable(lay)
    return sorted({(x + dx, y + dy) for x, y in run for dx, dy in painter.STEPS if (x + dx, y + dy) in walk})


def floor_types(lay: presets.Layout) -> dict[str, list[Tile]]:
    """`floor:<DT1 stem>` -> the walkable tiles drawing it, those with only same-type neighbours first."""
    walk, by = presets.walkable(lay), {}
    for c in sorted(walk):
        by.setdefault((lay.keys[c][1][0][0] or "none").rsplit("/", 1)[-1].removesuffix(".dt1"), set()).add(c)
    out = {}
    for stem, cells in by.items():
        inner = [c for c in sorted(cells) if all((c[0] + dx, c[1] + dy) in cells for dx, dy in AROUND)]
        out[f"floor:{stem}"] = inner or sorted(cells)
    return out


def pick(groups: list[list[Tile]], n: int) -> list[Tile]:
    """n points over the groups (longest first): each group's centre, then each group's next-most-spread tile."""
    spreads, out = [spread(g, n) for g in groups], []
    for r in range(n):
        for s in spreads:
            if r < len(s) and s[r] not in out:
                out.append(s[r])
            if len(out) == n:
                return out
    return out


@dataclass(frozen=True)
class Point:
    map: str  # the lvlprest Name, e.g. "Torture Halls Map"
    ds1: str  # the DS1 stem
    level: int
    type: str  # an edge type or `floor:<stem>`
    tile: Tile
    command: str

    @property
    def name(self) -> str:
        slug = re.sub(r"[^a-z0-9]+", "-", f"{self.ds1}_{self.type}".lower()).strip("-")
        return f"{slug}_{self.tile[0]}-{self.tile[1]}"


def plan(lay: presets.Layout, map_: str, ds1: str, level: int, offset: tuple[int, int], types: list[str] | None = None,
         n: int = 4, wet: frozenset = frozenset()) -> list[Point]:
    """The warp points of one DS1: up to n per wanted type (default: every edge type and floor type the DS1 has)."""
    edges = edge_tiles(lay, wet)
    groups = {t: [stands(lay, run) for run in components(edges[t])] for t in EDGES if edges.get(t)}
    groups = {t: [g for g in gs if g] for t, gs in groups.items()}
    groups |= {t: [c] for t, c in floor_types(lay).items()}
    out = []
    for t, gs in groups.items():
        if gs and (not types or t in types or t.split(":")[0] in types):
            out += [Point(map_, ds1, level, t, c, goto(level, offset, c)) for c in pick([g for g in gs if g], n)]
    return out


# ---------------------------------------------------------------------------------------------------- the maps

@dataclass
class Map:
    name: str
    level: int
    ds1: str  # DS1 path as lvlprest lists it
    files: list[str]  # the DT1 files its preset loads


def maps(out_data: Path, d2r_data: Path) -> list[Map]:
    """The PD2-only presets with a generated HD preset, one per DS1."""
    lp = load(out_data, "lvlprest")
    level = {r[0]: int(r[col(lp, "LevelId")]) for r in lp.rows if r[0] and r[col(lp, "LevelId")].isdigit()}
    out, seen = [], set()
    for name, _, files, ds1s in presets.pd2_only_presets(out_data, len(load(d2r_data, "lvlprest").records())):
        for ds1 in ds1s:
            hd = out_data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if ds1 not in seen and hd.is_file() and level.get(name):
                seen.add(ds1)
                out.append(Map(name, level[name], ds1, files))
    return out


def select(found: list[Map], wanted: str) -> list[Map]:
    """The maps whose preset name or DS1 stem contains `wanted` (case-insensitive), or all for an empty one."""
    w = wanted.lower()
    return [m for m in found if w in m.name.lower() or w in Path(m.ds1).stem.lower()]


def plan_all(out_data: Path, d2r_data: Path, wanted: str, types: list[str] | None, n: int) -> list[Point]:
    lv = offsets(out_data)
    _, standins, water = presets.read_rule(ROOT / "merge" / "presets-hd.tsv")
    tiles, pd2_tiles = presets.Tiles(d2r_data / "global" / "tiles"), presets.Tiles(out_data / "global" / "tiles")
    out = []
    for m in select(maps(out_data, d2r_data), wanted):
        lay = presets.layout((out_data / "global" / "tiles" / m.ds1).read_bytes(),
                             presets.stand_in(m.files, tiles, pd2_tiles, standins))
        _, ox, oy = lv[m.level]
        out += plan(lay, m.name, Path(m.ds1).stem, m.level, (ox, oy), types, n, frozenset(water))
    return out


# ---------------------------------------------------------------------------------------------------- the game slot

def side_by_side(left, right):
    """One toolkit Image holding two of the same size, `left` on the left."""
    from game_ctl import Image
    if (left.width, left.height) != (right.width, right.height):
        raise ValueError("the two shots differ in size")
    w, stride = left.width, left.width * 3
    rows = [left.rgb[y * stride:(y + 1) * stride] + right.rgb[y * stride:(y + 1) * stride] for y in range(left.height)]
    return Image(2 * w, left.height, b"".join(rows))


def shoot(points: list[Point], slot: int, out: Path, settle: float = SETTLE, log=print) -> list[Path]:
    """Warp to each point on a locked slot whose game is in a game, and save the HD|legacy pair. Leaves the game in HD."""
    sys.path.insert(0, str(toolkit(ROOT) / "testing"))
    import game_ctl
    import testing_config as tc
    game = game_ctl.Game(tc.load(None), slot, os.environ.get("MODKIT_RUN"))
    if not game.is_running():
        raise SystemExit(f"slot {slot}: the game is not running (game_ctl.py --slot {slot} start, then enter a game)")
    for line in ("cheat nogethit 1", "cheat god 1"):
        game.console(line)
    saved = []
    for p in points:
        if not game.console(p.command):
            log(f"skip {p.name}: the console did not take {p.command!r}")
            continue
        time.sleep(settle)
        hd = game.screenshot()
        game.key("g")
        time.sleep(TOGGLE)
        legacy = game.screenshot()
        game.key("g")
        time.sleep(TOGGLE)
        if game_ctl.signature_distance(hd.signature(), legacy.signature()) == 0:
            log(f"warning {p.name}: HD and legacy shots are identical (is `g` toggling? is the game in HD?)")
        path = out / p.map.lower().replace(" ", "-") / f"{p.name}.png"
        saved.append(Path(side_by_side(hd, legacy).save(str(path))))
        log(f"{p.command}  ->  {path}")
    return saved


# ---------------------------------------------------------------------------------------------------- command line

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("list", "shoot"))
    ap.add_argument("--map", default="", help="a PD2-only preset name or DS1 stem, or part of one (default: all)")
    ap.add_argument("--type", action="append", default=[], help=f"edge type ({', '.join(EDGES)}), `floor` for every "
                    "floor type or `floor:<stem>` (repeatable; default: all)")
    ap.add_argument("--points", type=int, default=4, help="points per type and DS1 (default 4)")
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's generated/data)")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA, help="D2R data root")
    ap.add_argument("--out", type=Path, default=ROOT / "out" / "edge-tour", help="where `shoot` writes the pairs")
    ap.add_argument("--slot", type=int, help="shoot: the locked slot, its game running in a game")
    ap.add_argument("--settle", type=float, default=SETTLE, help="shoot: seconds to wait after each warp")
    a = ap.parse_args(argv)
    points = plan_all(a.data, a.d2r, a.map, a.type, a.points)
    if not points:
        print(f"no points for map {a.map!r}, type {a.type or 'any'}")
        return 1
    if a.cmd == "list":
        for p in points:
            print(f"{p.map}\t{p.ds1}\t{p.type}\t{p.tile[0]},{p.tile[1]}\t{p.command}")
        return 0
    if a.slot is None:
        ap.error("shoot needs --slot N")
    saved = shoot(points, a.slot, a.out, a.settle)
    print(f"{len(saved)} of {len(points)} pairs under {a.out}")
    return 0 if saved else 1


if __name__ == "__main__":
    sys.exit(main())
