"""The water and lava edges (#738, part of #707): bank, rock and lava-edge pieces laid along the boundary between
walkable ground and liquid, so a river, pond, pool or lava bed has a shore as legacy draws it.

Legacy marks a shore two ways (pd2_fidelity.legacy_edges, the shared classifier's "water" type): a liquid floor tile with a
4-neighbour that is not liquid, and a wall-layer tile of a river/pond/rivbank/pool DT1 or lava's ext/intwalls and rocks.
Both are traced here from the liquid mask (presets/liquids.py, #741). Each target tile no water-type piece already stands
on gets, in order, a D2R riverbank (medium, small) or lava-kit piece turned so its bank faces the liquid, stepped toward the
liquid until its box clears the walkable floor, else (water) a rock_river rock on the tile. A piece's box then covers its
neighbours' targets too, which spaces the pieces by their length as D2R's rooms do (2.5-3 tiles for a bank).

Nothing floats or covers walkable ground: a piece rests at the liquid's height (liquids.liquid_y, as stock banks sit 0.5
under the water plane) and a candidate whose box reaches a walkable tile is rejected. The pieces are D2R's: apply() gives
the module the stock ones through stock(), found like the water planes (presets/water.py)."""
from __future__ import annotations

import copy
import json
import math
import zlib
from pathlib import Path

import pd2_coverage as coverage
import pd2_painter as painter

from .. import liquids
from ..consts import PRESETS, STREAM, TILE, WATER_Y
from ..entity import _transform
from ..layout import walkable

ORDER = 750  # after the liquid planes (700), before the ground (800)

MODELS = "data/hd/env/model/"
BANKS = [f"{MODELS}act3/jungle/act3_jungle_riverbanks/riverbank_{size}0{n}.model"
         for size in ("medium", "small") for n in (1, 2, 3)]
ROCKS = [f"{MODELS}global/prop/act1/outdoors/act1_outdoors_rocks_wet/rock_river0{n}_wet.model" for n in (1, 2, 3, 4, 5)]
LAVA = [f"{MODELS}global/prop/act4/lava/act4_lava_lava_kits/lava0{n}_{h}.model" for h in ("med", "low") for n in (1, 2, 3, 4)]
WANTED = BANKS + ROCKS + LAVA
NAME = "pd2r_liquid_edge"
REACH = 3  # tiles: how far from a wall-layer shore tile the liquid is looked for
PUSH = (0.0, 4.0, 8.0, 12.0, 16.0, 20.0)  # HD units: how far toward the liquid a piece is stepped to clear the walkable floor
NEIGHBOURS = tuple((dx, dy) for dx in range(-REACH, REACH + 1) for dy in range(-REACH, REACH + 1) if dx or dy)

# the stock pieces, set by apply() through stock(): {"pieces": model -> (entity, dependencies), "box": model -> box}
STOCK: dict = {}


def stock(d2r_data: Path) -> None:
    """Find the stock pieces: each model's first entity in the smallest D2R preset that draws it, with that preset's
    dependencies, and the model boxes. Without them (dev builds without D2R's presets) the pass places nothing."""
    boxes = d2r_data / coverage.MODELS
    root = d2r_data / PRESETS
    if not boxes.is_file() or not root.is_dir():
        return
    found: dict[str, tuple[dict, dict]] = {}
    for p in sorted(root.rglob("*.json"), key=lambda p: (p.stat().st_size, p)):
        if len(found) == len(WANTED):
            break
        text = p.read_text("utf-8-sig")
        for model in (m for m in WANTED if m not in found and m in text):
            preset = json.loads(text)
            e = next((e for e in preset["entities"]
                      if any(c.get("filename") == model for c in e["components"])), None)
            if e:
                found[model] = (e, preset["dependencies"])
    raw = coverage.model_boxes(boxes.read_bytes())
    STOCK.clear()
    STOCK.update(pieces=found, box=lambda path: raw.get(coverage.fnv(path)))


def direction(c: tuple[int, int], liquid: dict, inside: bool, lay) -> tuple[float, float]:
    """The unit vector (x, z) from tile c toward the liquid: for a liquid tile, away from the land around it; for a
    shore tile on land, toward the liquid around it."""
    vx = vz = 0.0
    for dx, dy in NEIGHBOURS:
        n = (c[0] + dx, c[1] + dy)
        if n not in lay.keys or (n in liquid) != (not inside):
            continue
        w = 1.0 / (dx * dx + dy * dy)
        vx, vz = vx + dx * w, vz + dy * w
    if not vx and not vz:
        return 0.0, 1.0
    s = math.hypot(vx, vz)
    return (-vx / s, -vz / s) if inside else (vx / s, vz / s)


def targets(b, liquid: dict) -> dict[tuple[int, int], str]:
    """The legacy shore tiles to lay pieces on -> the liquid ("water", "lava", "swamp") they border."""
    import pd2_fidelity as fidelity  # imports the presets package: not at the top
    lay, out = b.lay, {}
    for c, dt1s in liquid.items():
        if c not in b.taken and any((c[0] + dx, c[1] + dy) in lay.keys and (c[0] + dx, c[1] + dy) not in liquid
                                    for dx, dy in painter.STEPS):
            out[c] = liquids.kind(dt1s[0])
    for c, (kind, tiles) in lay.keys.items():
        if kind != "w" or c in b.taken or not any(fidelity.tile_type(t) == "water" for t in tiles):
            continue
        near = [(dx * dx + dy * dy, liquid[(c[0] + dx, c[1] + dy)][0]) for dx, dy in NEIGHBOURS
                if (c[0] + dx, c[1] + dy) in liquid]
        if near:  # a wall layer with no liquid around it borders none: it is left to the kit
            out[c] = liquids.kind(min(near)[1])
    return out


def piece(model: str, c: tuple[int, int], push: float, n: tuple[float, float]) -> tuple[dict, dict]:
    """A copy of the stock piece of `model` for shore tile c, its origin `push` units toward the liquid along n, turned so
    its local +z (the bank's liquid side, measured on D2R's own banks) faces it, resting as the stock one rests on its
    water: at the liquid's height."""
    template, deps = STOCK["pieces"][model]
    e = copy.deepcopy(template)
    t = _transform(e)
    yaw = math.atan2(n[0], n[1])
    t["position"] = {"x": (c[0] + 0.5) * TILE + n[0] * push,
                     "y": t["position"]["y"] + liquids.liquid_y(c) - WATER_Y,
                     "z": (c[1] + 0.5) * TILE + n[1] * push}
    t["orientation"] = {"x": 0.0, "y": math.sin(yaw / 2), "z": 0.0, "w": math.cos(yaw / 2)}
    t["scale"] = {"x": 1.0, "y": 1.0, "z": 1.0}
    e["name"] = NAME
    return e, deps


def run(b) -> None:
    if not STOCK.get("pieces"):
        return
    import pd2_fidelity as fidelity  # imports the presets package: not at the top
    liquid = liquids.of(b)
    wanted = targets(b, liquid)
    if not wanted:
        return
    box = STOCK["box"]
    ground = walkable(b.lay)
    # the tiles water-type pieces already stand on (the liquid planes do not count: a shore is a piece along them)
    done = set(fidelity.hd_pieces([e for e in b.entities if e.get("name") != STREAM], box, lambda p: []).tops["water"])
    state = b.mine(__name__)
    placed = state.setdefault("placed", {})
    missed = state.setdefault("missed", [])
    for c in sorted(wanted, key=lambda c: (c[1], c[0])):
        if c in done:
            continue
        kind = wanted[c]
        inside = c in liquid
        n = direction(c, liquid, inside, b.lay)
        # the pieces of the liquid, in the order they are tried; the variant a tile starts at is its seed's
        models = LAVA if kind == "lava" else BANKS + ROCKS
        start = zlib.crc32(f"{b.seed}{c}".encode())
        order = (models[start % 3:3] + models[:start % 3] + models[3 + start % 3:6] + models[3:3 + start % 3] + models[6:]
                 if kind != "lava" else models[start % 4:4] + models[:start % 4] + models[4 + start % 4:] + models[4:4 + start % 4])
        choice = None
        for model in order:
            if model not in STOCK["pieces"]:
                continue
            for push in (PUSH if model not in ROCKS else (0.0,)):
                e, deps = piece(model, c, push, n)
                foot = coverage.footprint(coverage.boxes([e], box, lambda p: []))
                if c in foot and not foot & ground:
                    choice = e, deps, foot, model
                    break
            if choice:
                break
        if not choice:
            missed.append(c)
            continue
        e, deps, foot, model = choice
        b.entities.append(e)
        b.depend(deps)
        done |= foot
        placed[model] = placed.get(model, 0) + 1
