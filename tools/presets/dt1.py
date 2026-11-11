"""Reading DT1 and DS1 headers."""
from __future__ import annotations

import struct
from pathlib import Path

from pd2_ds1 import norm


def dt1_tiles(data: bytes) -> dict[tuple[int, int, int], bytes]:
    """The (orientation, main, sub) of every tile in a DT1 -> its 25 subtile flags (bit 0 blocks walking), the
    first tile's for a key held twice."""
    count, at = struct.unpack_from("<2i", data, 268)
    out = {}
    for k in range(count):
        out.setdefault(struct.unpack_from("<3i", data, at + k * 96 + 20), data[at + k * 96 + 40:at + k * 96 + 65])
    return out


def header_dt1s(data: bytes) -> list[str]:
    """The DT1 paths (under global/tiles) a DS1's header lists: the editor's .tg1 names."""
    version, o = struct.unpack_from("<i", data)[0], 12 + 4 * (struct.unpack_from("<i", data)[0] >= 8)
    o += 4 * (version >= 10)
    out = []
    if version >= 3:
        n, o = struct.unpack_from("<i", data, o)[0], o + 4
        for _ in range(n):
            end = data.index(b"\0", o)
            name, o = norm(data[o:end].decode("latin-1")), end + 1
            if "/tiles/" in name:
                out.append(name.split("/tiles/", 1)[1].rsplit(".", 1)[0] + ".dt1")
    return out


class Tiles(dict):
    """DT1 path -> dt1_tiles, read from D2R's tiles on first use (an absent file holds nothing: stock lvltypes list DT1
    D2R does not ship. `strict`, for the stand-in lint, raises FileNotFoundError instead)."""
    def __init__(self, root: Path, strict: bool = False):
        super().__init__()
        self.root = root
        self.strict = strict

    def __missing__(self, path: str) -> dict:
        f = self.root / path
        if self.strict and not f.is_file():
            raise FileNotFoundError(f"no DT1 {f}")
        self[path] = dt1_tiles(f.read_bytes()) if f.is_file() else {}
        return self[path]

    def of(self, paths: list[str]) -> dict[str, dict]:
        return {p: self[p] for p in paths}
