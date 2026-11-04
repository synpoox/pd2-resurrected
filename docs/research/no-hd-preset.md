# A PD2 level without an HD preset

Task for issue #159 (map #143). Question: what D2R draws in HD for a PD2 level whose DS1 has no HD preset, and
whether we can write a preset ourselves.

Run on Slot5, 2026-11-04, build 93847, Reign of the Warlock mode. The fixture is not committed:
it holds a PD2 file (ADR 0002) and lived in a scratch folder.

## Setup

- Fixture: PD2's `Expansion/Map/UberAncients.ds1` (from `pd2maps.mpq`, 15,148 bytes) shipped at
  `data/global/tiles/expansion/map/uberancients.ds1`, and one patch: lvlprest `Act 5 - Baal Entrance` (Arreat Summit,
  level 120) `File1` = `Expansion/Map/UberAncients.ds1`. PD2's Uber Ancients level row is a copy of Arreat Summit's
  (LevelType 31, Dt1Mask 29360148, 21x29), so only the DS1 changes. Both DS1 are version 18 and 21x29; their tile
  layers differ.
- In game: `debug_mode`, `cheat god 1`, `cheat goto 120`.

## Results

| Run | HD preset at `data/hd/env/preset/expansion/map/uberancients.json` | What shows |
|---|---|---|
| 1 | none | Level loads, no crash, nothing in the log. HD draws a **flat, featureless ground** (the default grass texture, no biome look) to the screen edges: no cliffs, walls, floor tiles or decorations. Objects from the DS1 draw (the braziers). The automap draws the DS1's walls, and walking stops at them: **collision is the DS1's, invisible in HD** |
| 2 | D2R's `expansion/mountaintop/mtntop.json`, copied unchanged | Level loads, no crash. HD draws the **whole Arreat Summit scene** (snow biome, circle, pillars, gate, cliffs), with D2R's own terrain model. It does not match the Uber Ancients layout: the hero arrives standing inside a pillar, and the automap walls sit where the scene has none |

Classic graphics (G) are not available in Reign of the Warlock games ("Only Available in Classic and Expansion
games"), so the classic view of the DS1 could not be checked.

## What this means

- **D2R finds an HD preset by the DS1's path**, and a preset is a plain JSON file at a new path. It does not need a
  baked `terrain_lod0.model` of its own: the JSON names its terrain model, physics and every entity by full path, so
  it can point at D2R's stock files. Run 2 drew from MtnTop's model at a new preset path.
- **An imported DS1 with no preset is playable but not fit to ship**: correct collision, objects and monsters on a
  flat empty plain with invisible walls. Good enough for testing tables and scripts before the stand-in exists.
- **Writing a preset is scene authoring, not conversion.** A preset must place its walls, cliffs and props where the
  DS1's wall tiles are. D2R Reimagined does this in `scripts/maps/arenas.py` (branch `feature/maps`, GPL-3.0): a `Kit`
  harvests entity templates from a tileset's stock scenes, a `Scene` places clones of them at `10 * tile` HD units
  (one DS1 tile = 10 units), and the terrain is a stock flat 170x170 plane (`act2/palace/celse3`) scaled over the
  room, textured by the biome and terrain stamps. Each arena is a hand-written `build_<theme>()` that lays out the
  DS1 and the scene from one layout; it **cannot take an existing DS1** as input. Reimagined ships 825 preset JSON
  and no `.model` file, which agrees with run 2.
- For our 74 levels the same method applies the other way round: read each DS1's wall and floor tiles and place the
  matching stock entities (per tile set) at those cells. That is a generator per tile set, ours to write (copying
  Reimagined's code binds the release to GPL-3.0). For levels that are near-copies of a D2R room, starting from the
  D2R preset and moving the entities that differ may be cheaper.

## Not done

- Reimagined's builder was not run (the session's permission rules blocked running external code); the notes above
  come from reading its source at `404dee9`.
- Hell difficulty and a level 90 character were not used: the slot's save is a level 76 Barbarian in Normal. Neither
  matters for terrain drawing.
