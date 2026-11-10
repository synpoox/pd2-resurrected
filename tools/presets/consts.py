"""The generator's constants and key types."""
from __future__ import annotations

from pathlib import Path

TILE = 10  # HD units per DS1 tile
GROUND = "_ground"  # a stock preset's pd2_terrain.Ground, set by apply() for Kit.add; never written out
# a floor tile's stock pieces standing higher than this above their ground are a building's upper parts (Kurast's
# pillars and roof lips stand 9-26 up), dropped as the tile may have no building in a PD2 DS1 (#682)
FLOOR_MAX = 8.0
# a DS1 cell's unwalkable bit: what blocks the plain floor in PD2's dashed tree lines and the ground past the paths
# (jungle1: on 96% of the lines' gap tiles; D2R's stock jungle rooms stand 88% of their trees on such tiles)
UNWALKABLE = 0x20000
TERRAIN = "act2/palace/celse3"  # a stock preset whose terrain is a flat TERRAIN_SIZE square (D2R Reimagined's plane)
TERRAIN_SIZE = 170
SINK = 1.0  # HD units: a pasted room's ground this far below the flat plane would be hidden by it (#705)
ROOM_MATCH, ROOM_MIN = 0.6, 16  # a stock room of ROOM_MIN tiles or more is pasted where this share of its tiles match
ROOM_BARE = 0.001  # the share of a room's tiles that may be walkable DS1 floor off its walkable floor (#713:
# Uber Tristram's 2156-tile town has one PD2 edit; a room with tens of such tiles draws walls or lava on open ground)
FAR = 20.0  # HD units (2 tiles): a piece whose mesh stands this far clear of its position belongs to a structure
SQUARE = [(i, j) for j in (-1, 0, 1) for i in (-1, 0, 1)]
CROSS = [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)]
PRESETS = Path("hd") / "env" / "preset"
# a stock water plane's model -> its side in HD units, centred on its position (its .physics box: +-90)
WATER_SPAN = {"data/hd/env/model/act3/kurast/act3_kurast_water/water_16x16_01.model": 180.0}
WATER_Y = 0.05  # just over the flat ground plane (y 0), which would hide water at the stock rooms' -5 to 0
STREAM = "pd2r_stream_water"  # the name of the water planes streams() places, for the merge's count
WATER_RATIO = 2  # a water rectangle longer than this many times its width is split, so its texture stretches less
FETCH = "python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/"
TREE_LAYER = 14  # the orientation of a DT1's tree tiles
LIKE = 12  # the orientation whose PD2 tiles without a stand-in borrow a stock tile's pieces (like())

Tile = tuple  # (DT1 path or None, orientation, main, sub)
Key = tuple  # ("w" or "f", tiles)
