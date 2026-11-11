"""DS1 and lvltypes readers, on their own so a rule that only reads a map does not take pd2_levels (and with it pd2_ids,
pd2_strings and pd2_tables) into its cache key (#808)."""
from __future__ import annotations

import struct

from pd2_table import Table

SLOTS = 32  # lvltypes File 1-32


# DS1 (version 18 and older), as 1.13c's loader reads it: header, file names, tile layers, then the object list
def ds1_header(data: bytes) -> tuple[int, int, int, int, int, int, int, int]:
    """(version, width, height, act 0-4, tag type, offset of the tile layers, wall layers, floor layers). Width and
    height count tiles; the layers are walls (a tile grid, then an orientation grid, per layer), floors, shadow, tags."""
    version, width, height = struct.unpack_from("<3i", data, 0)
    o, act, tag = 12, 0, 0
    if version >= 8:
        act = min(struct.unpack_from("<i", data, o)[0], 4)
        o += 4
    if version >= 10:
        tag = struct.unpack_from("<i", data, o)[0]
        o += 4
    if version >= 3:
        files = struct.unpack_from("<i", data, o)[0]
        o += 4
        for _ in range(files):
            o = data.index(b"\0", o) + 1
    if 9 <= version <= 13:
        o += 8
    walls, floors = 1, 1
    if version >= 4:
        walls = struct.unpack_from("<i", data, o)[0]
        o += 4
        if version >= 16:
            floors = struct.unpack_from("<i", data, o)[0]
            o += 4
    return version, width + 1, height + 1, act, tag, o, walls, floors


def object_list(data: bytes) -> tuple[int, int, int, int]:
    """(act 0-4, offset of the object count, object count, bytes per object)."""
    version, width, height, act, tag, o, walls, floors = ds1_header(data)
    o += (2 * walls + floors + 1 + (tag in (1, 2))) * width * height * 4
    if version < 2:
        return act, o, 0, 0
    return act, o, struct.unpack_from("<i", data, o)[0], 20 if version >= 5 else 16


def norm(path: str) -> str:
    return path.replace("\\", "/").lower()


def col(t: Table, name: str) -> int:
    return t.header.index(name)
