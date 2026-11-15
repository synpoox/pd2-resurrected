# Cartographer candidate: frozen contracts (#946, epic #942)

The native-map base every Wave 1 lane builds on. A change to anything below is a shared change: it needs a checkpoint.

## What ticket 4 ships

| Piece | Where | Value |
|---|---|---|
| Native map | `files/data/global/excel/d2rloader/cartographer/maps.txt` | `Tunneling Depths`, Cave, Loop, item `cmc`, Medium, level 85 |
| Settings | `.../cartographer.txt` | `EventChance 0` (PD2 events stay in `core-maps`), `PlayerStats 1` |
| Level slots | `.../slots.txt` | Maze 202, MazeCrypt 203, MazeLava 204, MazeIce 205, Outdoor 206, OutdoorSnow 207, OutdoorSiege 208 |
| Levels | `merge/ids-levels.tsv` | 202-208 `Cartographer ...` (copies of PD2 area rows); lvlmaze rows for 202-205 in `patches/070-cartographer-base.json` |
| Item type | `merge/ids-itemtypes.tsv` | `cmap` (id 245), parent `misc`; the PD2 `map` and `tNN` types are untouched |
| Item | `patches/070-cartographer-base.json` | misc `cmc` "Tunneling Depths Map": type `cmap`, `pSpell 16`, `belt 0`, `useable 1`, `spellicon -1`, `compactsave 0`, `spawnable 1`, `rarity 0` (`spawnable 0` would keep the item out of treasure class generation) |
| Stats | `merge/ids-itemstatcost.tsv` | `cartographer_map_level` 564, `cartographer_mon_life` .. `_speed` 565-571, `cartographer_map_density` 572 (extended saves allow ids past 511, ADR 0003) |
| Properties | patch | `map-level`, `mon-life`, `mon-damage`, `mon-fire-res`, `mon-cold-res`, `mon-light-res`, `mon-poison-res`, `mon-speed`, `map-density` |
| Affixes | `merge/ids-magic*.tsv`, `ids-rare*.tsv` | `cmap` only: Teeming, Savage, Hastened, of the Swarm, of Embers, rare Labyrinth / Warren; magic groups 9201-9205 |
| Acquisition pool | patch | treasure class `Cartographer Maps` (one pick, `cmc`); `Act 1-5 (H) Good` slot `Item9` points at it instead of PD2's `Map Tier 1`, keeping PD2's odds (Prob 1). Tierless. |
| Catalyst marker | patch, stat `map_force_event` (id 553) | `Save Param Bits` and `Send Param Bits` 6. The marker rides **layer 1** (`MarkerLayer`, [event-link](event-link.md)). Lane B ticket 10 adapts the recipe and owns the game proof that the layer survives a save. |

None of it reads PD2 map items, affixes or recipes. The PD2 map rows still exist (lane A's removal ticket deletes them); `map_open.cpp` already yields item spell 16 to Cartographer, so PD2 maps cannot open here.

## Frozen consumer contracts

- **Map item**: misc type `cmap`, `pSpell 16`, `belt 0`, `useable 1`, `spellicon -1`, `compactsave 0`. A lane adds a map by adding a `maps.txt` row and a misc row (clone `cmc`).
- **Opening**: Harrogath, Hell, red portal, one map per game (Cartographer's rule). No lane changes it.
- **Event link**: [event-link.md](event-link.md) (#944). Metadata on layered stats only.
- **Reserved ids**: levels 202-208 taken, 209 and up free for lane statics (never build uber arena replacements: vanilla mini-ubers and D2R Tristram arena with PD2 bosses); stats 564-572 taken, 573 up free; item type 245 taken; magic affix groups 9201-9205 taken.
- **Gapless `+` lines**: new ids run on after the current maximum in the `merge/ids-*.tsv` file, one lane at a time.

## Four-lane base and file ownership

| Lane | Owns |
|---|---|
| A native maps | `.../cartographer/maps.txt`, `slots.txt`, new `cmap` misc rows, native affix rows and the acquisition pool in its own patch files |
| B general rules | cube, Almanac, Catalyst recipe: `plugin/core-maps/lane-b/`, its own patch files |
| C map events | `plugin/core-maps/lane-c/`, `mapevents.txt` |
| D ubers | `plugin/core-maps/lane-d/` and uber data patches |
| Shared (serial) | `merge/ids-*.tsv` ids, `patches/070-cartographer-base.json`, `cartographer.txt`, `plugin/core-maps/parts` registration, this file |

`tools/tests/test_core_maps_lanes.py` enforces the plugin allowlist; - Appending new affix ids to `merge/ids-magic*.tsv` and `ids-rare*.tsv`: lane A, one PR at a time, always after the current maximum; lanes B, C and D ask lane A (the shared serial role during a checkpoint) instead of appending, so #948 and #949 cannot collide.
- Uber-key recipes and uber tiers: recipe and tier behavior is lane B (core-rules); the uber-only access, rewards and fights that use them are lane D.
- Lane A's removal of PD2 map stats must keep `map_force_event` (553): it is the Catalyst marker.

`tools/tests/test_cartographer_base.py` checks the data base.

## Known caveats

- `cmap` has `Equiv1 misc` (copied from Herb), so a `misc` type-based pick could roll a map. Not seen in any treasure class; lane A decides whether to cut the parent.
- Affix names are literal English, not string keys.
- `LevelEntry` on levels 202-208: the `+` lines set it empty, but the merged `levels.txt` keeps the template area's string (for example `EnteringCaveLevel1` on 202). Harmless; Cartographer's map names replace the entry text.
