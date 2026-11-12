# Plan: a standalone map system plugin (working name "MapKit")

Revised 2026-11-11, after the owner's decisions D2–D5, from origin/main 97489f6.

## Goal

MapKit is a fully **standalone D2RLoader plugin** that any D2R mod can drop in, with all of its content defined in data tables.

1. **Any number of maps per game, one live at a time.** Opening a new map erases the previous one completely. Ground items and corpses are gone. The open is refused while anyone is still inside the old map.
2. **Map stats:** monster, area and player stats, rolled on the map item.
3. **Procedural layouts.** Each map has **one tileset** and its own **shape rules**. Every open builds a new random layout from that tileset only, following those rules. For example, "Shorelines" builds only from its Act 3 tileset as a mostly straight path with few dead ends and corners.
4. **Map events** can happen inside a map.

### Not in scope

- **PD2-Resurrected does not move to MapKit.** core-maps keeps its own map code. MapKit has no content interface for other plugins (no ADR 0004 amendment) and none of PD2's scripted map content.
- **Our own tile-level generator** (D5). It is left out for now; layouts come from D2's DRLG, steered by MapKit.
- **MapKit and core-maps never load together.** Both hook item spell 16 and the same spawn functions. A test slot runs MapKit with core-maps switched off.

## 1. What shapes the design

- **A level is generated once per game.** To open a new map, the level must be freed and rebuilt in place. The SDK has no level, DRLG, portal or room service, so this needs native hooks. The "SDK before native hooks" rule allows that, with the reason stated in the code.
- **Rebuilding a level in a running game has been done before.** locbones/D2RHUD-2.4 does it. Its steps are:
  1. Free the level's units and rooms.
  2. Set a new seed at `drlg+0x860` around a native `AllocateLevel` call.
  3. Keep the old level record as an unlinked tombstone until the act is freed.
  4. Clear the preset and superunique "spawned" bits, then repopulate.
  5. Send the seed to clients and wait for each one to confirm.

  It has no license, so we take only the approach and copy no code. Every function is found again in our own 93847 Ghidra project.
- **Our own code shows what "one live map" needs.** In today's core-maps, stats, area writes and event progress are game-wide single slots, and the limit is only the `'MAPS'` counter (`map_open.cpp:224`). MapKit keeps one live-map record per game and resets all of it on every open.
- **Slot levels.** Since only one map is live at a time, MapKit needs only a few reusable level rows, which it re-parameterises on each open. That means one slot per DRLG kind, plus one per extra level of a multi-level map, all in the act of the opening town. The 255 level-id limit is no concern.
- **Standalone from this repo's code.** MapKit links only d2rAI-toolkit's `plugin_common` and the PluginSDK. It never uses `plugin/common/pd2_engine.h`: the addresses it needs go into the toolkit's `rvas_93847.h`. It is built in `plugin/mapkit/` for now, so the repo's CI, Ghidra projects and game slots apply, but it can move to its own repo unchanged.
- **HD.** D2R draws a generated level from the same preset pieces it was built from. A map may only use pieces that have HD presets, and MapKit checks this when it loads.

## 2. Opening a map

The flow starts when a player uses a map item (item spell 16 by default; configurable).

1. **Gates.** The map's row sets the gates: difficulty, character level, quest and town. If the live map still holds a player, the open is refused with the message "the current map is still in use" (D2). The item is kept, and the refusal goes to the player as client message 0x15, as today.
2. **Erase the live map, if there is one:**
   - remove its portal pair;
   - free all its units and rooms, with ground items and corpses gone (D3);
   - keep the old level record as a tombstone;
   - restore the slot's monster region from its saved base copy;
   - clear all map stats and event state.
3. **Generate the new map:**
   1. Load the map's row and roll a seed.
   2. Write the tileset and DRLG parameters into the slot level.
   3. Build the **layout graph** from the map's shape rules (§3).
   4. Call `AllocateLevel` with the layout-guide hook active.
   5. Post-check the result. If it fails, re-roll the seed, up to N tries, then fall back to the map's fixed `fallback` preset.
4. **Apply stats.** Route the item's map stats into the monster, player and area lists (§4), then apply the area entries to the slot's region.
5. **Roll an event** (§5).
6. **Open the portal.** A red portal pair runs from town to the map's entry.
7. **Multiplayer.** Send `{map id, seed}` to each client. Each client rebuilds the same layout and confirms before the portal lets it in.

The live map is a single record per game: `{map, seed, slot, item stats, monster, player and area lists, event state, portal pair}`. A unit counts as "in a map" when its level is the live slot or one of its extra levels.

## 3. Maps, tilesets and shape rules

### `maps.txt`: one row per map

| Column | Meaning |
|---|---|
| `map`, `name` | Map key and display string (e.g. Shorelines) |
| `item` | Map item code(s) that open it |
| `tier`, `arealevel` | Tier and base area level |
| gates | Difficulty, character level, quest, town level |
| `leveltype` | **The one tileset** the map builds from |
| `drlg` | `maze`, `outdoor` or `preset`. `preset` is one fixed ds1 with no generation |
| `pieces` | The tileset's allowed LvlPrest pieces, as a list or by tag. The default is every piece of `leveltype` that has HD presets |
| shape columns | See below |
| `monsters`, `umonsters` | Monster pool (mon1–25, umon) |
| `palette`, `sound` | Palette and level sound |
| `extralevels` | Further map rows linked by warps (multi-level maps) |
| `fallback` | A preset used if the generator fails |

### Shape columns (per map)

| Column | Meaning | Shorelines example |
|---|---|---|
| `pathlen` | Minimum and maximum rooms on the main path, entry to exit or boss | 14–18 |
| `straightness` | Chance the path keeps its direction at each step (0–100) | 85 |
| `maxturns` | Maximum corners on the main path | 4 |
| `branches` | Minimum and maximum side branches (dead ends) | 0–2 |
| `branchlen` | Maximum rooms per branch | 2 |
| `loops` | Whether the path may rejoin itself | no |
| `bounds` | Maximum width and height in rooms | 20×6 |
| `bossroom` | Where the boss or end room goes: `end`, `farthest` or `none` | end |
| `required` | Pieces that must appear (entry, boss room, event rooms) | entry, dock-end |
| `eventslots` | Rooms on the main path that may host an event | 3 |

### How a layout is built

1. **Layout graph.** MapKit draws an abstract room graph on a grid from the shape columns and the seed. It walks a main path of `pathlen` rooms, keeps going straight with chance `straightness`, and turns at most `maxturns` times. It then adds `branches` dead ends of up to `branchlen` rooms each, all inside `bounds`. The boss room sits at the path's end. This is cheap code with no engine calls, so it can be unit-tested outside the game.
2. **Guided DRLG.** D2's own builder still places the tileset's real pieces and tiles, but a hook makes it follow the graph instead of its own random walk:
   - **Maze levels.** The builder adds rooms by picking directions. The hook gives it the graph's directions and limits each pick to pieces whose exits match the graph cell.
   - **Outdoor levels** (Act 3 jungle, Kurast-style shorelines). These are built from outdoor sub-presets on a grid, not from maze rooms. Phase 2 checks whether that builder can be steered the same way. If it cannot, outdoor maps use their `fallback` preset until a generator of our own exists (D5).
3. **Post-check.** Walk the real room graph and measure path length, turns, dead ends and bounds against the shape columns, plus the `required` pieces and connectivity. If any check fails, re-roll.

## 4. Map stats

`mapstats.txt` routes each item stat to one of three lists:

- **monster:** applied as a state on every map monster when it spawns.
- **player:** added while the player is in the map, and removed on leaving.
- **area:** a slot such as density, area level, rarity, extra groups, extra boss, fortify, ethereal, socket and corrupt drops, death drops, or multi-level.

A row can also mark a stat as physical-as-extra, or name an item-drop effect.

The mechanism follows today's core-maps code (region formulas `map_stats.cpp:309-382`, hooks `:826-843`), rewritten for MapKit. Stat ids come from the table, never from C++. Region writes start from the saved base copy each time, so effects never stack.

## 5. Map events

`mapevents.txt` columns:

| Column | Meaning |
|---|---|
| `event` | Event key |
| `weight` | Weight when one event is drawn from many |
| chance by item quality | Chance the map rolls an event at all |
| `forcestat` | A stat on the item that forces an event |
| `maps` | The maps it may appear on |
| `kind` | `monster`, `superunique`, `object` or `portal` (to another map row) |
| `unit`, `count`, `repeats` | What spawns, how many, how often |
| `progress` | The rooms-activated threshold that triggers it |
| `spot` | Spot rule, e.g. an `eventslots` room |
| `notify`, `sound` | Notify string and sound |

Event state belongs to the live map and is cleared on erase. Scripted events, such as PD2's Altar guardian AI, are out of scope: MapKit has no content interface.

## 6. Phases

Each phase gets one issue and a PR, with a game check in Hell on a level 90+ character. The table-diff and unit gate applies. All test content ships as a small MapKit test pack, separate from PD2's data.

| # | Phase | Work | Done when |
|---|---|---|---|
| 0 | Name + glossary | Pick the name. Add glossary entries for *slot level*, *layout graph*, *live map*. Record in an ADR that MapKit is standalone and that core-maps and MapKit are never loaded together | ADR merged |
| 1 | **Spike: erase and rebuild a level** (time-box ~2 days) | In 93847 Ghidra find AllocateLevel, level free, act room free, the seed field, the level list, preset and superunique bits, and the client's level build. Test build: `mapkit regen <level>` console command. Write `docs/research/level-regeneration.md` | One level rebuilt 20 times in one game, then leave and start a new game: no crash, a new layout each time |
| 2 | **Spike: steering the DRLG** (time-box ~3 days) | Find where AllocateLevel reads LevelType, LvlMaze and LvlPrest, and whether per-open overrides are safe. Find the **maze** direction and piece pickers, and the **outdoor** sub-preset grid builder, and whether each can follow a given graph. Find what the client needs to rebuild the same layout. Add the findings to the research doc | A maze slot built on a hand-written straight graph, a second build with a different tileset, and a yes or no on steering outdoor levels |
| 3 | Skeleton | `plugin/mapkit` target, standalone, with no `pd2_engine.h`. CustomTableService loading and validation, including the HD piece check. Slot-level rows. Item spell, gates, refuse-while-occupied, erase, portal pair. One `preset` map | Open five maps in a row; each erases the last; opening is refused while a player is inside |
| 4 | Layout graph | Shape columns and a pure graph generator with unit tests (path length, turns, branches, bounds, determinism by seed) | 1000 seeded graphs per test map meet their rules |
| 5 | Guided maze | Parameter write-in, maze guide hook, post-check with re-rolls, fallback | A test maze map builds 50 times in game, within its shape each time |
| 6 | Guided outdoor | The outdoor builder guide, if phase 2 said yes; otherwise outdoor maps use their fallback and phase 6 is dropped | Shorelines-style test map builds within shape |
| 7 | Map stats | `mapstats.txt`, three lists, region base copy, drop effects, player add/remove | Stats right on monsters, players and drops; no stacking after 5 opens |
| 8 | Map events | `mapevents.txt`, roll, progress, spawn kinds, notify, reset on erase | Each kind fires; a reopened map starts with no progress |
| 9 | Multiplayer | `{map id, seed}` packet, client rebuild and confirm, kill credit, occupancy check across clients, map-stat HUD | Two-client LAN check |
| 10 | Release | README for mod authors, the test pack as an example (two tilesets, three maps, three stats, two events), versioned table formats, its own release zip | A mod with no PD2 data runs maps from tables only |

Phases 1 and 2 decide whether the rest holds; run them first, in parallel. Phase 4 needs no game and can start at any time.

## 7. Risks

- **Crashes after a rebuild.** Native code caches pointers to levels and rooms. Mitigations: the tombstone rule, erasing only an empty level, and phase 1's 20-rebuild stress check.
- **Parameter overrides leaking.** Writing into shared level and maze records also affects the client in single player and every other reader. Restore them after `AllocateLevel`, or point the slot at a private copy; phase 2 decides.
- **Steering limits.** D2's builders may not follow every graph, for example piece sizes on outdoor grids. Post-checks and re-rolls cover small gaps. Outdoor tilesets are the main unknown (phase 6, D5).
- **Client desync.** A wrong seed or wrong parameters put walls in the wrong place. The portal waits for each client to confirm.
- **License.** D2RHUD-2.4 has no license, so no code is copied. All addresses come from our own research.

## 8. Decisions

- **D1 Name:** open. The owner picks from the shortlist below.
- **D2 Old map occupied:** refuse until it is empty. ✅
- **D3 What is left in the erased map:** gone. ✅
- **D4 Content interface:** none. MapKit is fully standalone and PD2-Resurrected keeps core-maps. ✅
- **D5 Our own tile generator:** deferred. ✅

### Name shortlist (D1)

Each must be generic, without PD2 or other game branding. None has been checked against existing mods or trademarks.

| # | Name | Why |
|---|---|---|
| 1 | **Cartograph** (recommended) | Says "maps" plainly; one word |
| 2 | MapKit | Plain toolkit name, already in use as the working name |
| 3 | Tileweaver | Points at layouts woven from a tileset |
| 4 | Riftforge | Portals ("rifts") plus building new maps |
| 5 | Realmgate | The portal into a fresh realm |
| 6 | Mapforge | Forges a new map every open |
| 7 | Shiftlands | Lands that change every open |
| 8 | Wanderways | Procedural paths to wander |
| 9 | Endless Realms | Any number of maps per game |
| 10 | Dreadmaps | Fits Diablo's tone |
