"""The fence pass (#736): a legacy fence tile (a wall layer of a fence DT1: pd2_fidelity.classify) stands as a whole,
low D2R fence piece laid along its run, in place of the per-tile kit's pick, which is a stock piece of whatever the
stock tile drew there (a 17-unit palisade, a gate, a wall: Fortress's palisade, #686).

- Material: every fence DT1 left (pd2_fidelity.classify: siege/fence, a row of wooden stakes; outdoors/fence and
  descend/fence, rail fences) is wooden, so the piece is a wooden fence (act3_spider_fences/fence_low_a01), never a
  rock or stone wall (the first cut took wildtemple_rockwalls/fence03 for wood: a rock wall). A stone fence DT1 would
  need a stone family here. The piece is in fence_kit.json: a D2R fence model under TALL (4 units, pd2_coverage), so it
  hides no walkable ground; its entity, box and dependencies come from the stock preset named there.
- A fence DT1 whose walls are tall is no fence here (pd2_fidelity.TALL_FENCE: foregate, chain_walls, barset,
  graveyrd, town/fence, stone_fence): classify still calls it a fence, but this pass leaves its tiles to the wall pass.
- Run direction: a tile turns its piece along x or z, by the straight run of fence tiles through it (the longer); a
  tile with no longer run (a lone tile, or the elbow of an L) goes by its orientation (ALONG_Z). One piece per tile,
  centred on it, as the legacy edge is one tile wide and the pieces span about a tile.
- Gates: orientations 8 and 9 (the end tiles of a fence DT1's gate) take the run piece too: the kit has no wooden end
  cap.
- Only a tile whose every wall layer is a fence or an invisible wall (act4/mesa/inv_wall) is replaced; a fence beside
  another wall layer keeps the kit's pick. Trees and pasted rooms' pieces stay.
"""
from __future__ import annotations

import copy
import functools
import json
from pathlib import Path

import pd2_painter as painter

from ..consts import TILE
from ..entity import _transform

ORDER = 450
KIT = Path(__file__).resolve().parent.parent / "fence_kit.json"
PIECE = "spider_run"
# a tile with no longer run goes along z for the left-hand orientations (1 left wall, 4 and 5 the left corners), else x
ALONG_Z = frozenset({1, 4, 5})
YAW_Z = {"x": 0.0, "y": -0.707106769, "z": 0.0, "w": 0.707106769}  # turns a piece's long x axis along z
STAND = {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0}


@functools.cache
def kit() -> dict:
    return json.loads(KIT.read_text("utf-8"))


def fence_tiles(lay) -> dict[tuple[int, int], int]:
    """Each pure fence tile -> its fence layer's orientation. Pure: every wall layer
    is a fence or an invisible wall (act4/mesa/inv_wall)."""
    import pd2_fidelity as fidelity  # it imports pd2_presets, which imports this package: not at the top
    out = {}
    for c, (kind, layers) in lay.keys.items():
        if kind != "w":
            continue
        kinds = [fidelity.classify(t[0], t[1], "wall") for t in layers]
        kinds = ["wall" if k == "fence" and fidelity.TALL_FENCE.search(t[0].lower().replace("\\", "/")) else k
                 for t, k in zip(layers, kinds)]
        if "fence" not in kinds or any(k not in ("fence", "invisible", None) for k in kinds):
            continue
        fence = [t for t, k in zip(layers, kinds) if k == "fence"]
        out[c] = min(t[1] for t in fence)
    return out


def along(tiles: dict, c: tuple[int, int]) -> str:
    """"x" or "z": the way the run of fence tiles through c is longer; a tie or a lone tile goes by orientation."""
    def reach(dx: int, dz: int) -> int:
        n = 0
        while (c[0] + dx * (n + 1), c[1] + dz * (n + 1)) in tiles:
            n += 1
        return n
    ax, az = reach(1, 0) + reach(-1, 0), reach(0, 1) + reach(0, -1)
    return "x" if ax > az else "z" if az > ax else "z" if tiles[c] in ALONG_Z else "x"


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
    for c in sorted(tiles):
        b.loose.append(piece(PIECE, c, along(runs, c)))
    b.depend(kit()[PIECE]["dependencies"])
