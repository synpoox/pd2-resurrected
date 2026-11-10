"""build(): the preset for a layout, made by the passes in presets/passes/ in their ORDER."""
from __future__ import annotations

import copy
import json
import re
from dataclasses import dataclass, field

from . import passes
from .entity import _transform
from .kit import Kit
from .layout import Layout


@dataclass
class Build:
    """What the passes share: the inputs of build() and the preset so far. A pass reads and sets the fields it needs;
    a field a pass adds for later passes gets a default here."""
    lay: Layout
    kit: Kit
    name: str
    seed: str
    biome: str
    terrain: dict
    stock: list
    trees: tuple | None
    prefabs: object
    water: dict
    objects: list
    entities: list = field(default_factory=list)  # the preset's entities so far, without ids
    deps: dict = field(default_factory=dict)  # every dependency of the stock presets copied from
    missing: int = 0  # tiles left empty for a key the kit lacks
    pasted: list = field(default_factory=list)  # [(stock index, x, y)] of the pasted rooms
    taken: set = field(default_factory=set)  # the tiles of the pasted rooms
    held: set = field(default_factory=set)  # tiles a pasted room puts a piece on
    wet: frozenset = frozenset()  # stream DT1: their blocked floor stays water past the tree line
    edge: frozenset = frozenset()  # the ring just past the playable area: the border the automap draws
    far: frozenset = frozenset()  # the far side of the edge
    play: set = field(default_factory=set)  # the playable area
    shades: object = None  # painter.shader of the playable area, set with `far`
    near: frozenset = frozenset()  # border tiles with playable ground screen-up of them
    rest: dict = field(default_factory=dict)  # the tiles the per-tile kit fills
    loose: list = field(default_factory=list)  # the kit's and painter's pieces, joined to `entities` by a pass

    def depend(self, of: dict) -> None:
        for kind, items in of.items():
            for d in items:
                self.deps.setdefault(kind, {}).setdefault(d["path"].lower(), d)


def build(lay: Layout, kit: Kit, name: str, seed: str, biome: str, terrain: dict,
          stock: list[tuple[Layout, dict]] = (), trees: tuple | None = None, prefabs=lambda path: [(0.0, 0.0)],
          water: dict[str, tuple[dict, dict]] | None = None, objects: list[tuple[str, float, float]] = ()
          ) -> tuple[dict, int]:
    """The preset for a layout, and how many of its tiles stayed empty for a key the kit lacks. Over the per-tile kit,
    in order: stock rooms pasted whole, the patch lookup (both from `stock`), then the border painter with `trees`
    (pd2_painter.harvest) repaints the trees past the playable area; `prefabs(path)` gives a prefab's piece positions.
    Last, `water` (water DT1 -> a stock water plane entity and its preset's dependencies) covers that DT1's blocked
    floor outside pasted rooms. Each of the DS1's waypoint `objects` (waypoints()) gets one stock waypoint
    (place_waypoints). Each step is a pass (presets/passes/), run in its ORDER."""
    b = Build(lay, kit, name, seed, biome, terrain, list(stock), trees, prefabs, water or {}, objects)
    for p in passes.ordered():
        p.run(b)
    entities, terrain = b.entities, copy.deepcopy(terrain)
    for i, e in enumerate(entities):
        e["id"] = i + 1
    terrain["id"] = len(entities) + 1
    deps = b.deps
    for kind, path in [("json", biome)] + [("models" if p.endswith(".model") else "physics", p)
                                          for p in re.findall(r'"filename"\s*:\s*"([^"]+)"', json.dumps(terrain))]:
        deps.setdefault(kind, {}).setdefault(path.lower(), {"path": path})
    return {"dependencies": {k: list(v.values()) for k, v in deps.items()}, "type": "Preset", "name": name,
            "entities": entities, "terrain": terrain, "biomeFilename": biome, "perTileBiomeOverrides": [],
            "specialTiles": {}}, b.missing
