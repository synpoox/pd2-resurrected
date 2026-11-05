# [LP3](https://github.com/synpoox/pd2-resurrected/issues/213) spike: Jungle Map HD presets with a border painter ([#213](https://github.com/synpoox/pd2-resurrected/issues/213))

A prototype, not repo code. It builds the three Jungle Map presets (`expansion/map/jungle1-3.ds1`) in four layers and
was judged "passable for alpha 0.1.0" in game. Porting it into `tools/pd2_presets.py` is the follow-up issue.

```bash
python spikes/lp3-border-painter/build_jungle.py OUT
```

Needs a merged `generated/data/` and modkit's D2R cache (`D2RAI_TOOLKIT`, default `../d2rAI-toolkit`). Copy
`OUT/jungle*.json` over `data/hd/env/preset/expansion/map/` in a slot's mod. The full-density presets need the
core-maps `pool` part (grown engine pools).

## The four layers (`build_jungle.py`)

1. **Pasted rooms:** a stock Act 3 room whose tile keys match 60% or more of a PD2 spot is pasted whole, with its own
   sculpted terrain, water mesh and bridges. 5-7 rooms per DS1 match 73-97% and cover 21-33% of the tiles.
2. **Patch lookup:** a tile whose 3x3 neighbourhood (else its 4-neighbour cross) appears in a stock preset copies what
   that stock preset placed on the centre tile, empty included. It covers 51-63% of the tiles.
3. **Per-tile fallback:** the remaining tiles get the stock per-tile kit (`pd2_presets.Kit`), without tall pieces
   (trees, canopy, hanging plants, physics bodies) off the wall tiles.
4. **Border painter (`painter.py`):** every tree from layers 2-3 is dropped and the trees are painted again from the
   playable area out.

## How the painter works

- **Playable area, at tile level.** Every wall tile widened by one tile seals PD2's thin tree lines. A sealed region
  holding a DS1 object, or one that streams (floor tiles with 13 or more blocked subtiles) run through and that doesn't
  touch the map's edge, is a path; the rest is outside. Paths then grow over lone trees (wall groups under 10 tiles)
  up to the lines (8-connected wall groups of 10 or more tiles), take a one-tile margin and the streams, and add the
  ground a 2-tile walk over DT1 subtile flags reaches. Line tiles are never playable.
- **The kit.** 11,463 trees harvested from 265 stock jungle rooms, keyed by direction (8 buckets) and distance to the
  nearest blocked subtile; each keeps how far it stood from its room's open ground. The stock spacing is 8.5 subtiles.
- **Placement.** Candidates are subtiles up to 30 out from the playable area, nearest first. A tree keeps the stock
  spacing and stands no closer to the playable area than its stock original. A tree prefab is a cluster of up to 100
  pieces over about 4x4 tiles; all its rotated pieces must stay off the playable area and the pasted rooms.
- **Three passes.** Tree clusters first, then single tree models in the gaps (same spacing), then canopy on its own
  spacing. With canopy first it took 2,472 of jungle1's spots and left 396 clusters; now about 2,900 clusters.
- **Bare lines.** A line with playable ground on both sides keeps the patch lookup's stock trees on its tiles.

Result: about 2,900 / 2,200 / 2,000 clusters per DS1, about 100 fps at the spawn (119 fps without the painter).

## What it gets wrong (the follow-up issue)

- **The lines are dashed.** PD2's tree lines have gaps of plain, unblocked floor tiles, yet the game blocks them and
  the automap draws trees there. The source of that blocking is not found: no level substitution (`SubType` -1),
  all DT1 tiles resolve. A 2-tile walk bound stands in for it, so some walkable ground gets trees (accepted for now).
- **Streams and bridges** come only from pasted rooms. The water surface is a per-room mesh
  (`act3_jungle_water_flow/<room>.model`), with no reusable plane, and the painter places no bridges.
- **Spots past the map entrance** still show bare ground where the automap draws lines, water and bridges.
- **Indoor tile sets** bake their walls and floor into each room's `terrain.model` (all 109 tomb rooms), and PD2's
  indoor maps match whole stock rooms on 0-30% of their tiles, so Tomb Map shows few walls. Catacomb rooms place walls
  as separate pieces, so Monastery Map Basement draws them.

## Files

- `build_jungle.py`: the four layers, writes the presets. Prints rooms, patch coverage and painted counts.
- `painter.py`: playable area, kit harvest, placement.
- `walk.py`: DT1 subtile flags per DS1 cell.
