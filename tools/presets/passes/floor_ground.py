"""The floor of each tile: a per-tile biome override where D2R's own presets draw that floor tile in another biome
(#740, part of #707).

The ground is one flat plane in the tile set's biome (passes/ground.py), so every floor of a generated map got the one
biome's ground texture. D2R's stock presets switch the biome per tile (`perTileBiomeOverrides`: tileX/tileY are DS1 tile
coordinates, checked on act1/monastry/facade1, whose court floor tiles are overridden in an outdoors preset): the
monastery's court floor is `act1_court` inside an `act1_outdoors` preset. This pass reads that from the tile set's stock
presets (`b.stock`): for each floor key, the biome it is drawn in there, by vote over its tiles (the effective biome:
the override, else the preset's own). A generated tile whose floor key (else its first tile's DT1) is drawn mostly in
another biome than the map's gets an override to that biome. The tiles of a pasted room are the room's: its own stock
overrides move with it.

This owns which biome a tile gets, not the plane's height (#742, passes/ground.py)."""
from __future__ import annotations

import zlib
from collections import Counter

ORDER = 810  # after ground, which adds the planes the overrides texture
SKIP = ("/test.json",)  # D2R's own test biome: some stock tiles are drawn in it, no real map is
MIN_SHARE = 0.5  # the biome must hold more than this share of a key's stock tiles
KEY = "perTileBiomeOverrides"


def floor_tile(key) -> tuple | None:
    """The tile whose DT1 names a floor key's ground: its first layer, None for a wall key (a wall tile's floor is
    not in the DS1's keys)."""
    return key[1][0] if key and key[0] == "f" and key[1] else None


def votes(stock: list) -> tuple[dict, dict]:
    """(floor key -> Counter of biomes, first tile's DT1 -> Counter of biomes) over the stock (layout, preset) pairs: each
    floor tile votes for its effective biome."""
    by_key: dict = {}
    by_dt1: dict = {}
    for lay, preset in stock:
        over = {(o["tileX"], o["tileY"]): o["biomeFilename"] for o in preset.get(KEY, ())}
        default = preset.get("biomeFilename")
        for c, k in lay.keys.items():
            t = floor_tile(k)
            if t is None or t[0] is None:
                continue
            biome = over.get(c) or default
            if biome and not biome.endswith(SKIP):
                by_key.setdefault(k, Counter())[biome] += 1
                by_dt1.setdefault(t[0], Counter())[biome] += 1
    return by_key, by_dt1


def winner(counts: Counter | None) -> str | None:
    if not counts:
        return None
    biome, n = counts.most_common(1)[0]
    return biome if n > MIN_SHARE * sum(counts.values()) else None


def biome_of(key, by_key: dict, by_dt1: dict) -> str | None:
    """The biome D2R draws a floor key in: its own tiles' vote, else its first tile's DT1's."""
    t = floor_tile(key)
    if t is None or t[0] is None:
        return None
    return winner(by_key.get(key)) or winner(by_dt1.get(t[0]))


def entry(x: int, y: int, biome: str, seed: str) -> dict:
    # D2R names an entry biomeEntry_<number>; ours is the hash of its place, so a build is reproducible
    return {"type": "TileBiomeOverride", "name": f"biomeEntry_{zlib.crc32(f'{seed}/{x}/{y}'.encode())}",
            "tileX": x, "tileY": y, "biomeFilename": biome}


def overrides(lay, stock: list, pasted: list, taken: set, biome: str, seed: str) -> list[dict]:
    """The overrides of a layout: a pasted room's stock overrides shifted to where it stands, and the other floor
    tiles' by the vote. Sorted by tile, so the output is stable."""
    out: dict = {}
    for n, ox, oy in pasted:
        for o in stock[n][1].get(KEY, ()):
            c = (o["tileX"] + ox, o["tileY"] + oy)
            if c in taken and 0 <= c[0] < lay.width and 0 <= c[1] < lay.height and o["biomeFilename"]:
                out[c] = o["biomeFilename"]
    by_key, by_dt1 = votes(stock)
    memo: dict = {}
    for c, k in lay.keys.items():
        if c in taken or k[0] != "f":
            continue
        if k not in memo:
            memo[k] = biome_of(k, by_key, by_dt1)
        if memo[k] and memo[k] != biome:
            out[c] = memo[k]
    return [entry(x, y, b, seed) for (x, y), b in sorted(out.items())]


def run(b) -> None:
    found = overrides(b.lay, b.stock, b.pasted, b.taken, b.biome, b.seed)
    b.mine("floor_ground")[KEY] = found
    for biome in sorted({o["biomeFilename"] for o in found}):
        b.depend({"json": [{"path": biome}]})
