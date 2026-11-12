# Plan: a standalone map system plugin ("MapKit", working name)

Revised 2026-11-11 to the owner's goal. It was written from origin/main 97489f6.

## Goal

MapKit is a **standalone D2RLoader plugin** that any D2R mod can drop in.

1. **Any number of maps per game, one live at a time.** Opening a new map completely erases the previous one.
2. **Map stats:** monster, area and player stats, rolled on the map item.
3. **Procedural layouts:** each map is generated fresh, within the constraints of a template and a tileset.
4. **Map events** can happen inside a map.

PD2-Resurrected is its first user. PD2's maps become MapKit templates and content. PD2's uber fights stay in core-maps; they use fixed levels, not maps.

## 1. What shapes the design

- **Today's limit is only a counter.** `map_open.cpp` refuses once the per-game counter `'MAPS'` is above 0 (MO:224). It also passes `max=1` to `OpenRedPortal`. All the other map state is game-wide, one slot each: stat lists, area writes, event progress. That suits "one live map", provided every open resets everything.
- **A level is generated once per game.** To open again, the level must be freed and rebuilt. That needs native hooks, because the SDK has no level, DRLG, portal or room service.
- **Rebuilding a level in a running game has been done.** locbones/D2RHUD-2.4 does it with these steps:
  1. Free the level's units and rooms.
  2. Set a new seed at `drlg+0x860` around a native `AllocateLevel` call.
  3. Keep the old level record as an unlinked tombstone until the act is freed.
  4. Clear the preset and superunique "spawned" bits, then repopulate.
  5. Send the seed to each client and wait for each to confirm.

  It has no license. We take the approach only, copy no code, and find every function in our own 93847 Ghidra project.
- **One live map makes erasing simple.** D2RHUD has to refuse when a level is in use. We don't: we warp everyone out, then erase. The owner accepts losing whatever is left inside.
- **Few level ids.** A single live map needs only a small set of reusable **slot levels**: about one per DRLG kind (maze, preset, outdoor) in the opening act. Each slot is re-parameterised at every open. This keeps us well under the 255-id limit, which a pool of map copies would break.
- **Layouts can use D2's own generator.** D2's maze DRLG already builds a level from template pieces: LvlMaze rows, plus LvlPrest pieces of one LevelType (tileset). Setting those parameters before `AllocateLevel`, and limiting which pieces it may pick, gives constrained procedural layouts with no new generator. Our own grid generator is an optional later stage.
- **HD.** D2R draws a generated level from the same preset pieces. So a template may only use pieces that have HD presets: D2R's own, or ours from ADR 0005's generated presets. MapKit checks this when it loads.
- **No plugin-to-plugin calls in the SDK.** The SDK has only `QueryService` for loader services; one plugin cannot register a service for others. For content plugins to hear MapKit's events, MapKit exports a small, versioned C interface, which they find by module name. This amends ADR 0004 ("plugins never call each other").

## 2. How it works

### Opening a map

The flow starts when a player uses a map item, through item spell slot 16 (configurable).

1. **Gates.** Check the difficulty, character level, quest and town. Each gate comes from the item's `maps.txt` row.
2. **Erase the live map, if there is one:**
   1. Warp every player inside it to town.
   2. Remove its portal pair.
   3. Free its units and rooms, keeping the old level record as a tombstone.
   4. Restore the slot's monster region from a saved base copy.
   5. Clear all map stats and event state.
3. **Generate the new map:**
   1. Choose a template from the item's pool.
   2. Roll a seed.
   3. Write the template's parameters into the slot level: LevelType, DRLG kind, size, rooms, merge, piece pool, Vis/warps, monster pool.
   4. Call `AllocateLevel`.
   5. Check the constraints (§3). If one fails, re-roll the seed, up to N tries, then fall back to the template's safe preset.
4. **Apply the stats.** Read the item's map stats into the live map's monster, player and area lists (§4). Then apply the area entries to the slot's region.
5. **Roll an event** (§5).
6. **Open a red portal pair** from town to the map's entry.
7. **Multiplayer.** Send `{template id, seed}` to each client. Each client rebuilds the slot level the same way and confirms before the portal lets it in.

### Live map state

The live map has one record per game: `{template, seed, slot, item stats, monster, player and area lists, event state, portal pair}`.

To test whether a unit is in a map, check that its level is the live slot or one of its extra levels. This replaces the hard-coded range 138–201.

### Plugin shape

- `mapkit.dll` is Shared and NativeHooks. It owns every native hook it needs: item spell check and use, monster spawned, player frame, area monster level, item spawn, preset spawn, room populate and room release.
- It uses the SDK wherever a service fits: MonsterDeath and DataTablesLoaded listeners, CustomTableService, Localization, and Mutations for byte patches.
- **Content interface** (exported C functions, versioned):
  - Subscribe to `MapOpened`, `MapErased`, `MonsterSpawned`, `MonsterDied`, `PresetSpawn`, `RoomActivated`, `EventStarted` and `PlayerEnteredMap`.
  - Query the live map: template, tier, stats and level ids.
  - Register custom event kinds.

  core-maps uses this interface for PD2's scripted map content: unique-map bosses, Zhar's seals, Horazon and the Altar guardian.

## 3. Templates and constraints

`templates.txt` has one row per layout family:

| Column | Meaning |
|---|---|
| `kind` | `maze` (D2 maze DRLG), `preset` (one fixed ds1 such as a unique map, with no generation), or `outdoor` (later; outdoor levels need the act rectangle and border links) |
| `leveltype` | Tileset (LevelType row) |
| `pieces` | Allowed LvlPrest pieces, as an explicit list or by tag. Every piece must have HD presets |
| `required` | Pieces that must appear: entry, boss room, event rooms |
| `rooms`, `size` | Minimum and maximum room count and size, rolled per open |
| `merge` | LvlMaze merge |
| `bossrule` | e.g. the boss room sits farthest from the entry by room path |
| `eventslots` | How many rooms may host an event |
| `monsters` | Monster pool for the slot level (mon1–25, umon), plus the area level base |
| `palette`, `sound` | Palette and level sound (MapKit owns the palette swap that core-ui does today) |
| `extralevels` | For multi-level maps: further templates linked by Vis/warps |

**Constraint checking** runs in two places:

- **While generating.** A hook on the maze piece picker limits which pieces may be chosen and forces the required ones.
- **After generating.** Walk the room graph and check `required`, `bossrule`, `rooms` and connectivity. If any check fails, re-roll the seed.

**Stage 2 (optional, later):** our own grid generator. It places ds1 pieces on a grid against a constraint graph, then feeds the rooms into D2's preset-room creation. Do it only if the maze generator's limits bite.

## 4. Map stats

`mapstats.txt` routes each item stat to one of three lists, replacing today's hard-coded `Routes`/`Extras`:

- **monster** list, applied as a state on every map monster at spawn;
- **player** list, added while the player is in the map;
- **area** slot: density, area level, rarity, extra groups, extra boss, fortify, ethereal, socket and corrupt drops, death drops, multi-level.

A row may also turn physical damage into extra damage, or point at an item-drop effect.

The application mechanism is today's code (MS:309-382, hooks MS:826-843) made generic. Two changes:

- The region writes start from the saved base copy each time, so stats never stack.
- Stat ids come from the table, never from C++.

PD2-only effects stay in core-maps as content subscribers: Desecration, corruption rows and Zhar's charms.

## 5. Map events

`mapevents.txt` columns:

| Column | Meaning |
|---|---|
| `event`, `weight` | Event name and draw weight |
| chance by item quality | Chance the map rolls an event at all |
| `forcestat` | A stat on the item that forces an event |
| `kind` | `monster`, `superunique`, `object`, `portal` (to another template), or `custom` (a content plugin handles it) |
| `unit`, `count`, `repeats` | What spawns and how often |
| `progress` | Rooms-activated threshold that triggers it |
| `spot` | Spot rule |
| `notify`, `sound` | Notify string and sound |

Event state is per live map and is cleared on erase. That fixes today's bugs where room progress and Horazon never reset.

PD2's events ship as rows: Mendeln, Dark Wanderer, Treasure Fallen, Gheed, Altar, Spire and Void/Tomb. Scripted parts, such as the Altar guardian AI and Spire stages, use `custom` events in core-maps.

## 6. Phases

Each phase has one issue and a PR. Each gets a game check in Hell on a level 90+ character, with the table-diff and unit gate. PD2's maps keep working, on the old code, until phase 8 switches them over.

| # | Phase | Work | Done when |
|---|---|---|---|
| 0 | Decisions + ADR | Settle D1–D5 (§8). Amend ADR 0004: standalone MapKit plus a content interface. Add glossary entries for *slot level*, *template*, *live map* | ADR merged |
| 1 | **Spike: erase and rebuild a level** (time-box ~2 days) | In 93847 Ghidra find: AllocateLevel, level free, act room free, the seed field, the level list, preset and superunique bits, and the client's level build. Test build with `cheat`-style `mapkit regen <level>`. Write `docs/research/level-regeneration.md` | One level rebuilt 20 times in one game, then leave and make a new game, with no crash and a new layout each time |
| 2 | **Spike: parameterising the DRLG** (time-box ~2 days) | Find where AllocateLevel reads LevelType, LvlMaze and LvlPrest, and whether per-open overrides work. Find the maze piece picker. Find what the client needs to rebuild the same layout. Add to the research doc | One slot level built three times with three tilesets and room counts |
| 3 | MapKit skeleton | New `plugin/mapkit` target, standalone. CustomTableService loading and validation (including the HD piece check). Slot-level rows shipped as data. Item spell, gates, the erase flow (warp out, portals, free, region restore) and the portal pair. One fixed template | Open five maps in a row in one game; each erases the last |
| 4 | Templates + constraints | `templates.txt`, parameter write-in, piece-picker constraint hook, post-checks with seed re-rolls, `preset` kind, multi-level maps, palette and sound | Each template generates within its constraints over 50 seeded opens (automated where possible) and in game |
| 5 | Map stats | `mapstats.txt`, three lists, region base copy, drop effects, player frame add/remove | Stats right on monsters, players and drops; no stacking after 5 opens |
| 6 | Map events | `mapevents.txt`, rolls, progress, spawn kinds, notify, per-map reset | Each built-in kind fires; a reopened map starts with no progress |
| 7 | Content interface | Exported C API, versioned, with off-safe fallbacks. Example content plugin | Example plugin's custom event fires |
| 8 | PD2 on MapKit | Tools emit PD2's maps as templates (each PD2 map's levels row becomes its template), stats and events. core-maps subscribes for scripted content. Delete core-maps' map code and keep the ubers | Table-diff parity; PD2 map game checks pass |
| 9 | Multiplayer | `{template, seed}` packet, client rebuild and confirm, per-map kill credit, map stat HUD (packet 0x55 or a panel) | Two-client LAN check |
| 10 | Release | README for mod authors, example pack (one tileset, two templates, three stats, two events), versioned tables | A non-PD2 test mod runs maps from tables only |

Phases 1 and 2 are the risk; they decide whether the rest holds, so run them first and in parallel. Phases 3–7 build the new plugin beside core-maps, so PD2 maps keep working on the old code throughout.

## 7. Risks

- **Crashes after rebuilding.** Native code caches pointers to levels and rooms. Mitigations: the tombstone rule, and only erasing after everyone has been warped out. Phase 1's stress check covers this.
- **Template overrides leaking.** Writing parameters into shared table records also affects the client in single player, since it is the same process, and every reader of those rows. Restore the records after `AllocateLevel`, or point the slot at a private record copy; phase 2 decides which.
- **Client desync.** A wrong seed or wrong parameters put walls in the wrong place. The portal waits for each client's confirmation.
- **Thin maze generator.** D2's maze DRLG may not express every constraint. If so, rely on post-checks and re-rolls, then stage 2's own generator.
- **License.** D2RHUD-2.4 has no license, so no code is copied. All addresses come from our own research.

## 8. Decisions for the owner

- **D1 Name:** "MapKit" or another name without PD2 branding (ADR 0004, 2026-11-03 amendment).
- **D2 Players inside the old map when a new one opens:** warp them to town (recommended), or refuse the open until the map is empty.
- **D3 Ground items in the erased map:** lost (simplest, and matches "completely erased"), or moved to the opener's feet in town.
- **D4 Content interface:** amend ADR 0004 so content plugins can call MapKit's exported C API (recommended; PD2's scripted map content needs it), or keep MapKit data-only and drop scripted map content.
- **D5 Stage 2 own generator:** out of scope until the maze generator falls short (recommended), or plan it now.
