"""PD2 DT1 tiles that are pixel-identical copies of a D2R tile, at any index (#214).

Many PD2 DT1 copy D2R's tiles under new indexes (`sanctuaryreindexed.dt1` is D2R's `act2/arcane/sanctuary.dt1`
reordered), which a stand-in line in merge/presets-hd.d/ cannot catch: a line names a D2R DT1 whose tiles stand in
at the same index. `copies` finds them by content (each block's place, format and pixels, plus the 25 subtile flags),
so the generator (pd2_presets) names a copied tile by its D2R tile and draws that tile's stock pieces. Built from the
PD2 and D2R files at merge time; nothing from PD2 is committed (ADR 0002).
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

Tile = tuple  # (DT1 path, orientation, main, sub)


def tile_images(data: bytes) -> dict[tuple[int, int, int], str]:
    """(orientation, main, sub) -> a hash of the tile's image (its blocks' place, format and pixels) and subtile
    flags, for every tile with an image; the first tile's for a key held twice. Tiles with no block draw nothing in
    classic, so they are left out: they would match any other empty tile."""
    count, at = struct.unpack_from("<2i", data, 268)
    out: dict[tuple[int, int, int], str] = {}
    for k in range(count):
        h = at + k * 96
        key = struct.unpack_from("<3i", data, h + 20)
        ptr, _length, blocks = struct.unpack_from("<3i", data, h + 72)
        if key in out or blocks <= 0:
            continue
        sha = hashlib.sha1(data[h + 40:h + 65])
        for b in range(blocks):
            x, y, _, gx, gy, fmt, ln, _, off = struct.unpack_from("<hhhbbhihi", data, ptr + b * 20)
            sha.update(struct.pack("<hhbbhi", x, y, gx, gy, fmt, ln) + data[ptr + off:ptr + off + ln])
        out[key] = sha.hexdigest()
    return out


def d2r_images(tiles_root: Path) -> dict[str, list[Tile]]:
    """Image hash -> every D2R tile holding that image, from each DT1 under `tiles_root` (paths lower-case, with /)."""
    out: dict[str, list[Tile]] = {}
    for f in sorted(tiles_root.rglob("*.dt1")):
        path = f.relative_to(tiles_root).as_posix().lower()
        try:
            images = tile_images(f.read_bytes())
        except struct.error:  # a truncated DT1 holds nothing usable
            continue
        for key, image in images.items():
            out.setdefault(image, []).append((path, *key))
    return out


def copies(pd2: dict[str, bytes], d2r: dict[str, list[Tile]], standins: dict[str, str | None]) -> dict[Tile, Tile]:
    """Each tile of the PD2 DT1 `pd2` (path -> bytes) whose image a D2R tile holds -> that D2R tile. Of several, the
    one in the DT1's stand-in line comes first, then one at the same index, then the first by path."""
    out: dict[Tile, Tile] = {}
    for path, data in pd2.items():
        for key, image in tile_images(data).items():
            homes = d2r.get(image)
            if homes:
                out[(path, *key)] = min(homes, key=lambda t: (t[0] != standins.get(path), t[1:] != key, t))
    return out
