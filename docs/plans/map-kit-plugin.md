# Plan: a reusable map system plugin ("MapKit", working name)

Written 2026-11-11 from origin/main 97489f6. The goal is a D2RLoader plugin that any D2R mod can use to add endgame maps. It must:

1. Open a portal from a map item into a custom level, with **no limit per game**.
2. Give that level area, monster and player map stats.
3. Run map events.

PD2-Resurrected becomes its first user: PD2's maps, uber fights and unique-map bosses stay ours, built on top of it.

## 1. What we found

### Our code today (`plugin/core-maps`)

- **"One per game" is only a counter.** `map_open.cpp` refuses once the per-game counter `'MAPS'` is above 0 (`Refusal`, MO:224). It also passes `max=1` to D2R's `OpenRedPortal`. Nothing in the DRLG enforces it.
- **Removing the gate would break things.** All map state is game-wide, one slot each:
  - **Stats.** The stat lists are counters `'M'|list|entry|field`. A second map overwrites the first map's stats.
  - **Area writes.** `ApplyArea` changes the monster region in place, so a second map stacks density and groups on top of the first.
  - **Events.** The event roll (`EventKey`, `SpawnsKey`), room progress and Horazon are single slots that are never reset.
  - **"Is this a map?"** The check is a hard-coded level range, 138–201.
  - **Level layout.** A level is generated once per game, so reopening a level gives the same layout, and monsters already killed stay dead.
- **PD2 content is hard-coded in C++.** This covers routes, stat ids, level ids, monster and object ids, events, gates, TCs and uber references. There are no data tables. The full list is in the internals study, section 4 (summarised in §4 below).
- **The SDK has no level, portal, DRLG or room service.** Item spells, operate slots, room populate, preset spawn, monster spawn and the player frame are all native hooks. The SDK is used only for the monster-death listener, DataTablesLoaded, Localization and Mutations. So level work must use native hooks, which the "SDK before native hooks" rule allows when the code says why.
- **Coupling.** `key_drops` (death), `map_stats` (spawn, item and preset hooks), `map_open` (item spell and portal) and `map_events` (room populate) call every PD2 fight by name. Issue #600 already proposes the event seam needed to fix this.

### D2R Reimagined does not have unlimited maps

Its wiki says each theme and tier combination runs once per game, and a second map does not reset a map already visited (`maps/overview.md` and `maps/crafting.md` in github.com/D2R-Reimagined/wiki-content). That gives 60 distinct maps per game, several of them open at once. It looks like one level instance per theme and tier, with no regeneration. The plugin is closed source.

### Unlimited maps have been done elsewhere: locbones/D2RHUD-2.4

D2RHUD-2.4 regenerates an existing level id in the same game with a new seed. Its "Cube maps" code is in `CubeMapLayouts.inl` and `CubeMap.cpp`. It has **no license**, so we read it for the approach only, never copy it, and find every function in our own 93847 Ghidra project. Its steps:

1. Refuse if any room has a player, a corpse or a client-cached room.
2. Move ground items to town, then remove the units and free the rooms.
3. Set a new seed at `drlg+0x860` around a native `AllocateLevel(drlg, id)` call.
4. Keep the old level record as an unlinked "tombstone" until the act is freed, because native code still holds pointers to it.
5. Send the seed to each client, which rebuilds its own copy. The portal refuses entry until every client acknowledges.
6. Clear the room-preset "spawned" bits and the superunique-spawned bits (`game+0x5670`), then run population again.

### Why a pool of duplicate levels won't do

The level id limit is 255, and PD2's levels already reach about 201 with about 60 map levels. Giving each map level even one more copy goes over the limit. **Regenerating the level is the only route to unlimited maps** for a mod of PD2's size. A pool is still useful for a small mod (§3, Backend A).

## 2. Architecture

```
mapkit (static library, generic, data-driven)
  ├─ Instance store: per-instance state, replacing game-wide counters
  ├─ Opener: item spell → gates → instance → portal pair
  ├─ Instance backend: Regenerate (unlimited) | Pool (fixed copies)
  ├─ Stats: routing table → per-instance m/p/area lists → apply hooks
  ├─ Events: event table → roll → progress → spawn
  └─ Event bus: MonsterSpawned, PresetSpawn, RoomPopulate, MonsterDeath,
                ItemSpell, PortalOperate, PlayerFrame, ItemSpawn, InstanceOpened/Closed
         │
         ├── mapkit.dll   = library + a thin plugin.cpp; content comes from tables only (for any mod)
         └── core-maps.dll = library + PD2 content: uber fights, unique maps, Zhar, Horazon …
```

**One DLL, or one library linked twice.** ADR 0004 says plugins never call each other, and core-maps' fights need the library's events in the same process. So MapKit is a static library:

- `mapkit.dll` is that library plus a 30-line shell, for other mods.
- `core-maps.dll` links the same library and adds PD2's fights as event subscribers.

Only one of the two is loaded at a time; core-maps replaces mapkit. This needs an amendment to ADR 0004.

**Content as data.** All content is read through the SDK's CustomTableService from `data/global/excel/d2rloader/mapkit/*.txt`, the same pattern as `core-ui/automap.cpp`. Our merge tools emit PD2's rows. Another mod writes its own rows.

| Table | Rows say |
|---|---|
| `maps.txt` | Item code or misc `len`, entry level, extra levels (multi-level maps), tier, room count for progress, backend, gates (difficulty, character level, quest, town level), portal object |
| `mapstats.txt` | Stat id → list (monster / player / area slot), physical-as-extra, item-drop effects (ethereal, sockets, corrupt) |
| `mapevents.txt` | Event key, weight, chance by item quality, kind (monster / superunique / object / portal), unit id, count, repeats, progress threshold, notify string, sound |
| `mapkit.txt` | Global settings: item spell slot, refusal message, item level rules, debug overrides |

The PD2-only stats (Desecration, corruption rows, Zhar's charms) stay as core-maps subscribers. They are not table features.

## 3. Unlimited maps: the instance model

- **Instance.** An instance holds `{id, level ids, seed, owner, stat lists, area slots, event state, portal pair, state}`, where state is Open, Occupied, Empty or Closed. Its lifetime is from opening the map to closing it. It replaces every `'MAPS'`, `'M…'`, `EventKey`, `SpawnsKey` and `RoomsKey` counter.
- **"Is this unit in a map?"** Look up the unit's level in the live-instance index. This replaces the 138–201 range.
- **Opening a map:**
  - If no live instance uses the level, start one.
  - If the instance using the level is empty (no players, corpses or cached rooms), close it: remove its portal pair, regenerate the level, then start a new one.
  - If it is occupied, refuse with a message such as "a map of this area is still in use". **(Owner decision D2; see §6.)**
  - Different levels run side by side, so there is no total cap.
- **Backend B, Regenerate (the target).** Do steps 1–6 of the D2RHUD approach in §1, each from our own Ghidra work on 93847. On every open, reset the monster region from a saved base copy, so area stats never stack.
- **Backend A, Pool (fallback and small mods).** Use N level rows per map and pick a free copy. When all N are used, refuse. No engine work is needed. This is also the safety net if regeneration proves unstable in multiplayer.

## 4. Phases

Each phase is one issue with its own PR, and a game check in Hell on a level 90+ character unless it is docs only. PD2 behaviour must not change until phase 5. The table-diff and unit gate applies throughout.

| # | Phase | Work | Done when |
|---|---|---|---|
| 0 | Decisions + ADR | Settle D1–D4 (§6). Amend ADR 0004 (library + two shells). Add a glossary entry for *map instance* | ADR merged |
| 1 | **Regeneration spike** (riskiest; time-box ~2 days) | In 93847 Ghidra, find AllocateLevel, level free, act room free, the seed field, the level list, superunique bits, and the client's level build. Prototype `cheat` commands `regen <level>` and `mapkit regen` in a test build. Write `docs/research/level-regeneration.md` | A level regenerates twice in one game in single player with a new layout and fresh monsters, without crashing on leaving the game. Otherwise fall back to Backend A and say so |
| 2 | Event bus | One dispatcher per native hook. `map_stats`, `map_open`, `key_drops` and `map_events` publish events, and the fights subscribe (#600's seam). Counter keys go into one registry header | Same behaviour; no hub file names a fight |
| 3 | Instance store | Move all map state from game-wide counters to instances. The area test uses the instance index. Save and restore the monster region per instance. The limit stays at 1 | Same behaviour; unit tests for the store |
| 4 | Data-driven content | Add CustomTableService loading. Add `maps`, `mapstats` and `mapevents` tables. Extend `tools/` to emit PD2's rows. Delete the hard-coded `Routes`, `MultiLevels`, `TieredLevels`, `Draws`, gates and level range | PD2 rows give the same table-diff and game behaviour |
| 5 | **Unlimited** | Backend B behind the instance store, with portal pair removal and region reset on reopen. Remove the `'MAPS'` gate and `max=1` | Open the same map 5 times in one game, with a new layout and fresh stats each time, plus two different maps open together |
| 6 | Multiplayer | Seed sync packet and client acknowledgement before portal entry. Per-instance kill credit (replacing "first player"). Map stat HUD (packet 0x55 or a core-ui panel) | Two-client LAN check |
| 7 | Library split + `mapkit.dll` | CMake target `mapkit` (static) plus a shell. core-maps links it. Ship an example content pack: one custom map, two stats, one event. Write a README for mod authors | A non-PD2 test mod opens its map from tables only |

Phases 2 and 3 are refactors with no behaviour change and can follow each other quickly. Phase 1 can run alongside them, because it touches no shipped code.

## 5. Risks

- **Crashes after regeneration.** Native code caches level and room pointers. Mitigations: the tombstone rule, and freeing only empty levels. Run a stress check: 20 regenerations, then leave the game and make a new one.
- **Client desync.** Each client builds the level itself. A wrong seed means walls in the wrong place. The portal refuses entry until the client acknowledges.
- **Level-type quirks.** Outdoor levels need the act-placed rectangle and border links carried over, and the Act 3 jungle needs its template. PD2 maps are mostly preset or maze levels, so check which DRLG type each map uses during phase 1.
- **Quest and superunique state.** Clear only the bits of the instance's own level. Never touch quest flags.
- **License.** D2RHUD-2.4 has no license, so we copy no code from it. All addresses come from our own Ghidra work.

## 6. Decisions for the owner

- **D1 Name:** "MapKit" or another name. It must not carry PD2 branding (ADR 0004, 2026-11-03 amendment).
- **D2 Reopening an occupied level:** refuse (recommended), or fall back to a pool copy when the mod ships one.
- **D3 Where state lives when a player leaves:** the instance stays until the level is empty and a new map replaces it (recommended), or it closes as soon as the portal owner leaves.
- **D4 Release scope:** the library split (phase 7) ships before or after unlimited maps (phase 5). Recommended: after, so PD2 players get unlimited maps first.
