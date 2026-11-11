"""Void (#783): the tiles where legacy draws nothing, a pit or the black past the map, which HD must not floor.

A tile is empty in legacy when its DS1 cell has no layer at all, or only floor layers whose DT1 tile is missing or has
no blocks (the PD2 DT1 `hellgate` floor of the Descent, `aer_palace` floor 0): nothing is drawn there, so the screen
stays black. Walkable tiles are never empty (a player can stand on them, the ground stays); a wall cell is not empty
either (its floor layer may be drawn under it, and a blocker is a wall's business).

This module is the shared seam: the DS1-to-cells reader (`empty`, from a legacy layout and `Drawn`), the cells the merge
hands each build (`configure`, `of`) and the stock black piece (`black`). Passes: `void_kit` (the kit leaves the empty
tiles bare) and `void` (planes and black cover). pd2_parity scores the result with the same `empty`."""
from __future__ import annotations

import json
import struct
from pathlib import Path

from .consts import PRESETS
from .layout import Layout, walkable

BLACK = "data/hd/env/model/act4/fort/act4_fort_stairs/black_square01.model"  # a flat black plane, 100 x 100 units
BLACK_PIECE = "pd2r_void"  # the entity name of a black cover, for the scorer
SIGHT = 7  # tiles from walkable ground that are covered: what the camera shows (pd2_parity's SIGHT)
CELLS: dict[str, frozenset] = {}  # DS1 path (Build.seed) -> its empty cells, set by apply()
STOCK: list = []  # [(the stock black plane entity, its dependencies)] once apply() has found it (black())


def drawn_tiles(data: bytes) -> frozenset:
    """The (orientation, main, sub) of every DT1 tile that has at least one block: the others draw nothing."""
    count, at = struct.unpack_from("<2i", data, 268)
    out = set()
    for k in range(count):
        o = at + k * 96
        if struct.unpack_from("<i", data, o + 80)[0] > 0:
            out.add(struct.unpack_from("<3i", data, o + 20))
    return frozenset(out)


class Drawn(dict):
    """DT1 path -> drawn_tiles, read on first use from the first root that holds the file (PD2's, then D2R's); an absent
    file holds nothing."""

    def __init__(self, *roots: Path):
        super().__init__()
        self.roots = roots

    def __missing__(self, path: str) -> frozenset:
        f = next((r / path for r in self.roots if (r / path).is_file()), None)
        self[path] = drawn_tiles(f.read_bytes()) if f else frozenset()
        return self[path]


def empty(legacy: Layout, drawn: dict) -> frozenset:
    """The cells of a legacy layout (the DS1 named by its own DT1s) where nothing is drawn, walkable tiles apart:
    `drawn` maps a DT1 path to the keys of its tiles that have blocks (Drawn)."""
    walk = walkable(legacy)
    out = set()
    for x in range(legacy.width):
        for y in range(legacy.height):
            key = legacy.keys.get((x, y))
            if (x, y) not in walk and (key is None or key[0] == "f" and all(
                    t[0] is None or t[1:] not in drawn[t[0]] for t in key[1])):
                out.add((x, y))
    return frozenset(out)


def configure(ds1: str, cells: frozenset) -> None:
    CELLS[ds1] = cells


def of(b) -> frozenset:
    """The empty cells of a build's DS1: none when the merge gave none (a build run on its own)."""
    return CELLS.get(b.seed, frozenset())


def black(d2r_data: Path) -> tuple[dict, dict]:
    """(a visual-only entity of the stock flat black plane, its dependencies), taken from the smallest stock preset that
    draws it. The stock entity's physics body is dropped: the cover is drawn, never touched."""
    for p in sorted((d2r_data / PRESETS).rglob("*.json"), key=lambda p: (p.stat().st_size, p)):
        text = p.read_text("utf-8-sig")
        if BLACK in text:
            e = next((e for e in json.loads(text)["entities"]
                      if any(c.get("filename") == BLACK for c in e["components"])), None)
            if e:
                e["components"] = [c for c in e["components"] if c["type"] != "PhysicsBodyDefinitionComponent"]
                return e, {"models": [{"path": BLACK}]}
    raise ValueError(f"no stock preset draws {BLACK}: run `python -m modkit fetch --prefix data/hd/env/`")
