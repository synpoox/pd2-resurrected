"""Walkability per DS1 cell from DT1 subtile flags (+0x28, 25 bytes; bit 0 blocks walking)."""
import functools, struct
from pathlib import Path

@functools.cache
def dt1_flags(path):
    data = Path(path).read_bytes()
    count, at = struct.unpack_from("<2i", data, 268)
    out = {}
    for k in range(count):
        h = at + k * 96
        key = struct.unpack_from("<3i", data, h + 20)
        out.setdefault(key, data[h + 0x28:h + 0x28 + 25])
    return out

def cell_flags(lay_raw, roots):
    """lay_raw: P.Layout keys {(x,y): (kind, tiles)}; tiles are (dt1, orient, main, sub). OR of all layers' subtile
    flags per subtile -> 25 bytes per cell."""
    out = {}
    for c, (kind, tiles) in lay_raw.items():
        acc = bytearray(25)
        for dt1, o, m, s in tiles:
            if dt1 is None:
                continue
            for root in roots:
                p = Path(root) / dt1
                if p.is_file():
                    f = dt1_flags(str(p)).get((o, m, s))
                    if f:
                        for i in range(25):
                            acc[i] |= f[i]
                    break
        out[c] = bytes(acc)
    return out

def blocked(flags, bit=1):
    return sum(1 for b in flags if b & bit) >= 13
