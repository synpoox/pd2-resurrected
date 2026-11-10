"""Stock pillars for a PD2 DT1's pillar tiles (orientation 12, #737): the model family and height like() ranks by.

A pillar tile is one DS1 cell of a standing column (orientation 12 is "pillar" in the DT1 format). like() lends it a
stock piece by subtile collision; borrowed by collision alone, the nearest footprint was a chair, a shrine or a cage.
So a candidate is ranked by its model first (a pillar or column, PILLAR, at least PILLAR_TALL high), then by collision.
A tile with no such LIKE key in the kit stands a stock pillar taken from the kit's pieces of the tile set the layout
draws (standing()), centred on the tile: Torture Halls' flesh pillars have no stock tile at all, but the wildtemple's
wall tiles hold `pillar01` and `column01`. One piece per tile, as the legacy DS1 has one pillar per cell, not the
stock walls' rows of them."""
from __future__ import annotations

import dataclasses
import functools
import re
from pathlib import Path
from typing import Callable

from .consts import TILE, Key
from .entity import _transform

# a pillar or column, by the model's folder and file name (pd2_fidelity.HD_MODEL's "pillar", which scores it)
PILLAR = re.compile(r"pillar|column")
PILLAR_TALL = 4.0  # HD units: as pd2_coverage.TALL, the height a piece hides a wall at, and pd2_fidelity scores tall at
KIND = "pillar"  # the key kind of a stand-alone stock pillar in place()'s kit (never a DS1 key)


def model(e: dict) -> str:
    """The folder and file name of an entity's model, lower case; "" if it has none."""
    for c in e["components"]:
        if c["type"] == "ModelDefinitionComponent":
            return "/".join(c.get("filename", "").lower().split("/")[-2:])
    return ""


def tall_enough(kit, e: dict) -> bool:
    top = kit.top(e)
    return top is None or top >= PILLAR_TALL


def is_pillar(kit, e: dict) -> bool:
    """Whether a stock piece is a pillar or column of pillar height."""
    return bool(PILLAR.search(model(e))) and tall_enough(kit, e)


def holds(kit, ents: list) -> bool:
    """Whether a kit instance's pieces hold a pillar."""
    return any(is_pillar(kit, e) for e, _, _ in ents)


def standing(kit, drawn: set[str]) -> list[tuple[int, list]]:
    """The stock pillars of the tile set the layout draws, one single-piece instance each, centred on its tile: every
    pillar piece in the kit instances whose key draws only `drawn` DT1s (a wall tile's pillar stands in the wall's
    row; here it stands alone), one per model and stock source, in model order."""
    seen: dict[tuple[str, int], tuple[int, list]] = {}
    for key, options in kit.instances.items():
        if key[0] == KIND or not key[1] or any(not t or t[0] not in drawn for t in key[1]):
            continue
        for src, ents in options:
            for e, _, _ in ents:
                if is_pillar(kit, e):
                    seen.setdefault((model(e), src), (src, [(e, TILE / 2, TILE / 2)]))
    return [seen[k] for k in sorted(seen)]


def with_stand_ins(kit, stand: list[tuple[int, list]]):
    """The kit with each stand-alone pillar under its own key ((KIND, ((None, -1, i, 0),)): no DS1 tile has it)."""
    if not stand:
        return kit
    return dataclasses.replace(kit, instances={**kit.instances, **{
        (KIND, ((None, -1, i, 0),)): [s] for i, s in enumerate(stand)}})


def stand_keys(kit) -> list[Key]:
    return [k for k in kit.instances if k[0] == KIND]


def model_top(d2r_data: Path) -> Callable[[dict], float | None]:
    """Kit.top from D2R's model boxes (data/hd/model_lod_desc.bin): the height of an entity's model above its own
    origin (the box's top times the piece's scale); None for a model it does not know, and without the file."""
    import pd2_coverage as coverage  # it imports this module
    models = d2r_data / coverage.MODELS
    if not models.is_file():
        return lambda e: None
    boxes = coverage.model_boxes(models.read_bytes())

    @functools.cache
    def top(path: str) -> float | None:
        bb = boxes.get(coverage.fnv(path))
        return None if bb is None else bb[4]

    def of(e: dict) -> float | None:
        t = _transform(e)
        scale = (t or {}).get("scale") or {"y": 1.0}
        return next((None if (h := top(c.get("filename", ""))) is None else h * scale["y"]
                     for c in e["components"] if c["type"] == "ModelDefinitionComponent"), None)
    return of
