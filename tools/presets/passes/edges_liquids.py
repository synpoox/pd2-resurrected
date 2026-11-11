"""The liquid edge pass (#738): a legacy water or lava edge (a liquid tile with a walkable neighbour) with no D2R water
edge piece over it gets a whole, low piece from D2R's own edge families, laid along the shore, so the shore reads as
an edge and not as a hole in the pieces.

- Which tiles: the legacy edge the scorer reads (pd2_tiles.legacy_edges: a liquid floor tile with a neighbour inside the
  layout that is no liquid), in the per-tile kit's tiles (`b.rest`, never a pasted room). A tile is left alone when a piece the scorer types "water"
  (pd2_tiles.model_type: a model named water, river, lava edge, ...) already stands over it, so the kit's own picks and
  every pass before this one keep their tiles, and the pieces this pass puts down space themselves (each covers the
  tiles under its box).
- Lava (act4/lava floor DT1s): D2R's lava edge rims (act4_lava_edge_straight, act4_lava_edges_corners), 1.5 units
  high, 2.5 to 4.5 tiles long. A straight rim is turned so its long side runs along the shore; a tile open on two sides
  at a right angle takes a curve.
- Act 1 rivers and ponds (act1/outdoors river and pond DT1s: Descend's river): D2R's river rocks (act1_outdoors_rocks),
  1.5 units high, one per tile, turned by tile.
- Any other liquid DT1 (canals, oases, Act 3 river banks) is left to the kit: no D2R piece stands for it here.
- The pieces are in liquid_kit.json: model, box and dependencies from the stock palettes (prop_palette/act4/lava,
  act1/outdoors), without the physics body, so a rim hides no walkable ground and blocks nothing. They stand at y 0;
  floor_height (900) lifts them with the ground, and the water plane (streams, 700) lies under them.
- Needs the D2R model boxes (the wall pass's context, set by the merge); without them (dev builds) it places nothing.
"""
from __future__ import annotations

import functools
import json
import math
import re
from pathlib import Path

import pd2_painter as painter

from ..consts import TILE
from ..entity import _transform, clone
from . import edges_walls

ORDER = 460
KIT = Path(__file__).resolve().parent.parent / "liquid_kit.json"
STRAIGHT = ("lava_small01", "lava_small02", "lava_small03", "lava_small04")
CURVES = ("lava_curve01", "lava_curve02", "lava_curve03")
ROCKS = ("rock_river01", "rock_river02", "rock_river03")
LAVA = re.compile(r"(^|/)lava/")
RIVER = re.compile(r"act1/outdoors/(river|pond)\.dt1$")
SHIFT = 0.25 * TILE  # a piece sits this far from the liquid tile's centre, toward the land


@functools.cache
def kit() -> dict:
    return json.loads(KIT.read_text("utf-8"))


def yaw(angle: float) -> dict:
    return {"x": 0.0, "y": math.sin(angle / 2), "z": 0.0, "w": math.cos(angle / 2)}


def choose(c: tuple[int, int], dt1s: tuple, open_: list) -> tuple[str, float] | None:
    """The kit piece and the turn (radians) for the liquid tile c, or None for a liquid the kit has no piece for."""
    pick = (c[0] * 3 + c[1] * 5) % 12
    if any(LAVA.search(d) for d in dt1s):
        xs, zs = sum(1 for dx, dz in open_ if dz == 0), sum(1 for dx, dz in open_ if dx == 0)
        if xs and zs:  # open on two sides at a right angle: a corner
            return CURVES[pick % len(CURVES)], pick * math.pi / 6
        name = STRAIGHT[pick % len(STRAIGHT)]
        x0, _, z0, x1, _, z1 = kit()[name]["box"]
        long_z = (z1 - z0) >= (x1 - x0)
        along_x = not xs  # open only north or south: the shore runs along x
        return name, math.pi / 2 if long_z == along_x else 0.0
    if any(RIVER.search(d) for d in dt1s):
        return ROCKS[pick % len(ROCKS)], pick * math.pi / 6
    return None


def piece(name: str, c: tuple[int, int], angle: float, toward: tuple[float, float]) -> dict:
    """The kit entity `name` centred on tile c, turned by `angle`, moved SHIFT toward `toward` (a unit direction)."""
    k = kit()[name]
    e = clone(k["entity"])
    q = yaw(angle)
    x0, _, z0, x1, _, z1 = k["box"]
    cx, _, cz = painter.rotate(q, ((x0 + x1) / 2, 0.0, (z0 + z1) / 2))
    t = _transform(e)
    t["orientation"] = q
    t["position"] = {"x": c[0] * TILE + TILE / 2 + toward[0] * SHIFT - cx, "y": 0.0,
                     "z": c[1] * TILE + TILE / 2 + toward[1] * SHIFT - cz}
    return e


def run(b) -> None:
    ctx = edges_walls.CONTEXT.get(b.seed)
    if ctx is None:  # no D2R model boxes: dev builds
        return
    import pd2_tiles  # it imports pd2_presets, which imports this package: not at the top
    legacy = ctx.legacy  # the DS1 named by its own DT1s: the scorer reads the legacy edge off it
    water = {c for c, (kind, layers) in legacy.keys.items() if kind == "f" and any(
        pd2_tiles.liquid_floor(t, legacy.flags.get(c, bytes(25)), ctx.heights) for t in layers)}
    jobs = {}
    for c in water:
        if c not in b.rest or c in b.taken:
            continue
        open_ = [(dx, dz) for dx, dz in painter.STEPS
                 if (c[0] + dx, c[1] + dz) in legacy.keys and (c[0] + dx, c[1] + dz) not in water]
        if open_ and (pick := choose(c, tuple(t[0] for t in legacy.keys[c][1]), open_)):
            jobs[c] = pick, open_
    if not jobs:
        return

    def covered(entities: list) -> set:
        return pd2_tiles.footprint(x for x in pd2_tiles.boxes(entities, ctx.box, ctx.prefab, meta=True)
                                   if pd2_tiles.model_type(x[6]) == "water")
    have = covered(b.entities + b.loose)
    for c in sorted(jobs):
        if c in have:
            continue
        (name, angle), open_ = jobs[c]
        n = math.hypot(sum(dx for dx, _ in open_), sum(dz for _, dz in open_)) or 1.0
        e = piece(name, c, angle, (sum(dx for dx, _ in open_) / n, sum(dz for _, dz in open_) / n))
        b.loose.append(e)
        b.depend(kit()[name]["dependencies"])
        have |= covered([e])
