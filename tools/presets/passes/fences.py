"""The fence pass (#736): a legacy fence tile (a wall layer of a fence DT1: pd2_fidelity.classify) stands as a whole,
low D2R fence piece laid along its run, in place of the per-tile kit's pick, which is a stock piece of whatever the
stock tile drew there (a 17-unit palisade, a gate, a wall: Fortress's palisade, #686).

- Family: a wooden fence DT1 (fence, barset, graveyrd) takes the wood pieces, a stone one (chain_walls, foregate,
  stone_fence) the stone pieces. The pieces are fence_kit.json: D2R fence models under TALL (4 units, pd2_coverage), so
  none hides walkable ground; their entities, boxes and dependencies come from the stock presets named there.
- Run direction: a tile turns its piece along x or z, by the straight run of fence tiles through it (the longer; a lone
  tile by its orientation: 1 runs along z, the rest along x). One piece per tile, centred on it, as the legacy edge is
  one tile wide and the pieces span about a tile.
- Gates: orientations 8 and 9 (the end tiles of a fence DT1's gate) take the end cap.
- Only a tile whose every wall layer is a fence or an invisible wall (act4/mesa/inv_wall) is replaced; a fence beside
  another wall layer keeps the kit's pick. Trees and pasted rooms' pieces stay.
"""
from __future__ import annotations

import copy
import functools
import json
import re
import zlib
from pathlib import Path

import pd2_painter as painter

from ..consts import TILE
from ..entity import _transform

ORDER = 450
KIT = Path(__file__).resolve().parent.parent / "fence_kit.json"
STONE = re.compile(r"chain_walls|foregate|stone_fence|palisade")
POOLS = {"wood": ("spider_run", "wild_run"), "stone": ("ruin_long", "ruin_medium", "tomb_run")}
GATE, GATES = "tomb_end", (8, 9)
YAW_Z = {"x": 0.0, "y": -0.707106769, "z": 0.0, "w": 0.707106769}  # turns a piece's long x axis along z
STAND = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}


@functools.cache
def kit() -> dict:
    return json.loads(KIT.read_text("utf-8"))


def fence_tiles(lay) -> dict[tuple[int, int], tuple[str, bool, int]]:
    """Each pure fence tile -> (family, whether it is a gate end, its fence layer's orientation). Pure: every wall layer
    is a fence or an invisible wall (act4/mesa/inv_wall)."""
    import pd2_fidelity as fidelity  # it imports pd2_presets, which imports this package: not at the top
    out = {}
    for c, (kind, layers) in lay.keys.items():
        if kind != "w":
            continue
        kinds = [fidelity.classify(t[0], t[1], "wall") for t in layers]
        if "fence" not in kinds or any(k not in ("fence", "invisible", None) for k in kinds):
            continue
        fence = [t for t, k in zip(layers, kinds) if k == "fence"]
        fam = "stone" if STONE.search(fence[0][0].lower().rsplit("/", 1)[-1]) else "wood"
        out[c] = (fam, any(t[1] in GATES for t in fence), min(t[1] for t in fence))
    return out


def along(tiles: dict, c: tuple[int, int]) -> str:
    """"x" or "z": the way the run of fence tiles through c is longer; a tie or a lone tile goes by orientation."""
    def reach(dx: int, dz: int) -> int:
        n = 0
        while (c[0] + dx * (n + 1), c[1] + dz * (n + 1)) in tiles:
            n += 1
        return n
    ax, az = reach(1, 0) + reach(-1, 0), reach(0, 1) + reach(0, -1)
    return "x" if ax > az else "z" if az > ax else "z" if tiles[c][2] == 1 else "x"


def piece(name: str, c: tuple[int, int], axis: str) -> dict:
    """The kit entity `name` on tile c, centred on it along `axis`."""
    k = kit()[name]
    e = copy.deepcopy(k["entity"])
    q = YAW_Z if axis == "z" else STAND
    x0, y0, z0, x1, y1, z1 = k["box"]
    cx, _, cz = painter.rotate(q, ((x0 + x1) / 2, 0.0, (z0 + z1) / 2))
    t = _transform(e)
    t["orientation"] = dict(q)
    t["position"] = {"x": c[0] * TILE + TILE / 2 - cx, "y": 0.0, "z": c[1] * TILE + TILE / 2 - cz}
    return e


def run(b) -> None:
    runs = fence_tiles(b.lay)  # a run goes through the pasted rooms' fences too
    tiles = {c: v for c, v in runs.items() if c in b.rest}
    if not tiles:
        return

    def stands_on(e: dict) -> bool:
        p = _transform(e)["position"]
        return (int(p["x"] // TILE), int(p["z"] // TILE)) in tiles and not painter.is_tree(e)
    b.loose = [e for e in b.loose if not stands_on(e)]
    used = set()
    for c in sorted(tiles):
        fam, gate, _ = tiles[c]
        pool = POOLS[fam]
        name = GATE if gate else pool[zlib.crc32(f"{b.seed}:{c[0]}:{c[1]}:fence".encode()) % len(pool)]
        used.add(name)
        b.loose.append(piece(name, c, along(runs, c)))
    for name in sorted(used):
        b.depend(kit()[name]["dependencies"])
