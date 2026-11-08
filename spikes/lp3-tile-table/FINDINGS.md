# LP3: a classic tile -> HD piece table learned from D2R

Feasibility study for #213/#293 (2026-11-08, D2R build 93847). Scripts here run from the repo root after
`pd2_merge.py` and `modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/ --prefix data/hd/model_lod_desc.bin`.

## The table

`tile_table.py <scratch dir>` pairs every stock DS1 with its HD preset (2041 pairs, 303k entities, 602k tiles) and
records, per single tile (DT1, orientation, main, sub), the model sets D2R placed on it. `pd2_presets.Kit` already is
this table, keyed per DS1 key and per tile.

- 4231 wall tiles (3591 with an HD piece), 6178 floor tiles (4308 with a piece).
- Ambiguity: a median of 3 distinct piece sets per drawn wall tile; 35% have one set, 59% have one set on at least
  half their instances.
- Room-sized meshes (terrain.model, water_flow): 2090 entities. They carry baked walls (every Tomb room) and room
  water, which a per-tile table cannot transfer.

## Coverage of PD2's map tiles

| Presets | Kind | Seen and drawn | Never in stock | PD2 DT1, no stand-in | D2R draws nothing |
|---|---|---|---|---|---|
| on D2R tiles (39) | wall | 87.2% | 4.6% | 5.4% | 2.3% |
| on D2R tiles | floor | 82.3% | 7.8% | 7.6% | 2.0% |
| waiting on stand-ins (36) | wall | 43.6% | 2.3% | 48.7% | 5.4% |

`dt1_copies.py`: 16.5% of the PD2-tile uses in the waiting presets are pixel-identical to a D2R tile (some DT1 95-100%,
the largest 0%).

## A preview instead of per-map game checks

`bbox_preview.py <scratch dir> <min height> <preset names...>` places each entity's model box from
`data/hd/model_lod_desc.bin` (14,310 models; `u32 FNV-1a key | f32 bbMin[3] | f32 bbMax[3] | ...`, 44 bytes) and
scores the share of DS1 wall tiles (what the automap draws) under a piece at least that high. Its PNG marks bare walls
red, drawn walls green.

At 4 units it reproduces the slice 1 game check: Tomb Map 28-31% (bare walls in game), Monastery Map Basement 90%
(walls in game), Jungle Map 60-65%. Worst: tomb, mesa, arcane, palace, ashenplains; best: sewer, market, frozen river,
royal crypts (89-98%). It cannot judge looks, frame rate, collision or strings.
