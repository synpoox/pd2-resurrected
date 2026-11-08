"""The ground height of a stock D2R HD preset, from its terrain's collision mesh (#682).

D2R sculpts each preset's terrain (Kurast's terraces and canals, Harrogath's steps) and stands its pieces on it. The
generator (pd2_presets) draws a flat terrain instead, so a piece copied at its stock height floats or sinks by the
stock ground's height under it. `Ground` reads that height from the preset's `terrain.physics`, so the kit can keep
each piece's height above its own ground.

`terrain.physics` (D2R 93847): u64 offsets of the node, vertex and triangle tables at 0, then u32 node, vertex and
triangle counts at 0x18; a vertex is 4 f32 (x, y up, z, pad), a triangle 8 u32 whose first three are vertex indexes.
"""
from __future__ import annotations

import struct
from pathlib import Path

CELL = 10.0  # bucket size for the triangle lookup, in HD units (one DS1 tile)


class Ground:
    """Ground height at (x, z) in preset units: the highest triangle of the mesh over that point, None off it."""

    def __init__(self, data: bytes, offset: tuple[float, float, float] = (0.0, 0.0, 0.0)):
        _nodes, verts, tris = struct.unpack_from("<3Q", data, 0)
        nv, nt = struct.unpack_from("<2I", data, 0x1c)
        ox, oy, oz = offset
        v = [struct.unpack_from("<3f", data, verts + 16 * i) for i in range(nv)]
        self.v = [(x + ox, y + oy, z + oz) for x, y, z in v]
        self.cells: dict[tuple[int, int], list[tuple[int, int, int]]] = {}
        for k in range(nt):
            t = struct.unpack_from("<3I", data, tris + 32 * k)
            if max(t) >= nv:
                continue
            xs, zs = [self.v[i][0] for i in t], [self.v[i][2] for i in t]
            for cx in range(int(min(xs) // CELL), int(max(xs) // CELL) + 1):
                for cz in range(int(min(zs) // CELL), int(max(zs) // CELL) + 1):
                    self.cells.setdefault((cx, cz), []).append(t)

    def at(self, x: float, z: float) -> float | None:
        best = None
        for i, j, k in self.cells.get((int(x // CELL), int(z // CELL)), ()):
            (ax, ay, az), (bx, by, bz), (cx, cy, cz) = self.v[i], self.v[j], self.v[k]
            den = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz)
            if abs(den) < 1e-9:  # a wall face, seen edge-on from above
                continue
            l1 = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / den
            l2 = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / den
            if min(l1, l2, 1 - l1 - l2) >= -1e-4:
                y = l1 * ay + l2 * by + (1 - l1 - l2) * cy
                best = y if best is None else max(best, y)
        return best


def of_preset(preset: dict, d2r_data: Path) -> Ground | None:
    """The ground of a stock preset (its terrain entity's physics file, at the terrain's position), None without one."""
    terrain = preset.get("terrain") or {}
    comps = terrain.get("components", [])
    pos = next((c["position"] for c in comps if c.get("type") == "TransformDefinitionComponent"), None) or {}
    for c in comps:
        for fixture in c.get("fixturedefs", []) if c.get("type") == "PhysicsBodyDefinitionComponent" else ():
            name = fixture.get("shapetype", {}).get("filename", "")
            f = d2r_data / name.removeprefix("data/")
            if name.endswith(".physics") and f.is_file():
                try:
                    return Ground(f.read_bytes(), (pos.get("x", 0.0), pos.get("y", 0.0), pos.get("z", 0.0)))
                except struct.error:
                    return None
    return None
