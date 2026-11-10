"""The ambient light of an indoor map (#773). D2R lights a dungeon room from its biome's image-based light (the IBL a
vis file's `iblIntensity` scales: 1.0 in most) and the point lights its designers placed; the sun does not reach it.
A generated map copies the stock pieces and their lights tile by tile but not the room's lighting design, so away
from a torch the floor falls to the player's own light and the rest of the screen draws black, where the legacy view
shows the whole room (Torture Halls, Dark Temple, Demon Road: the player's light radius had to be raised by hand to see
the floor).

The fix is one visual-data box over the whole map (the box D2R's Kurast huts use, enableIBL only) that takes its IBL
from a stock vis file with a brighter value. The box takes the ambient only (enableGlobalLights off), so a
map's own key light stays; a biome already at or past AMBIENT keeps its own. (Torture Halls' stand-in tiles name a
trees.dt1, so a "draws trees" test cannot tell indoor from outdoor here: the table of dark biomes does.)"""
import zlib

from ..consts import TILE

ORDER = 950  # after floor_height (900), which lifts every entity with a position: the box stays at 0

AMBIENT = 2.5  # the iblIntensity an indoor map is lifted to: D2R's own palace cells (and Arachnid Lair's 3.0) draw dark rooms at this
# a biome's iblIntensity: the lowest of its vis files (data/hd/env/vis/<vis>_{dawn1,dawn2,day,dusk1,dusk2,night}.json;
# `python tools/pd2_lighting.py check` compares this with D2R's files)
BIOME_IBL = {
    "data/hd/env/biome/act1_catacombs.json": 1.0,
    "data/hd/env/biome/act1_caves.json": 2.0,
    "data/hd/env/biome/act1_crypt.json": 1.0,
    "data/hd/env/biome/act2_arcane.json": 1.0,
    "data/hd/env/biome/act2_maggot.json": 12.5,
    "data/hd/env/biome/act2_palace_cells.json": 2.5,
    "data/hd/env/biome/act2_sewer.json": 0.5,
    "data/hd/env/biome/act2_tomb.json": 0.12,
    "data/hd/env/biome/act3_jungle_dungeon.json": 1.0,
    "data/hd/env/biome/act3_sewer.json": 1.0,
    "data/hd/env/biome/act4_lava.json": 0.375,
    "data/hd/env/biome/expansion_baallair.json": 0.95,
    "data/hd/env/biome/expansion_icecave.json": 1.25,
    "data/hd/env/biome/expansion_wildtemple.json": 1.0,
}
# the stock vis file an AMBIENT box takes its IBL from, and what the box needs loaded
VIS = "data/hd/env/vis/act2_palace_cells_day.json"
VIS_DEPS = {"json": [{"path": VIS}],
            "particles": [{"path": "data/hd/vfx/particles/env/vis/vfx_act2_palace_vis_motes.particles"}],
            "textures": [{"path": "data/hd/vfx/textures/default/White.texture"}],
            "other": [{"path": "data/hd/env/lut/default.dds"}]}
BOX = "pd2r_ambient_visbox"
MARGIN = 5 * TILE  # HD units the box reaches past the map's edge


def box(width: int, height: int) -> dict:
    """The visual-data box over a width x height tile map: IBL only, as D2R's Kurast hut boxes (scale is half the extent)."""
    return {"type": "Entity", "name": BOX, "id": 0, "components": [
        {"type": "TransformDefinitionComponent", "name": f"{BOX}_TransformDefinitionComponent",
         "position": {"x": width * TILE / 2, "y": 0.0, "z": height * TILE / 2},
         "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
         "scale": {"x": width * TILE / 2 + MARGIN, "y": 200.0, "z": height * TILE / 2 + MARGIN},
         "inheritOnlyPosition": False},
        {"type": "VisualDataBoxDefinitionComponent", "name": f"{BOX}_VisualDataBoxDefinitionComponent",
         "min": {"x": -1.0, "y": -1.0, "z": -1.0}, "max": {"x": 1.0, "y": 1.0, "z": 1.0},
         "filename": VIS, "priority": 1, "targetId": str(zlib.crc32(f"{BOX}:{width}x{height}".encode())),
         "enableInsideLighting": True, "enableHopeLightShadows": True, "enableGlobalLights": False,
         "enableIBL": True, "enableGradingLutFile": False}]}


def wanted(biome: str) -> bool:
    """Whether a map of this biome gets the box: its IBL is below AMBIENT."""
    return BIOME_IBL.get(biome, AMBIENT) < AMBIENT


def run(b) -> None:
    if wanted(b.biome):
        b.entities.append(box(b.lay.width, b.lay.height))
        b.depend(VIS_DEPS)
