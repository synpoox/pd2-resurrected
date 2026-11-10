"""Stock water planes over a water DT1's blocked floor."""
from __future__ import annotations

import copy
import json
from pathlib import Path

import pd2_painter as painter

from .consts import FETCH, PRESETS, STREAM, TILE, WATER_RATIO, WATER_SPAN, WATER_Y
from .entity import _transform
from .layout import Layout


def rectangles(cells: set) -> list[tuple[int, int, int, int]]:
    """`cells` as (x, y, width, height) rectangles, greedily: the longest run along x, grown along y."""
    left, out = set(cells), []
    for x, y in sorted(cells, key=lambda c: (c[1], c[0])):
        if (x, y) not in left:
            continue
        w = 1
        while (x + w, y) in left:
            w += 1
        h = 1
        while all((x + i, y + h) in left for i in range(w)):
            h += 1
        left -= {(x + i, y + j) for i in range(w) for j in range(h)}
        out.append((x, y, w, h))
    return out


def squarish(rect: tuple[int, int, int, int]) -> list[tuple[int, int, int, int]]:
    """A rectangle cut along its long side into pieces at most WATER_RATIO times as long as they are wide."""
    x, y, w, h = rect
    if w > WATER_RATIO * h:
        n = -(-w // (WATER_RATIO * h))
        return [(x + w * i // n, y, w * (i + 1) // n - w * i // n, h) for i in range(n)]
    if h > WATER_RATIO * w:
        n = -(-h // (WATER_RATIO * w))
        return [(x, y + h * i // n, w, h * (i + 1) // n - h * i // n) for i in range(n)]
    return [rect]


def streams(lay: Layout, water: dict[str, tuple[dict, dict]], taken: set) -> list[tuple[dict, dict]]:
    """Stock water planes over a water DT1's blocked floor tiles outside the pasted rooms (whose own mesh draws theirs),
    one plane scaled to each near-square rectangle of tiles, with the dependencies of the preset it came from. A tile
    whose floor layers hold two water DT1 gets the first one's plane only."""
    out, done = [], set()
    for dt1 in sorted(water):
        plane, of = water[dt1]
        cells = {c for c, k in lay.keys.items() if c not in taken and c not in done and k[0] == "f"
                 and any(t[0] == dt1 for t in k[1]) and painter.blocked_floor(lay.flags.get(c, bytes(25)))}
        done |= cells
        span = WATER_SPAN[next(c["filename"] for c in plane["components"] if c["type"] == "ModelDefinitionComponent")]
        for x, y, w, h in (r for rect in rectangles(cells) for r in squarish(rect)):
            e = copy.deepcopy(plane)
            t = _transform(e)
            t["position"] = {"x": (x + w / 2) * TILE, "y": WATER_Y, "z": (y + h / 2) * TILE}
            t["orientation"] = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}
            t["scale"] = {"x": w * TILE / span, "y": 1.0, "z": h * TILE / span}
            e["name"] = STREAM
            out.append((e, of))
    return out


def water_planes(d2r_data: Path, rule: dict[str, str]) -> dict[str, tuple[dict, dict]]:
    """Water DT1 -> (the first entity drawing its line's model in the smallest stock preset that has one, that
    preset's dependencies)."""
    found: dict[str, tuple[dict, dict]] = {}
    for p in sorted((d2r_data / PRESETS).rglob("*.json"), key=lambda p: (p.stat().st_size, p)):
        if len(found) == len(set(rule.values())):
            break
        text = p.read_text("utf-8-sig")
        for model in set(rule.values()) - set(found):
            if model in text:
                preset = json.loads(text)
                # the model may be named only in the dependencies or a prefab: then a later preset holds it
                e = next((e for e in preset["entities"] if any(c.get("filename") == model for c in e["components"])), None)
                if e:
                    found[model] = (e, preset["dependencies"])
    if lost := sorted(set(rule.values()) - set(found)):
        raise ValueError(f"no stock preset draws the water plane {', '.join(lost)}: run `{FETCH}`")
    return {dt1: found[model] for dt1, model in rule.items()}
