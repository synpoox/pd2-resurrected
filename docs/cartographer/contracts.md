# Cartographer candidate: frozen contracts (#946, epic #942)

The native-map base every Wave 1 lane builds on. A change to anything below is a shared change: it needs a checkpoint.

## What ticket 4 ships

| Piece | Where | Value |
|---|---|---|
| Native map | `files/data/global/excel/d2rloader/cartographer/maps.txt` | Twenty-five maps, one per Cartographer tileset, Medium, level 85: `Tunneling Depths` (Cave, Loop, `m01`), `Cathedral of Light` (Crypt, Branching, `m02`), `Ancestral Trial` (Ice, Winding, `m03`), `Torajan Jungle` (Jungle, Winding, `m04`), `Canyon of Sescheron` (Snow, blank Shape, a documented default for Snow, `m05`); #1287 added `Phlegethon` (Lava, Sprawl, `m06`), `Battlefields of Eternity` (Durance, Branching, `m07`), `Horazon's Memory` (Arcane, Loop, `m08`), `Arreat Battlefield` (Siege, `m09`), `Howling Plateau` (Mesa, `m10`), `Kehjistan Marketplace` (Desert, `m11`), `Shadows of Westmarch` (Wilderness, `m12`), `Ruins of Corvus` (Kurast, `m13`), `Leoric's Passage` (Dungeon, `m14`), `Cursed Hold` (Jail, `m15`), `Khazra Den` (Lair, `m16`), `Cellar of the Damned` (KurastSewer, `m17`), `Bastion Keep` (Barracks, `m18`), `Imperial Palace` (Harem, `m19`), `Sanatorium` (Basement, the Palace Cellar, `m20`), `Ruined Cistern` (Sewer, `m21`), `Royal Crypts` (Catacombs, `m22`), `Tomb of Zoltun Kulle` (Tombs, Branching, `m23`), `Lost Temple` (Spider, `m24`) and `Dark Temple` (Temple, `m25`), each with a PD2 map boss from `bosses.txt`. The plugin takes at most 16 boss entries, so `bosses.txt` has 16 and the later maps share one (Dungeon and KurastSewer, Jail and Barracks, Lair and Spider each use the same boss). A new tileset in d2r-tools needs a map here (`tools/tests/test_native_catalogue.py` lists them) |
| Settings | `.../cartographer.txt` | `EventChance 0` (PD2 events stay in `core-events`), `PlayerStats 1` |
| Level slots | `.../slots.txt` | Maze 138, MazeCrypt 139, MazeLava 140, MazeIce 141, Outdoor 142, OutdoorSnow 143, OutdoorSiege 144 (202-208 before #1231), MazeDurance 152, MazeArcane 153, OutdoorMesa 154, OutdoorDesert 155, OutdoorWilderness 156, OutdoorKurast 157, MazeDungeon 158, MazeJail 159, MazeLair 160, MazeKurastSewer 161, MazeBarracks 162, MazeHarem 163, MazeBasement 164, MazeSewer 165, MazeCatacombs 166, MazeTombs 167, MazeSpider 168, MazeTemple 169 (#1287, made by d2r-tools' `tools\install.ps1` on a copy of the tables; the arena levels 145-151 sit between) |
| Levels | `merge/ids-levels.tsv` | 138-144, 152-157 and 158-169 `Cartographer ...` (copies of PD2 area rows); lvlmaze rows for 138-141, 152, 153 and 158-169 in the committed `lvlmaze.txt` |
| Item type | `merge/ids-itemtypes.tsv` | `cmap` (id 245), parent `misc`; `t1m`/`t2m`/`t3m` are its children (#1304, `Equiv1 cmap`); the other PD2 `map` and `tNN` types are untouched |
| Item | the committed `misc.txt` | misc `m01` "Tunneling Depths Map": type `t1m`/`t2m`/`t3m` (its tier, children of `cmap`), `pSpell 16`, `belt 0`, `useable 1`, `spellicon -1`, `compactsave 0`, `spawnable 1`, `rarity 0` (`spawnable 0` would keep the item out of treasure class generation) |
| Stats | `merge/ids-itemstatcost.tsv` | `cartographer_map_level` 564, `cartographer_mon_life` .. `_speed` 565-571, `cartographer_map_density` 572, then the 64 catalogue stats `cartographer_mon_tohit` .. `cartographer_mon_phys_as_mag` 573-636 in d2r-tools' installer order (#1331; its installer refuses ids of 511 or more, so PD2R places them), then 13 mod stats 637-649 (#1309: `cartographer_map_drop_weapons`/`_armor`/`_crafting`/`_jewelry`, `cartographer_map_extra_doll` .. `_shriek`; one per Param, routed in `statroutes.txt`) (extended saves allow ids past 511, ADR 0003) |
| Properties | the committed `properties.txt` | `map-level`, `mon-life`, `mon-damage`, `mon-fire-res`, `mon-cold-res`, `mon-light-res`, `mon-poison-res`, `mon-speed`, `map-density`, then `mon-tohit` .. `mon-phys-as-mag`, one per catalogue stat (#1331), then ids 448-468 (#1309): `map-drop-weapons`/`-armor`/`-crafting`/`-jewelry`, `map-extra-doll` .. `map-extra-shriek` and the shared-roll combos `mon-att-pierce`, `mon-cast-speed`, `mon-lifesteal-hp`, `mon-nofreeze-hp`, `map-xp-drop`, `map-swing-cast`, `map-speed-all`, `map-mag-gold`; `d2rloader/cartographer/statroutes.txt` routes the 13 mod stats to `drop_tc` / `extra_groups` with their Param |
| Affixes | `merge/ids-magic*.tsv`, `ids-rare*.tsv` | `cmap` only: Teeming, Savage, Hastened, of the Swarm, of Embers, rare Labyrinth / Warren; magic groups 9201-9205 |
| Acquisition pool | the committed `treasureclassex.txt` | `Act 1-5 (H) Good` slot `Item9` names `Map Tier 1` (PD2's; #1307). `Cartographer Maps` below is the tierless pool used by Horazon's drops (`plugin/core-events/horazon.cpp`) (the 25 maps, Picks 1, equal odds: a class holds 10 items, so m06-m25 sit in `Cartographer Maps B`, which the pool names at weight 20, and B names `Cartographer Maps C` (m14-m21, weight 12), which names `Cartographer Maps D` (m22-m25, weight 4)) instead of PD2's `Map Tier 1`, keeping PD2's odds (Prob 1). A class must be defined before any row that names it, so the unreferenced row 121 is renamed (`Act 1 Melee A`) into the class there. Tierless. |
| Catalyst marker | the committed `itemstatcost.txt`, stat `map_force_event` (id 553) | `Save Param Bits` and `Send Param Bits` 6. The marker rides **layer 1** (`MarkerLayer`, [event-link](../features/event-link.md)). Lane B ticket 10 adapts the recipe and owns the game proof that the layer survives a save. |

None of it reads PD2 map items, affixes or recipes. PD2's map rows, code and hooks are cut (#964): `tools/pd2_cutmaps.py` deletes the map items, recipes, properties and drop entries, and core-events hooks neither item spell dispatcher, so Cartographer owns both. Kept ids: the reserved levels 145-151 and stats 564-572, and the cut items' itemstatcost and itemtypes rows as inert numbers (ADR 0003). PD2's cut map and uber levels are not kept: #1231 deleted levels 138-201 (and their lvlprest rows and lvlwarp 85-89) outright, so the Cartographer levels moved from 202-215 to 138-151 (the owner's decision, 2026-11-16). `merge/ids-levels.tsv` and the other ids lists keep the PD2 names with id `-`.

## Frozen consumer contracts

- **Map icons**: the 60 owner-supplied icons are in `files/data/hd/global/ui/items/misc/pd2/cartoicon_<white|gold|red>_NN(.lowend).sprite` (white 01-20, gold 21-40, red 41-60; 98x98 HD, 49x49 lowend) and as legacy graphics `files/data/global/items/cartoicon_<colour>_NN.dc6` (28x28, units palette, as `icemap.dc6`). A map's misc `invfile` is `cartoicon_<colour>_NN`. A map's `items.json` asset is `pd2/cartoicon_<colour>_NN`, and it needs a copy of `hd/items/misc/pd2/icemap.json` under the same name for its ground model. Maps m01-m25 use 25 of them; the rest are for future maps.
- **Map item**: misc type `t1m`/`t2m`/`t3m` (children of `cmap`), `pSpell 16`, `belt 0`, `useable 1`, `spellicon -1`, `compactsave 0`. A lane adds a map by adding a `maps.txt` row and a misc row (clone `m01`).
- **Opening**: Harrogath, Hell, red portal, one map per game (Cartographer's rule). No lane changes it.
- **Event link**: [event-link.md](../features/event-link.md) (#944). Metadata on layered stats only.
- **Reserved ids**: levels 138-144 taken, 145-151 reserved for the private areas below (#947; 152 and up free; never build uber arena replacements: vanilla mini-ubers and D2R Tristram arena with PD2 bosses); stats 564-649 taken, 650 up free (none reserved: the fights keep state in game counters); item type 245 taken; magic affix groups 9201-9205 taken.
- **Gapless `+` lines**: new ids run on after the current maximum in the `merge/ids-*.tsv` file, one lane at a time.

## Four-lane base and file ownership

| Lane | Owns |
|---|---|
| A native maps | `.../cartographer/maps.txt`, `slots.txt`, new `cmap` misc rows, native affix rows and the acquisition pool in its own patch files |
| B general rules | cube, Almanac, Catalyst recipe: `plugin/core-events/lane-b/`, its own patch files |
| C map events | `plugin/core-events/lane-c/`, `mapevents.txt` |
| D ubers | `plugin/core-events/lane-d/` and uber data patches |
| Shared (serial) | `merge/ids-*.tsv` ids, the committed `misc.txt`, `lvlmaze.txt`, `cartographer.txt`, `plugin/core-events/part_registry.cpp` registration, this file |

`tools/tests/test_core_events_lanes.py` enforces the plugin allowlist.

- Appending new affix ids to `merge/ids-magic*.tsv` and `ids-rare*.tsv`: lane A, one PR at a time, always after the current maximum; lanes B, C and D ask lane A (the shared serial role during a checkpoint) instead of appending, so #948 and #949 cannot collide.
- Uber-key recipes and uber tiers: recipe and tier behavior is lane B (core-rules); the uber-only access, rewards and fights that use them are lane D.
- Lane A's removal of PD2 map stats must keep `map_force_event` (553): it is the Catalyst marker. The marker needs the func 24 property `map-force-event-l` (#953).
- Native event progress divides by the built level's real room count, not PD2's 200 (#956).

`tools/tests/test_cartographer_base.py` checks the data base.

## Known caveats

- `cmap` has `Equiv1 misc` (copied from Herb), so a `misc` type-based pick could roll a map. Not seen in any treasure class; lane A decides whether to cut the parent.
- Affix names are string keys in `item-nameaffixes.json`, added as `texts` and `ids` rows in `merge/strings.json` (a `string` op in a patch would replace the merged file, #996); a name without one reads "Missing string" on the tooltip.
- Slot level `LevelType` (138-144) is set by `set` ops on `levels` (`where: {"Id": ...}`): PD2's merge renumbers LevelType after the `+` lines, so a value in the `+` line is lost and `cartographer check` reports the wrong type.
- `cmap` gets `Magic=0 Rare=1 Normal=0` from the committed `itemtypes.txt` (no map rolls magic; #1304), and `t1m`/`t2m`/`t3m` are repointed to `cmap` with the same flags, and `m01` has `rarity 1`, `level 1`. The pool class `Cartographer Maps` sits on row 121, before the Good rows that name it: a treasure class defined after the rows that name it is never resolved (0 drops in 440,000), and patches can only append rows.
- Building the PD2 plugins works against the toolkit commit `toolkit.version` pins; #946 was built against an older toolkit, before the pin.
- `LevelEntry` on levels 138-144: the `+` lines set it empty, but the merged `levels.txt` keeps the template area's string (for example `EnteringCaveLevel1` on 138). Harmless; Cartographer's map names replace the entry text.

## Encounter placements (#947, frozen)

`plugin/common/placements.h` is the one place AI, server/client skills, stage travel, chests and re-entry read an encounter's level and coordinates from. A consumer never holds a level number or a coordinate of its own. Data: one file per encounter in `plugin/placements/` (globbed into both `core-events` and `core-rules`; no edit to CMake or any list). The `placements` part (order 55, lane S) logs the counts and the status command shows lookups that found nothing.

Interface (frozen; a change is a checkpoint): `Binding {encounter, level, target, Source, uber, noWaypoint}`, `Place {encounter, id, x, y, w, h, arg}` (w = h = 0 a point, else the region [x, x+w) x [y, y+h); `arg` a monstats class for a boss spot), `Reg`, `LevelOf(encounter)`, `BindingAt(level)`, `IsUberLevel`, `NoWaypointLevel`, `Find`, `Need`, `SpotOf`. `level` is what consumers read now; `target` the frozen destination. A private author flips `level` to `target` in the same PR that ships the area, and edits the places to the new geometry.

Consumers adapted once (all read the registry): Dclone AI (`dclone_ai.cpp`: centre, souls square), Lucion AI (centre, adds square), Lucion death (`lucion.cpp`: arena and Temple levels, Temple portal), Rathma stages and AI (`rathma.cpp` builds its stage table from `rathma1`-`rathma3`; `rathma_ai.cpp` drops its duplicate spot table), uber entry and re-entry (`uber_entry.cpp`: uber and no-waypoint levels, Tristram level, fountain, grey portals), Void/Tomb event (`map_events.cpp`: Bitter Depths level), the Rathma retreat portal levels (`rathma_ai.cpp`), whose former users `map_bosses.cpp` and `map_stats.cpp` are cut (#964). Chests (`lucion_chests.cpp`) hold no level or coordinate (the object is in the area's DS1); a chest placement goes in the Temple's file as `chest*` places. Skills: the core-rules fight skills (UberDiabSummon/Run, RathmaSummon, the Lucion kit) are built with their fights (#958-#962) and read `placements.h` too; none exists yet.

Reserved levels (appended in `merge/ids-levels.tsv`, so no later PR appends there; an author sets the real fields with `set` ops in its own patch). Before #1231 these were 209-215 (the "Replaces" column is the PD2 level each took the place of, not a number to keep):

| Level | Encounter (binding) | Lane / ticket | Replaces |
|---|---|---|---|
| 145 | `dclone` | D #958 | 138 (built: Cartographer's 1x1 Wilderness (Swamp) arena from `arenas.txt`, #958; its spots are `arenaspots.txt` rows read through `cartographer.arena.v1`, `plugin/core-events/arena_spots.cpp`, not `plugin/placements/`) |
| 146, 147, 148 | `rathma1`, `rathma2`, `rathma3` | D #961 | 160, 161, 162 (built: Cartographer's Mesa arenas from `arenas.txt`, #961; places in `plugin/placements/rathma.cpp` follow the `arenaspots.txt` spots) |
| 149 | `lucion` | D #962 | 184 (built: Cartographer's 1x1 Desert arena from `arenas.txt`, #962; its spots are `arenaspots.txt` rows read through `cartographer.arena.v1`) |
| 150 | `temple` | D #962 | 185 (built: Cartographer's 1x1 Desert Temple from `arenas.txt`, #962; the altar and chests are `arenaspots.txt` `Object` rows) |
| 151 | `bitterdepths` | C #960 | 215 (built: Cartographer's Mesa arena from `arenas.txt`, #960; its monsters are the row's `mon*` columns from PD2's Tomb Event Map 198, no spots) |

No bridge is justified (Horazon's #959 needs a second Cartographer map, which a fixed D2R level cannot be: d2r-tools#485; next free id 152). Frozen without a replacement arena: `miniuber1-3` = D2R levels 133-135 (vanilla, `miniubers.cpp`, bindings only), and `tristram` = D2R's Uber Tristram arena 136 with PD2's bosses (`Source::D2RTristram`, target 136; Tristram is now level 136 with no places (#957)). `harrogath` (109) holds the Blood Fountain and the three grey re-entry portals.

Exact file ownership (a lane edits only these for its encounter; lane S owns everything else listed above):

| Encounter | Placement data | Fight modules |
|---|---|---|
| Dclone (D #958) | `plugin/placements/dclone.cpp` | `lane-d/*`, `dclone_ai.*` |
| Rathma/Mendeln (D #961) | `plugin/placements/rathma.cpp` | `lane-d/*`, `rathma*.{cpp,h}` |
| Lucion, Temple (D #962) | `plugin/placements/lucion.cpp` | `lane-d/*`, `lucion*.{cpp,h}` |
| Mini-ubers, Tristram (D #955, #957) | `plugin/placements/miniubers.cpp` | `lane-d/*`, `uber_tristram.*`, `prime_minions.*` |
| Bitter Depths (C #960) | `plugin/placements/bitterdepths.cpp` | `lane-c/*`, `map_events.*` |
| Access, re-entry (D #951) | `plugin/placements/uber_entry.cpp` | `uber_entry.*`, `uber_scaling.*` |

The placement files need no generated output: nothing in `plugin/` reads `generated/`, `out/` or any DT1/DS1/converter product, and `tools/tests/test_placements.py` checks the files against `merge/ids-levels.tsv` and every consumer lookup.

Skipped by the owner: building a representative native static uber area (acceptance criterion 1). The registry is proven on the retained fights instead; the first new arena (#958) is the proof of native static authoring.
