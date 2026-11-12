# Plan: a standalone map system plugin "Cartographer"

Revised 2026-11-11. This version follows the owner's decisions and two research spikes:

- [DRLG steering, #929](https://github.com/synpoox/pd2-resurrected/pull/929): `docs/research/drlg-steering.md`
- [Level regeneration, #930](https://github.com/synpoox/pd2-resurrected/pull/930): `docs/research/level-regeneration.md`

The outdoor builder has its own plan: [cartographer-outdoor-builder.md](cartographer-outdoor-builder.md).

## Goal

Cartographer is a fully **standalone D2RLoader plugin** that any D2R mod can drop in, with all of its content in data tables.

1. **One map per game.** A map item in Act 5 (Harrogath) opens a red portal into the game's map level. Once a map has been opened, the game refuses another one.
2. **Map stats:** monster, area and player stats, rolled on the map item.
3. **Procedural layouts.** Each map has **one tileset** and its own **shape rules**. Each game builds a new random layout from that tileset alone, inside those rules. For example, "Shorelines" is a mostly straight path with few dead ends and corners.
4. **Map events** can happen inside a map.

### Out of scope

- **PD2-Resurrected does not move to Cartographer.** core-maps keeps its own map code. Cartographer has no content interface for other plugins and carries no PD2-specific content.
- **Erasing or rebuilding a map within a game.** The owner decided on one map per game (2026-11-11). Rebuilding works, as #930 showed. The owner ruled it out because of the risk of a stale automap after a layout change.
- **Cartographer and core-maps never load together.** Both hook item spell 16 and the same spawn functions.

## 1. What the research settled

- **The SDK cannot do it, and tables alone cannot do it.** The PluginSDK has no level, DRLG, portal or room service. Tables cannot shape a layout or steer the builder. Level work therefore uses native hooks. Every hook says in the code why no SDK service fits, and byte patches go through MutationService.
- **Indoor (maze) maps can be steered: yes** (#929, game-checked).
  - Cartographer replaces the room walk `0x3e96d0` for the slot level. D2R's own room placer and piece picker then follow the layout graph. Cartographer also steers the entrance and exit placer `0x3eb850`.
  - Tileset, room size and merge chance are fields of the level record, so no shared table record is written.
  - Tilesets that steer as-is: Cave, Crypt and Act 4 Lava.
  - Tilesets that need a no-op hook on the ring grower `0x3e9820`: Barracks, Jail, Lair, Durance, Dungeon, Act 3 Sewer, Ice, Harem, Basement, Spider and Temple.
  - Not yet steerable: Arcane, Catacombs, the tombs, Act 2 Sewer and Baal.
- **D2R's outdoor builders cannot build a map level: no** (#929, §3). Each one is hand-written for vanilla level ids that act start-up placed. D2RLoader's D2RCore also replaces the outdoor entry point. So outdoor maps need **Cartographer's own outdoor builder**, built on D2R's own pieces and primitives (separate plan).
- **A level is still rebuilt within a game.** When a level is empty, D2R unloads its rooms and rebuilds them on the next visit (#930). The builder therefore has to produce the same rooms in the same order every time in a game. Cartographer caches the solved layout per act seed and map level.

## 2. Opening a map

1. **Gates.** The map row sets difficulty, character level, quest and the town level (Harrogath by default). If a map was already opened this game, the open is refused and the player keeps the item. The refusal uses client message 0x15.
2. **Lay out the map.** Load the map row, roll the map seed and solve the layout from the shape rules (§3). If no solution is found within the retry budget, use the map's `fallback` preset.
3. **Fill the map level.** The map's level is a **slot level**: a levels.txt row in Act 5 with no links to other levels, one row per DRLG kind. Before the map is first built, Cartographer writes the tileset and builder parameters into that level's record. The level is built on the first portal entry, through Cartographer's build hook.
4. **Apply stats** (§4), then **roll an event** (§5).
5. **Open the portal.** A red portal pair runs from town to the map's entry room.
6. **Multiplayer.** Cartographer sends `{map id, seed}` to each client, and the client builds the same layout. The portal waits until each client confirms.

Each game keeps one map record: `{map, seed, slot, solved layout, item stats, monster/player/area lists, event state, portal pair}`. A unit counts as "in the map" when its level is the slot level.

## 3. Maps, tilesets and shape rules

`maps.txt` has one row per map. Its columns:

- `map`, `name`, `item`, `tier`, `arealevel`, and the gates.
- `drlg`: `maze`, `outdoor` or `preset`.
- `tileset`: the one tileset.
- `pieces`: piece tags.
- `monsters`: monster pool.
- `palette` and `sound`.
- `fallback`: the preset to use when no layout solves.
- **shape columns:**

| Column | Meaning | Shorelines example |
|---|---|---|
| `pathlen` | Rooms on the main path (minimum–maximum) | 14–18 |
| `straightness` | Chance, 0–100, that the path keeps its direction at each step | 85 |
| `maxturns` | Most corners on the main path | 4 |
| `branches`, `branchlen` | Dead ends (minimum–maximum), and the most rooms in each | 0–2, 2 |
| `loops` | Whether the path may rejoin itself | no |
| `bounds` | Largest size, in cells | 12×6 |
| `bossroom` | `end`, `farthest` or `none` | end |
| `required` | Pieces that must appear | entry, boss clearing |
| `eventslots` | Main-path rooms that may host an event | 3 |

Layouts are built in three steps:

1. **Layout graph.** A pure, seeded generator draws the graph from the shape columns. It needs no engine, has unit tests outside the game, and uses its own PCG/splitmix random numbers so host and client agree.
2. **Building it in the level:**
   - **maze:** the guided maze from #929.
   - **outdoor:** Cartographer's own builder, which stamps tileset pieces cell by cell (separate plan).
   - **preset:** one DS1, with no graph.
3. **Post-check and retry.** Check the result against the shape rules. On failure, retry with `seed_k = splitmix64(seed ^ k)` up to 32 times, then use `fallback`. All of this happens before any engine call.

When the plugin loads, it checks that every piece has HD presets.

## 4. Map stats

`mapstats.txt` routes each item stat to one of three lists:

- the **monster** list, applied as a state on every map monster when it spawns;
- the **player** list, added while the player is in the map;
- an **area** slot: density, area level, rarity, extra groups, extra boss, fortify, ethereal, socket and corrupt drops, death drops.

A row can also mark physical-as-extra, or name an item-drop effect.

This works like today's core-maps code (formulas `map_stats.cpp:309-382`, hooks `:826-843`), rewritten for Cartographer. Stat ids come from the table, never from C++.

## 5. Map events

`mapevents.txt` columns:

- `event`, `weight`;
- the chance by item quality, and an optional `forcestat`;
- `maps`;
- `kind`: `monster`, `superunique`, `object` or `portal`;
- `unit`, `count`, `repeats`;
- `progress`: the rooms-activated threshold;
- `spot`: for example an event-slot room;
- `notify`, `sound`.

The event state belongs to the game's map record.

## 6. Phases

Each phase gets one issue and one PR. Each needs a game check in Hell on a level 90+ character, and must pass the table-diff and unit gate. All test content ships in a small Cartographer test pack, kept apart from PD2's data.

| # | Phase | Work | Done when |
|---|---|---|---|
| 0 | Glossary + ADR | Add glossary entries for *slot level*, *layout graph* and *map record*. Write an ADR recording that Cartographer is standalone, that it never loads with core-maps, and that a game has one map | ADR merged |
| ✅ | Spikes | DRLG steering (#929) and level regeneration (#930) | Done |
| 1 | Skeleton | Add the `plugin/cartographer` target, standalone (no `pd2_engine.h`). Load and validate tables through CustomTableService, including the HD check. Add the slot rows, item spell, gates, the one-map-per-game refusal and the portal pair, with one `preset` map | A map opens; a second open is refused; leaving and returning shows the same map |
| 2 | Layout graph | Shape columns and the pure generator, with unit tests: path length, turns, branches, bounds, determinism by seed | 10k seeded graphs per test map meet their rules |
| 3 | Guided maze | Productise the #929 prototype: build hook, walk replacement, special-room steer, ring-grower no-op, parameter write-in, post-check, fallback, layout cache | A test maze map builds within its shape in 50 games, and is the same after an unload in one game |
| 4 | Own outdoor builder | See [cartographer-outdoor-builder.md](cartographer-outdoor-builder.md), phases S0–7, Act 3 jungle first | A "Shorelines" jungle map builds within its shape in 50 games |
| 5 | Map stats | `mapstats.txt`, the three lists, drop effects, player add and remove | Stats are right on monsters, players and drops |
| 6 | Map events | `mapevents.txt`, roll, progress, spawn kinds, notify | Each kind fires |
| 7 | Multiplayer | The `{map id, seed}` message, client build and confirm, kill credit, map-stat HUD | Two-client LAN check shows the same layout |
| 8 | Release | README for mod authors, an example pack, versioned table formats and a release zip | A mod with no PD2 data runs maps from tables alone |

Phases 2, 5 and 6 can run in parallel with 3 and 4.

## 7. Risks

- **D2RLoader updates.** D2RCore owns the outdoor entry and helpers. Mitigation: byte-check every hook, bypass D2RCore only for the slot level, and switch outdoor maps to `fallback` on any mismatch.
- **Cross-act tilesets in an Act 5 slot.** The client preloads tile files for the current act only. Act 5 Ice drew fine in Act 1, but other pairs are unchecked. Mitigation: a preload hook for the map's LevelType.
- **The same rooms after an unload.** The layout is cached and pure, and the stamping order is fixed.
- **Client desync.** The graph comes from the map seed only. The portal waits for each client's confirmation.
- **License.** D2RHUD-2.4 has no license, so no code is copied from it. All addresses come from our own research.

## 8. Decisions

| # | Decision | Status |
|---|---|---|
| D1 | Name: Cartographer | ✅ |
| D2 | One map per game (replaces "refuse until empty") | ✅ |
| D3 | Items left in a map: gone (now only at the end of the game) | ✅ |
| D4 | No content interface; fully standalone | ✅ |
| D5 | Our own outdoor builder is **in scope** (2026-11-11) | ✅ |
