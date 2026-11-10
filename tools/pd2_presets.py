"""HD presets for the PD2-only presets on D2R tiles (LP3, ADR 0005): one generated preset JSON per DS1.

Two halves with one seam, the Layout. layout() reads a DS1 into a layout: per tile, a key for what lies there, its
wall tiles (one per wall layer) or, with no wall, its floor tiles. A tile is named (DT1 file, orientation, main
index, sub index), the DT1 being the first, by path, of the DS1's DT1 files that holds that tile: a PD2 preset's are
the ones its level type and Dt1Mask load, a stock DS1's the ones its header lists. build() makes the preset from a
layout and a kit, harvested from D2R's stock DS1 and presets: each stock entity belongs to the tile under its
position (one tile is TILE HD units, DS1 x along x, DS1 y along z), so a key collects every instance D2R drew on such
a tile, empty ones too. build() gives each tile one of its key's instances, picked by a hash of the DS1 path and the
tile, so props come at D2R's density. A key no stock preset has falls back to each of its tiles alone (a wall on two
layers), else the tile stays empty. The ground is D2R's stock flat plane (TERRAIN), one unscaled copy per
TERRAIN_SIZE block (a scaled plane does not draw), textured by the tile set's biome; the preset depends on what the stock presets it copies from depend on. Over the
per-tile kit, build() pastes whole stock rooms, applies the patch lookup and, for a DS1 that draws tree tiles, the
border painter (pd2_painter), all from the tile set's stock: the presets that list a DT1 its DS1 draw from. Last,
the water: a stock room draws its stream as one room-sized mesh, so outside pasted rooms a water DT1's blocked floor
tiles get a stock water plane, scaled to cover them in rectangles.

merge/presets-hd.d/*.tsv (merge/presets-hd.tsv is its pd2_merge anchor: no lines; presets.apply.rule_files gives the read order): `tile set <tab> biome`, one line per lvltypes Name the PD2-only presets on D2R tiles load. The
merge fails on a PD2-only preset on D2R tiles whose tile set has no line, and on a line no such preset needs. A
preset that loads a PD2 DT1 (pd2assets/) waits until each of them has a stand-in line (LP4/LP5), `PD2 DT1 <tab> D2R
DT1`, or `-` for none: a PD2 tile the stand-in holds too (the same orientation, main and sub index and subtile flags;
most PD2 DT1 are copies or recolours of a D2R one) is named by it, so it gets that tile's stock pieces; the rest stay
empty in HD, their
collision kept. A stand-in line no PD2-only preset loads fails the merge too. `water:<D2R DT1> <tab> <water plane
model>` names a DT1 whose blocked floor is water (WATER_SPAN gives each plane's size); a line for a DT1 no PD2-only
preset draws from fails. Needs D2R's tiles and
presets in modkit's cache, else it makes none (dev builds run without, ADR 0005):
`python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/`.
"""

from presets.apply import apply, pd2_only_presets, read_rule, rule_files, stand_in  # noqa: F401
from presets.build import build  # noqa: F401
from presets.consts import *  # noqa: F401,F403
from presets.dt1 import Tiles, dt1_tiles, header_dt1s  # noqa: F401
from presets.entity import _transform, is_waypoint, room_terrain  # noqa: F401
from presets.hooks.far import far_off  # noqa: F401
from presets.hooks.plants import PLANT, is_plant  # noqa: F401
from presets.kit import Kit, Piece, indoor  # noqa: F401
from presets.layout import Layout, _floor, draws_trees, layout, walkable, wanted  # noqa: F401
from presets.patches import patches  # noqa: F401
from presets.place import STRUCTURE, fades, like, place, structure  # noqa: F401
from presets.rooms import rooms, sunken  # noqa: F401
from presets.waypoints import place_waypoints, waypoint_options, waypoints  # noqa: F401
from presets.water import rectangles, squarish, streams, water_planes  # noqa: F401

import pd2_dt1_copies as dt1_copies  # noqa: F401,E402  (the tests reach these through this module)
import pd2_painter as painter  # noqa: F401,E402
import pd2_terrain as terrain_ground  # noqa: F401,E402
