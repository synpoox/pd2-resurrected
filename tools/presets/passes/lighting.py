"""The ambient light of an indoor map (#773). D2R lights a dungeon room from its biome's image-based light (the IBL a
vis file's `iblIntensity` scales) and the point lights its designers placed; the sun does not reach it. A generated map
copies the stock pieces and their lights tile by tile but not the room's lighting design, so away from a torch the
floor falls to the player's own light.

The game check of #773 (HD against legacy at pinned spots, with D2R's own Halls of Pain, Tal Rasha's tomb and River of
Flame as controls) found a gap only where the biome's IBL is low and the floor carries no light of its own:
the tomb maps (IBL 0.12), the Dark Temple (baallair, 0.95) and the act 2 sewer maps (market2, Ruined Cistern, 0.5). The
lava maps draw as bright as legacy from their lava glow, and Torture Halls and the act 3 sewers draw as bright as
legacy from their torches: a lift there only washes them out. So the lift is opt-in per biome (LIFT), not a flat
rule over every dark biome.

The fix is a visual-data box over the map (the kind D2R's Kurast huts use, enableIBL only) that takes its IBL from a
stock vis file with a brighter value. The box takes the ambient only (enableGlobalLights and enableInsideLighting off),
so a map's own key light stays. Stock rooms pasted into the map bring their own boxes (all at priority 1, and D2R's
data does not say which of two equal boxes wins): the lift leaves out the area of every box already in the map, and
takes priority 0 as well."""
import zlib

from ..consts import TILE

ORDER = 950  # after floor_height (900), which lifts every entity with a position: the box stays at 0 (and after paste, 100)

AMBIENT = 2.5  # the iblIntensity a lifted map takes: D2R's own palace cells (and Arachnid Lair's 3.0) draw dark rooms at this
# the biomes that are lifted: biome -> None (every map on it) or the lower-case name prefixes of the maps that are
# (Dark Temple only: Throne of Destruction and Fallen Gardens share its biome and were not checked)
LIFT = {
    "data/hd/env/biome/act2_tomb.json": None,
    "data/hd/env/biome/act2_sewer.json": None,
    "data/hd/env/biome/expansion_baallair.json": ("darktemple",),
}
# a lifted biome's iblIntensity: the lowest of its vis files (data/hd/env/vis/<vis>_{dawn1,dawn2,day,dusk1,dusk2,night}.json;
# `python tools/pd2_lighting.py check` compares this with D2R's files)
BIOME_IBL = {
    "data/hd/env/biome/act2_sewer.json": 0.5,
    "data/hd/env/biome/act2_tomb.json": 0.12,
    "data/hd/env/biome/expansion_baallair.json": 0.95,
}
# the stock vis file the box takes its IBL from, and what the box needs loaded
VIS = "data/hd/env/vis/act2_palace_cells_day.json"
VIS_DEPS = {"json": [{"path": VIS}],
            "particles": [{"path": "data/hd/vfx/particles/env/vis/vfx_act2_palace_vis_motes.particles"}],
            "textures": [{"path": "data/hd/vfx/textures/default/White.texture"}],
            "other": [{"path": "data/hd/env/lut/default.dds"}]}
BOX = "pd2r_ambient_visbox"
MARGIN = 5 * TILE  # HD units the box reaches past the map's edge
PRIORITY = 0  # below the stock boxes' 1


def rect_of(e: dict) -> tuple[float, float, float, float] | None:
    """(x0, z0, x1, z1) of an entity's visual-data box, or None when it has none or is turned (its footprint is not a rectangle)."""
    c = {c["type"]: c for c in e.get("components", [])}
    t, v = c.get("TransformDefinitionComponent"), c.get("VisualDataBoxDefinitionComponent")
    if not t or not v:
        return None
    o = t["orientation"]
    if abs(o["x"]) + abs(o["y"]) + abs(o["z"]) > 1e-6:
        return None
    p, s = t["position"], t["scale"]
    xs, zs = sorted((p["x"] + s["x"] * v["min"]["x"], p["x"] + s["x"] * v["max"]["x"])), sorted(
        (p["z"] + s["z"] * v["min"]["z"], p["z"] + s["z"] * v["max"]["z"]))
    return xs[0], zs[0], xs[1], zs[1]


def minus(r: tuple, hole: tuple) -> list[tuple]:
    """The rectangle r without the rectangle hole: up to four rectangles."""
    x0, z0, x1, z1 = r
    hx0, hz0, hx1, hz1 = hole
    if hx0 >= x1 or hx1 <= x0 or hz0 >= z1 or hz1 <= z0:
        return [r]
    out = [(x0, z0, x1, min(hz0, z1)) if hz0 > z0 else None, (x0, max(hz1, z0), x1, z1) if hz1 < z1 else None]
    zlo, zhi = max(hz0, z0), min(hz1, z1)
    out += [(x0, zlo, hx0, zhi) if hx0 > x0 else None, (hx1, zlo, x1, zhi) if hx1 < x1 else None]
    return [o for o in out if o]


def box(r: tuple, name: str) -> dict:
    """The visual-data box over the rectangle r (HD units): IBL only, as D2R's Kurast hut boxes (scale is half the extent)."""
    x0, z0, x1, z1 = r
    return {"type": "Entity", "name": name, "id": 0, "components": [
        {"type": "TransformDefinitionComponent", "name": f"{name}_TransformDefinitionComponent",
         "position": {"x": (x0 + x1) / 2, "y": 0.0, "z": (z0 + z1) / 2},
         "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
         "scale": {"x": (x1 - x0) / 2, "y": 200.0, "z": (z1 - z0) / 2}, "inheritOnlyPosition": False},
        {"type": "VisualDataBoxDefinitionComponent", "name": f"{name}_VisualDataBoxDefinitionComponent",
         "min": {"x": -1.0, "y": -1.0, "z": -1.0}, "max": {"x": 1.0, "y": 1.0, "z": 1.0},
         "filename": VIS, "priority": PRIORITY, "targetId": str(zlib.crc32(f"{name}:{r}".encode())),
         "enableInsideLighting": False, "enableHopeLightShadows": True, "enableGlobalLights": False,
         "enableIBL": True, "enableGradingLutFile": False}]}


def boxes(width: int, height: int, taken: list[tuple] = ()) -> list[dict]:
    """The boxes over a width x height tile map, past its edge by MARGIN, without the rectangles in `taken`."""
    rs = [(-MARGIN, -MARGIN, width * TILE + MARGIN, height * TILE + MARGIN)]
    for hole in taken:
        rs = [piece for r in rs for piece in minus(r, hole)]
    return [box(r, f"{BOX}_{i}" if i else BOX) for i, r in enumerate(rs)]


def wanted(biome: str, name: str = "") -> bool:
    """Whether a map of this biome and name (the DS1's stem) gets the box."""
    if biome not in LIFT:
        return False
    only = LIFT[biome]
    return only is None or name.lower().startswith(only)


def run(b) -> None:
    if wanted(b.biome, b.name):
        taken = [r for r in map(rect_of, b.entities) if r]
        b.entities.extend(boxes(b.lay.width, b.lay.height, taken))
        b.depend(VIS_DEPS)
