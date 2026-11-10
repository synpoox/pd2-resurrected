"""No-game coverage report for the generated HD presets of PD2-only areas (LP3c, #672).

Run after pd2_merge.py. Each entity's model box (D2R's data/hd/model_lod_desc.bin, prefabs followed TWO levels, their
transforms composed) is placed on the DS1's tiles (pd2_presets.TILE HD units each), giving the tiles under a piece at
least TALL units high. Per generated preset:

- wall score: the share of DS1 wall tiles (what the automap draws as walls) under a tall piece;
- border score, for a DS1 that draws trees (pd2_presets.draws_trees): the ring of DS1 tiles 4-adjacent to and
  outside pd2_painter.playable() (the map's edge on the automap). A ring tile is covered (under a tall piece), a gap
  (bare), and a leak if the DS1 lets the player walk on it;
- floor score (#695): the share of walkable DS1 floor tiles (floor, no wall, not marked unwalkable, not mostly
  blocked) under a terrain's box (the flat ground planes and the pasted stock rooms' own terrain) and, on the flat
  planes, under no wrong-tile piece: a structure such as stairs or a wall (wrong_tiles). A bare tile draws no ground;
  a wrong one draws stairs or a wall where the DS1 has flat, walkable floor.

Writes <out>/report.md, worst first (border score, then wall score), and per preset <name>_walls.png (walls green
where covered, red where bare), <name>_floor.png (walkable floor green where covered, red where bare, orange where
wrong) and, for a
border, <name>_border.png (ring green, gap red, leak magenta). Known bias:
a turned piece's footprint is its axis-aligned box, which spills onto neighbour tiles, so both scores run a little
generous. TALL 4 reproduced the LP3 slice 1 game check (research/lp3-tile-table, FINDINGS.md). The report cannot judge
looks, frame rate or collision.

Size budget: each preset's JSON size and entity count, the presets above STOCK_BYTES or STOCK_ENTITIES (D2R's own
largest) listed in a "Size budget" section, largest first. Reported, not checked.

Changes against the baseline: `--baseline` (`preset <tab> wall % <tab> border % or - <tab> floor %`) holds main's
scores. The report and <out>/changes.md list each preset whose score moved, a new one, and one no longer scored.
`--check` fails (exit 1) when a preset's wall, border or floor score drops more than TOLERANCE points below the
baseline, or a baselined preset is no longer scored. In CI (#723) each push to main writes its scores
(`--write-baseline`) as the coverage-baseline artifact, and a PR is checked against the latest one, so PRs never
commit a baseline; the PR label coverage-drop-ok accepts an intended drop. BASELINE (docs/coverage-baseline.tsv) is
a snapshot for local runs and CI's fallback when main has no artifact; don't update it in a PR.
Needs model_lod_desc.bin in modkit's cache, else it skips:
`python -m modkit fetch --prefix data/hd/model_lod_desc.bin`.
"""
from __future__ import annotations

import argparse
import functools
import json
import math
import os
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

import pd2_painter as painter
import pd2_presets as presets
from pd2_ids import load, toolkit

ROOT = Path(__file__).resolve().parent.parent
D2R_DATA = Path(os.environ.get("MODKIT_CACHE", toolkit(ROOT) / "d2r-data")) / "93847" / "data"
MODELS = Path("hd") / "model_lod_desc.bin"
TALL = 4.0  # HD units: a piece this high hides a wall in game
ROOM = 400  # HD units: a box wider than this is a room-sized mesh (water, sculpted terrain), not a wall piece
STRUCTURE = 20.0  # HD units (2 tiles): a tall piece this wide both ways is a structure (stairs, platform), not a prop
DEPTH = 2  # prefab levels followed
EDGE = 1e-3  # tiles: how far a ground box may cross a tile edge and still end on it
BASELINE = ROOT / "docs" / "coverage-baseline.tsv"
TOLERANCE = 1.0  # percentage points a score may drop below the baseline
# D2R 93847's largest stock presets: act4/diab/dungrm6b.json, 6.1 MB; act2/town/lutw.json, 5642 entities
STOCK_BYTES = 6_100_000
STOCK_ENTITIES = 5642
# and the densest (presets of 100+ DS1 tiles): act2/temple1.json 5.0 entities, act4/catsdown.json 8.0 KB per tile
STOCK_DENSITY = 5.0
STOCK_BYTES_PER_TILE = 8000
IDENTITY = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}

Box = tuple[float, float, float, float, float, float]  # x0, x1, z0, z1, top, bottom


def fnv(path: str) -> int:
    """model_lod_desc.bin's key for a model path: 32-bit FNV-1a of the lower-cased path."""
    h = 0x811C9DC5
    for b in path.lower().encode():
        h = ((h ^ b) * 0x1000193) & 0xFFFFFFFF
    return h


def model_boxes(raw: bytes) -> dict[int, tuple[float, ...]]:
    """model_lod_desc.bin: `u32 count | i32 offset`, then from 4 + offset, 44-byte records
    `u32 key | f32 bbMin[3] | f32 bbMax[3] | ...`. key -> (min x, y, z, max x, y, z)."""
    count, off = struct.unpack_from("<Ii", raw, 0)
    return {r[0]: r[1:] for r in (struct.unpack_from("<I6f", raw, 4 + off + 44 * i) for i in range(count))}


def qmul(a: dict, b: dict) -> dict:
    """The quaternion a * b (b turned, then a)."""
    ax, ay, az, aw = a["x"], a["y"], a["z"], a["w"]
    bx, by, bz, bw = b["x"], b["y"], b["z"], b["w"]
    return {"w": aw * bw - ax * bx - ay * by - az * bz, "x": aw * bx + ax * bw + ay * bz - az * by,
            "y": aw * by - ax * bz + ay * bw + az * bx, "z": aw * bz + ax * by - ay * bx + az * bw}


def boxes(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
          base: tuple | None = None, depth: int = 0) -> Iterator[Box]:
    """The world boxes of the entities' models; a prefab's entities are placed by the prefab's transform."""
    for e in entities:
        t = presets._transform(e)
        if t is None or e.get("name") == "terrain":
            continue
        p = (t["position"]["x"], t["position"]["y"], t["position"]["z"])
        q, sc = t.get("orientation") or IDENTITY, t.get("scale") or {"x": 1, "y": 1, "z": 1}
        s = (sc["x"], sc["y"], sc["z"])
        if base:
            bp, bq, bs = base
            v = painter.rotate(bq, (p[0] * bs[0], p[1] * bs[1], p[2] * bs[2]))
            p, q, s = (bp[0] + v[0], bp[1] + v[1], bp[2] + v[2]), qmul(bq, q), (s[0] * bs[0], s[1] * bs[1], s[2] * bs[2])
        for c in e["components"]:
            if c["type"] == "ModelDefinitionComponent" and (bb := box(c.get("filename", ""))):
                corners = [painter.rotate(q, (x * s[0], y * s[1], z * s[2]))
                           for x in (bb[0], bb[3]) for y in (bb[1], bb[4]) for z in (bb[2], bb[5])]
                xs, ys, zs = ([p[i] + v[i] for v in corners] for i in range(3))
                yield min(xs), max(xs), min(zs), max(zs), max(ys), min(ys)
            elif c["type"] == "PrefabPlacementDefinitionComponent" and c.get("prefab") and depth < DEPTH:
                yield from boxes(prefab(c["prefab"]), box, prefab, (p, q, s), depth + 1)


def tall_tiles(found: Iterator[Box], tall: float = TALL) -> set[tuple[int, int]]:
    """The tiles under the footprint of a box at least `tall` high (room-sized boxes left out)."""
    return footprint(b for b in found if b[4] >= tall and b[1] - b[0] <= ROOM and b[3] - b[2] <= ROOM)


def footprint(found: Iterator[Box], exclusive: bool = False) -> set[tuple[int, int]]:
    """The tiles under the boxes. `exclusive`: a box ending on a tile edge does not reach the tile past it (a
    170-unit ground plane covers 17 tiles, not 18); the wall score keeps the tile it ends on (tall_tiles)."""
    out = set()
    T = presets.TILE

    def span(a: float, b: float) -> range:
        if not exclusive:
            return range(math.floor(a / T), math.floor(b / T) + 1)
        lo = math.floor(a / T + EDGE)  # the stock plane's box spills past its edges by float noise (170.00003)
        return range(lo, max(math.ceil(b / T - EDGE), lo + 1))
    for x0, x1, z0, z1, *_ in found:
        out.update((tx, tz) for tx in span(x0, x1) for tz in span(z0, z1))
    return out


def ground_tiles(preset: dict, box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
                 flat: bool | None = None) -> set:
    """The tiles under a terrain's box: the preset's terrain, its flat planes and its pasted rooms' terrain; with
    `flat`, only the flat planes' (True) or only the rooms' (False)."""
    def is_flat(e: dict) -> bool:
        return any(f"/{presets.TERRAIN}/" in c.get("filename", "").lower() for c in e["components"])
    terrains = [e | {"name": ""} for e in preset["entities"] + [preset.get("terrain") or {}]
                if e.get("name") == "terrain" and "components" in e  # boxes() skips entities named terrain
                and (flat is None or is_flat(e) == flat)]
    return footprint(boxes(terrains, box, prefab), exclusive=True)


def wrong_tiles(entities: list[dict], box: Callable[[str], tuple | None], prefab: Callable[[str], list[dict]],
                at: set) -> set:
    """The tiles under the structures (STRUCTURE wide both ways, TALL high over the flat ground or its own base,
    whichever is higher: stairs, platforms, walls, gates; #679) anchored on `at`, walkable floor on the flat ground. A
    flat decal raised to its stock ground's height is not one, nor a ground mesh sunk below the plane. Pieces floating over the flat ground are left out: hanging
    beams and candles float in stock too (#682 stands kit pieces at their stock height)."""
    out = set()
    for e in entities:
        t = presets._transform(e)
        if t is None or e.get("name") == "terrain":
            continue
        if (int(t["position"]["x"] // presets.TILE), int(t["position"]["z"] // presets.TILE)) not in at:
            continue
        out |= footprint((b for b in boxes([e], box, prefab) if STRUCTURE <= b[1] - b[0] <= ROOM
                          and STRUCTURE <= b[3] - b[2] <= ROOM and b[4] - max(b[5], 0) >= TALL), exclusive=True)
    return out


def walkable_floor(lay: presets.Layout) -> set[tuple[int, int]]:
    """The DS1's walkable floor: floor tiles (no wall), not marked unwalkable, not mostly blocked (water)."""
    return {c for c, k in lay.keys.items() if k[0] == "f" and c not in lay.unwalkable
            and not painter.blocked_floor(lay.flags.get(c, bytes(25)))}


@dataclass
class Score:
    name: str
    area: str  # the lvlprest row (Name), e.g. Ashen Plains Map
    walls: int  # DS1 wall tiles
    bare: set  # wall tiles under no tall piece
    ring: set | None = None  # border ring tiles, None for a DS1 without trees
    gaps: set = frozenset()
    leaks: set = frozenset()
    size: int = 0  # preset JSON bytes
    entities: int = 0  # top-level entities
    tiles: int = 0  # DS1 tiles with content
    floors: int = 0  # walkable DS1 floor tiles
    floor_bare: set = frozenset()  # walkable floor under no terrain
    floor_wrong: set = frozenset()  # walkable floor under a piece that does not belong on flat floor (wrong_tiles)

    @property
    def over_budget(self) -> bool:
        return self.size > STOCK_BYTES or self.entities > STOCK_ENTITIES

    @property
    def dense(self) -> bool:
        """Denser than any stock preset: a per-tile cost no D2R room carries."""
        return bool(self.tiles) and (self.entities / self.tiles > STOCK_DENSITY
                                     or self.size / self.tiles > STOCK_BYTES_PER_TILE)

    @property
    def wall_score(self) -> float:
        return 1 - len(self.bare) / self.walls if self.walls else 1.0

    @property
    def floor_score(self) -> float:
        return 1 - len(self.floor_bare | self.floor_wrong) / self.floors if self.floors else 1.0

    @property
    def border_score(self) -> float | None:
        return None if self.ring is None else 1 - len(self.gaps) / len(self.ring) if self.ring else 1.0


def score(lay: presets.Layout, tall: set, name: str = "", area: str = "", wet: frozenset = frozenset(),
          ground: set | None = None, wrong: set = frozenset()) -> Score:
    """A layout's scores from its tall tiles, `ground` (ground_tiles; None: every tile has ground) and `wrong`
    (wrong_tiles)."""
    walls = {c for c, k in lay.keys.items() if k[0] == "w"}
    floor = walkable_floor(lay)
    s = Score(name, area, len(walls), walls - tall, floors=len(floor),
              floor_bare=set() if ground is None else floor - ground, floor_wrong=(floor & wrong))
    if presets.draws_trees(lay):
        play = painter.playable(lay, wet)
        ring = ({(x + dx, y + dy) for x, y in play for dx, dy in painter.STEPS} - play) & set(lay.keys)
        s.ring, s.gaps = ring, ring - tall
        s.leaks = {c for c in ring if c not in lay.unwalkable and not any(b & 1 for b in lay.flags.get(c, bytes(25)))}
    return s


def png(w: int, h: int, px: dict[tuple[int, int], tuple[int, int, int]], back=(20, 20, 20), zoom: int = 3) -> bytes:
    """An RGB PNG of w x h cells, `zoom` pixels each (no Pillow: the unit job has none)."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + b"".join(bytes(px.get((x // zoom, y // zoom), back)) for x in range(w * zoom))
                    for y in range(h * zoom))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", w * zoom, h * zoom, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def images(lay: presets.Layout, s: Score, tall: set, wet: frozenset = frozenset()) -> dict[str, bytes]:
    """<name>_walls.png and, with a border, <name>_border.png."""
    base = {c: (80, 75, 60) for c in lay.keys} | {c: (40, 90, 160) for c in tall if c in lay.keys}
    walls = {c: (230, 40, 40) if c in s.bare else (60, 210, 60) for c, k in lay.keys.items() if k[0] == "w"}
    out = {f"{s.name}_walls.png": png(lay.width, lay.height, base | walls)}
    floor = {c: (230, 40, 40) if c in s.floor_bare else (240, 150, 30) if c in s.floor_wrong else (60, 210, 60)
             for c in walkable_floor(lay)}
    out[f"{s.name}_floor.png"] = png(lay.width, lay.height, {c: (60, 55, 45) for c in lay.keys} | floor)
    if s.ring is not None:
        px = {c: (45, 40, 32) for c in lay.keys} | {c: (120, 112, 95) for c in painter.playable(lay, wet)}
        px |= {c: (230, 40, 200) if c in s.leaks else (230, 40, 40) if c in s.gaps else (50, 200, 60) for c in s.ring}
        out[f"{s.name}_border.png"] = png(lay.width, lay.height, px, zoom=4)
    return out


def _key(s: Score) -> tuple:
    return (s.border_score is None, s.border_score or 0.0, s.wall_score, s.name)


def pct1(v: float | None) -> float | None:
    """A score as the baseline keeps it: a percentage to one decimal."""
    return None if v is None else round(100 * v, 1)


def baseline_text(scores: list[Score]) -> str:
    lines = ["# HD preset coverage on main (tools/pd2_coverage.py --write-baseline): preset, wall %, border % or -, "
             "floor %"]
    for s in sorted(scores, key=lambda s: s.name):
        b = pct1(s.border_score)
        lines.append(f"{s.name}\t{pct1(s.wall_score)}\t{'-' if b is None else b}\t{pct1(s.floor_score)}")
    return "\n".join(lines) + "\n"


def read_baseline(text: str) -> dict[str, tuple[float, float | None, float | None]]:
    """name -> (wall, border or None, floor or None: a line from before the floor score)."""
    out = {}
    for line in text.splitlines():
        if line and not line.startswith("#"):
            name, wall, border, *floor = line.split("\t")
            out[name] = (float(wall), None if border == "-" else float(border), float(floor[0]) if floor else None)
    return out


@dataclass
class Change:
    name: str
    wall: tuple[float | None, float | None]  # (baseline, now); None: absent
    border: tuple[float | None, float | None]
    floor: tuple[float | None, float | None]
    drop: bool  # beyond TOLERANCE, or no longer scored


def changes(scores: list[Score], base: dict, tolerance: float = TOLERANCE) -> list[Change]:
    """The presets whose scores differ from the baseline, worst drop first."""
    now = {s.name: (pct1(s.wall_score), pct1(s.border_score), pct1(s.floor_score)) for s in scores}
    out = []
    for name in sorted(set(now) | set(base)):
        (w0, b0, f0), (w1, b1, f1) = base.get(name, (None,) * 3), now.get(name, (None,) * 3)
        if f0 is None and name in base:  # a baseline line from before the floor score: no floor change
            f1 = None
        if name in base and name in now and (w0, b0, f0) == (w1, b1, f1):
            continue
        gone = name not in now
        drop = gone or b0 is not None and b1 is None or any(  # a border no longer scored drops too
            old is not None and new is not None and old - new > tolerance
            for old, new in ((w0, w1), (b0, b1), (f0, f1)))
        out.append(Change(name, (w0, w1), (b0, b1), (f0, f1), drop))
    def worst(c: Change) -> float:
        return min((n - o for o, n in (c.wall, c.border, c.floor) if o is not None and n is not None), default=0.0)
    return sorted(out, key=lambda c: (not c.drop, worst(c), c.name))


def changes_md(found: list[Change], tolerance: float = TOLERANCE) -> str:
    def cell(old, new) -> str:
        if old is None and new is None:
            return "-"
        if old is None:
            return f"new {new}%"
        if new is None:
            return f"{old}% -> gone"
        return f"{old}% -> {new}% ({new - old:+.1f})" if old != new else f"{new}%"
    drops = sum(c.drop for c in found)
    lines = ["## Changes against the baseline", ""]
    if not found:
        return "\n".join(lines + ["No preset's coverage changed.", ""])
    lines += [f"{len(found)} presets changed; {drops} dropped by more than the {tolerance:g}-point tolerance or are "
              "no longer scored"
              + (" (accept an intended drop: label the PR `coverage-drop-ok`)." if drops else "."),
              "", "| Preset | Wall | Border | Floor | |", "|---|---|---|---|---|"]
    lines += [f"| {c.name} | {cell(*c.wall)} | {cell(*c.border)} | {cell(*c.floor)} | {'**drop**' if c.drop else ''} |"
              for c in found]
    return "\n".join(lines) + "\n"


def report(scores: list[Score], commit: str = "", found: list[Change] | None = None) -> str:
    """The markdown report, worst first: presets with a border by border score, then the rest by wall score."""
    pct = lambda v: "-" if v is None else f"{100 * v:.1f}%"  # noqa: E731
    lines = ["# HD preset coverage" + (f" ({commit})" if commit else ""), "",
             f"{len(scores)} generated presets. Wall: DS1 wall tiles under a piece >= {TALL:g} units. Border (DS1s "
             "with trees): the ring outside the playable area; gaps bare, leaks walkable in the DS1. Floor: walkable "
             "DS1 floor tiles under a terrain and no wrong-tile piece (stairs or walls on flat floor). Images: red "
             "where bare, orange where wrong.", "",
             "| Preset | Area | Border | Gaps | Leaks | Wall | Walls | Floor | Bare floor | Wrong floor | MB "
             "| Entities | Images |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    if found is not None:
        lines[2:2] = [changes_md(found), ""]
    for s in sorted(scores, key=_key):
        img = (f"[walls]({s.name}_walls.png) [floor]({s.name}_floor.png)"
               + (f" [border]({s.name}_border.png)" if s.ring is not None else ""))
        border = s.ring is not None
        lines.append(f"| {s.name} | {s.area} | {pct(s.border_score)} | {len(s.gaps) if border else '-'} | "
                     f"{len(s.leaks) if border else '-'} | "
                     f"{pct(s.wall_score)} | {s.walls} | {pct(s.floor_score)} | {len(s.floor_bare)} | "
                     f"{len(s.floor_wrong)} | "
                     f"{s.size / 1e6:.1f} | {s.entities} | {img} |")
    if scores:
        n = sum(s.walls for s in scores)
        lines += ["", f"All walls: {pct(1 - sum(len(s.bare) for s in scores) / n if n else 1.0)} of {n}."]
        if f := sum(s.floors for s in scores):
            lines.append(f"All floors: {pct(1 - sum(len(s.floor_bare | s.floor_wrong) for s in scores) / f)} of {f} "
                         f"walkable floor tiles ({sum(len(s.floor_wrong) for s in scores)} wrong).")
        rings = [s for s in scores if s.ring is not None]
        if r := sum(len(s.ring) for s in rings):
            lines.append(f"All borders: {pct(1 - sum(len(s.gaps) for s in rings) / r)} of {r} ring tiles.")
    return "\n".join(lines) + "\n" + budget_md(scores)


def budget_md(scores: list[Score]) -> str:
    """The presets above stock's largest in size or entities, largest first."""
    over = sorted((s for s in scores if s.over_budget), key=lambda s: (-s.size, s.name))
    lines = ["", "## Size budget", "",
             f"{len(over)} of {len(scores)} presets are above D2R's largest stock preset ({STOCK_BYTES / 1e6:.1f} MB "
             f"or {STOCK_ENTITIES} entities); a PD2 map is one DS1 where a D2R area is many rooms, so size alone is "
             f"expected. **dense**: more per DS1 tile than any stock preset ({STOCK_DENSITY:g} entities or "
             f"{STOCK_BYTES_PER_TILE / 1000:g} KB); {sum(s.dense for s in scores)} presets."]
    if over:
        lines += ["", "| Preset | Area | MB | x stock | Entities | x stock | Entities/tile | KB/tile | |",
                  "|---|---|---|---|---|---|---|---|---|"]
        lines += [f"| {s.name} | {s.area} | {s.size / 1e6:.1f} | {s.size / STOCK_BYTES:.1f} | {s.entities} | "
                  f"{s.entities / STOCK_ENTITIES:.1f} | {s.entities / max(s.tiles, 1):.1f} | "
                  f"{s.size / max(s.tiles, 1) / 1000:.1f} | {'**dense**' if s.dense else ''} |" for s in over]
    return "\n".join(lines) + "\n"


def run(data: Path, d2r: Path, out: Path, commit: str = "", base: dict | None = None) -> list[Score] | None:
    """Score every generated preset under data (generated/data) and write the report to out, with the changes
    against `base` (read_baseline) when given. None: skipped."""
    if not (d2r / MODELS).is_file():
        print(f"pd2_coverage: skipped, no {d2r / MODELS} (`python -m modkit fetch --prefix data/hd/model_lod_desc.bin`)")
        return None
    rule = ROOT / "merge" / "presets-hd.tsv"
    _, standins, water = presets.read_rule(rule)
    wet = frozenset(water)  # stream DT1, water past the tree line too (painter.water)
    found = model_boxes((d2r / MODELS).read_bytes())

    @functools.cache
    def prefab(path: str) -> list[dict]:
        f = d2r / path.removeprefix("data/")
        return json.loads(f.read_text("utf-8-sig")).get("entities", []) if f.is_file() else []

    def box(path: str):
        return found.get(fnv(path))
    tiles, pd2_tiles = presets.Tiles(d2r / "global" / "tiles"), presets.Tiles(data / "global" / "tiles")
    out.mkdir(parents=True, exist_ok=True)
    scores, seen = [], set()
    for area, _, files, ds1s in presets.pd2_only_presets(data, len(load(d2r, "lvlprest").records())):
        for ds1 in ds1s:
            f = data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if not f.is_file() or ds1 in seen:  # waiting on stand-ins (LP4/LP5), or a DS1 two presets load
                continue
            seen.add(ds1)
            lay = presets.layout((data / "global" / "tiles" / ds1).read_bytes(),
                                 presets.stand_in(files, tiles, pd2_tiles, standins))
            preset = json.loads(f.read_text("utf-8"))
            entities = preset["entities"]
            tall = tall_tiles(boxes(entities, box, prefab))
            flat = ground_tiles(preset, box, prefab, True) - ground_tiles(preset, box, prefab, False)
            wrong = wrong_tiles(entities, box, prefab, walkable_floor(lay) & flat)
            s = score(lay, tall, Path(ds1).stem, area, wet, ground_tiles(preset, box, prefab), wrong)
            s.size, s.entities, s.tiles = f.stat().st_size, len(entities), len(lay.keys)
            scores.append(s)
            for name, image in images(lay, s, tall, wet).items():
                (out / name).write_bytes(image)
    found = None if base is None else changes(scores, base)
    if found is not None:
        (out / "changes.md").write_text(changes_md(found), "utf-8")
    (out / "report.md").write_text(report(scores, commit, found), "utf-8")
    print(f"pd2_coverage: {len(scores)} presets scored, {out / 'report.md'}")
    return scores


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's output)")
    ap.add_argument("--d2r", type=Path, default=D2R_DATA, help=f"D2R data root (default: {D2R_DATA})")
    ap.add_argument("--out", type=Path, default=ROOT / "out" / "coverage", help="report folder")
    ap.add_argument("--commit", default="", help="commit named in the report's title")
    ap.add_argument("--baseline", type=Path, default=BASELINE, help=f"main's scores (default: {BASELINE})")
    ap.add_argument("--check", action="store_true", help=f"exit 1 on a drop of more than {TOLERANCE:g} points")
    ap.add_argument("--write-baseline", action="store_true", help="write the scores to --baseline")
    a = ap.parse_args(argv)
    base = read_baseline(a.baseline.read_text("utf-8")) if a.baseline.is_file() else None
    scores = run(a.data, a.d2r, a.out, a.commit, base)
    if scores is None:
        return 0
    if a.write_baseline:
        a.baseline.write_text(baseline_text(scores), "utf-8", newline="\n")
        print(f"pd2_coverage: baseline written, {a.baseline}")
        return 0
    if base is None:
        print(f"pd2_coverage: no baseline at {a.baseline}" + ("" if not a.check else "; nothing to check"))
        return 1 if a.check else 0
    found = changes(scores, base)
    drops = [c.name for c in found if c.drop]
    print(f"pd2_coverage: {len(found)} presets changed against the baseline, {len(drops)} dropped"
          + (f": {', '.join(drops)}" if drops else ""))
    return 1 if a.check and drops else 0


if __name__ == "__main__":
    sys.exit(main())
