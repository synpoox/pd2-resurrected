"""Shared builders for the test_presets_* tests: a synthetic DS1, DT1 and stock preset."""
import struct
A, B = ("act1/a/wall.dt1", 1, 2, 3), ("act1/a/wall.dt1", 4, 5, 6)
FLOOR = ("act1/a/floor.dt1", 0, 0, 1)


def cell(main: int, sub: int) -> int:
    return main << 20 | sub << 8 | 1


def ds1(w: int, h: int, walls: list[dict], floors: list[dict], files: list[str] = ()) -> bytes:
    """A version 18 DS1 of w x h tiles: walls[k] maps (x, y) to (orientation, main, sub), floors[k] to (main, sub)."""
    head = struct.pack("<6i", 18, w - 1, h - 1, 0, 0, len(files)) + b"".join(f.encode() + b"\0" for f in files)
    head += struct.pack("<2i", len(walls), len(floors))

    def grid(f) -> bytes:
        return struct.pack(f"<{w * h}I", *(f((i % w, i // w)) for i in range(w * h)))
    body = b"".join(grid(lambda c, L=L: cell(*L[c][1:]) if c in L else 0) + grid(lambda c, L=L: L[c][0] if c in L else 0)
                    for L in walls)
    body += b"".join(grid(lambda c, L=L: cell(*L[c]) if c in L else 0) for L in floors)
    return head + body + bytes(w * h * 4) + struct.pack("<i", 0)


def dt1(tiles: list[tuple[int, int, int]], blocked: set = frozenset()) -> bytes:
    """A DT1 of `tiles`; the ones in `blocked` block walking on all 25 subtiles."""
    out = bytearray(276 + 96 * len(tiles))
    struct.pack_into("<2i", out, 0, 7, 6)
    struct.pack_into("<2i", out, 268, len(tiles), 276)
    for k, t in enumerate(tiles):
        struct.pack_into("<3i", out, 276 + 96 * k + 20, *t)
        if t in blocked:
            out[276 + 96 * k + 0x28:276 + 96 * k + 0x28 + 25] = b"\1" * 25
    return bytes(out)


def entity(name: str, x: float, z: float, wall: tuple[int, int] | None = None) -> dict:
    comps = [{"type": "TransformDefinitionComponent", "position": {"x": x, "y": 0.0, "z": z},
              "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
             {"type": "ModelDefinitionComponent", "filename": f"data/hd/env/model/{name}.model"}]
    if wall:
        comps.append({"type": "WallTransparencyComponent", "wallTileLocalCoord": {"x": wall[0], "y": wall[1]}})
    return {"type": "Entity", "name": name, "id": 77, "components": comps}


TERRAIN = {"type": "Entity", "name": "terrain", "id": 9, "components": [
    {"type": "TransformDefinitionComponent", "position": {"x": 0.0, "y": 0.0, "z": 0.0}, "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
    {"type": "ModelDefinitionComponent", "filename": "data/hd/env/preset/act2/palace/celSE3/terrain.model"}]}


def stock() -> dict:
    return {"dependencies": {"models": [{"path": "data/hd/env/model/pillar.model"}],
                             "textures": [{"path": "data/hd/env/texture/pillar_alb.texture"}]},
            "entities": [entity("pillar", 3.0, 4.0, wall=(0, 0)), entity("torch", 14.0, 2.0),
                         {"type": "Entity", "name": "no transform", "components": []}],
            "terrain": TERRAIN, "biomeFilename": "data/hd/env/biome/x.json"}


def with_black(preset: dict) -> dict:
    from presets import void  # the void pass (#783) needs a stock preset that draws the black plane
    preset["entities"].append(entity("black", 0.0, 0.0))
    preset["entities"][-1]["components"][1]["filename"] = void.BLACK
    return preset


def at(e: dict) -> tuple[float, float]:
    return e["components"][0]["position"]["x"], e["components"][0]["position"]["z"]

