# Plan: a standalone map system plugin "Cartographer"

Revised 2026-11-11. This version follows the owner's decisions and two research spikes:

- DRLG steering: [#929](https://github.com/synpoox/pd2-resurrected/pull/929), `docs/research/drlg-steering.md`
- Level regeneration: [#930](https://github.com/synpoox/pd2-resurrected/pull/930), `docs/research/level-regeneration.md`

The outdoor builder has its own plan: [cartographer-outdoor-builder.md](cartographer-outdoor-builder.md).

## Goal

Cartographer is a fully **standalone D2RLoader plugin** that any D2R mod can drop in.

1. **One map per game.** A map item used in Act 5 (Harrogath) opens a red portal into the game's map. Once a map is open, any further map is refused for the rest of the game.
2. **Map stats:** monster, area and player stats.
3. **Procedural layouts.** Each map has **one tileset** and a **shape**. Every game builds a new random layout from that tileset only, within that shape. For example, "Shorelines" is a jungle corridor: mostly straight, with few dead ends and corners.
4. **Map events** can happen inside a map.
5. **Extremely simple to use** (§3). Someone with little modding experience must be able to make a working map by editing one small table.

### Out of scope

- **Moving PD2-Resurrected onto Cartographer.** core-maps keeps its own map code. Cartographer has no content interface for other plugins and carries no PD2-specific content.
- **Erasing or rebuilding a map within a game** (owner, 2026-11-11). This works (#930), but it risks a stale automap.
- **Loading Cartographer and core-maps together.** They never load at the same time: both hook item spell 16 and the same spawn functions.

## 1. What the research settled

- **No SDK service, no table-only route.** The PluginSDK has no level, DRLG, portal or room service, and its data tables are read-only. Tables alone cannot shape a layout.
  - Level work therefore uses native hooks. Each hook says in its code why no SDK service fits.
  - Byte patches go through MutationService.
- **Indoor (maze) maps can be steered, confirmed in game (#929).**
  - Cartographer replaces the room walk `0x3e96d0` for the map level. D2R's own room placer and piece picker then follow the layout graph.
  - The entrance and exit placer `0x3eb850` is steered too.
  - Cave, Crypt and Act 4 Lava steer as-is.
  - With a no-op ring grower `0x3e9820`, these also steer: Barracks, Jail, Lair, Durance, Dungeon, Act 3 Sewer, Ice, Harem, Basement, Spider and Temple.
  - Not yet steerable: Arcane, Catacombs, the tombs, Act 2 Sewer and Baal.
- **D2R's own outdoor builders cannot build a map level (#929 §3).** Outdoor maps need Cartographer's own outdoor builder (separate plan), with the Act 3 jungle first.
- **D2R rebuilds an empty level on the next visit (#930).** The builder must therefore produce the same rooms in the same order every time within a game. The solved layout is cached per act seed and map level.

## 2. Opening a map

1. **Gates.** Defaults: Hell, Harrogath, and no map opened yet this game. A map row can change the difficulty or require a character level. A refusal keeps the item and tells the player why.
2. **Lay out the map.** Roll the map seed and solve the layout from the map's shape (§3). If no layout solves after 32 seeds, use a safe built-in layout for the tileset.
3. **Build.** The map uses one of Cartographer's own **slot levels**: Act 5 levels with no links. They ship with the plugin; mod authors never edit `levels.txt` (§3.4). The level is built on the first portal entry, through Cartographer's build hook.
4. **Roll stats** (§3.2) and **an event** (§3.3).
5. **Open the portal.** A red portal pair runs from town to the map's entry room.
6. **Multiplayer.** The host sends `{map, seed}`. Each client builds the same layout and confirms before the portal lets it in.

## 3. What a mod author touches

### 3.0 Rules for the author's side

- **One required file.** `maps.txt`, one row per map. Everything else is optional and has a sensible default.
- **Plain words only.** No ids, flags, bitmasks, hex or formulas. Tilesets, shapes and sizes are names from a short built-in list. Monsters, items and stats use the names modders already see in D2's own tables.
- **Blank means default.** A missing optional column or a blank cell takes the default. A map row needs only 3 filled cells.
- **No vanilla table edits.** Authors never edit levels.txt, lvlmaze.txt, lvlprest.txt or misc.txt for Cartographer (§3.4).
- **Plain-English errors.**
  - **Where they appear:** at load, every problem is written to `cartographer-report.txt`, and the in-game console command `cartographer check` shows them too. An example: "maps.txt row 3: tileset 'Jungel' is not known. Did you mean 'Jungle'?"
  - **What happens to a bad row:** it is skipped, and the other maps still work. A bad row never crashes the game.
- **Familiar format.** Tab-separated `.txt` with a header row, like D2's own tables, so authors can edit it in the same spreadsheet tools. Our own words (tileset, shape, size, target) ignore case. Names from D2's tables (monstats `Id`, item codes) must match exactly, and the check suggests the right spelling and case.

### 3.1 `maps.txt` (required)

| Column | Required | What to put | Example |
|---|---|---|---|
| `Name` | yes | The map's name, shown on entry | Shorelines |
| `Tileset` | yes | A name from the tileset list (§3.5) | Jungle |
| `Shape` | yes | `Corridor`, `Winding`, `Branching` or `Sprawl` | Corridor |
| `Size` | no | `Small`, `Medium` (default) or `Large` | Large |
| `Level` | no | Monster level in the map (default 85) | 90 |
| `Item` | no | Item code of the map item. Blank: the map can drop from Cartographer's own map item | `shm1` |
| `Boss` | no | A monstats `Id` placed at the end of the path. Blank: no boss | `fallenshaman` |
| `Monsters` | no | Up to 10 monstats `Id`s, comma-separated. Blank: the tileset's own monsters | `quillrat1,zombie3` |
| `Difficulty` | no | `Normal`, `Nightmare` or `Hell` (default) | |
| `MinCharLevel` | no | Character level needed (default 1) | 80 |

The smallest complete map:

```
Name	Tileset	Shape
Shorelines	Jungle	Corridor
```

**What the shapes mean.** The numbers behind each shape are built in; authors never see them.

| Shape | Feels like |
|---|---|
| `Corridor` | One long path, mostly straight, few dead ends |
| `Winding` | One long path with many turns, few dead ends |
| `Branching` | A main path with several side rooms and dead ends |
| `Sprawl` | Wide and open, with loops and many branches |

### 3.2 `mapstats.txt` (optional)

Each row is one possible map stat. When a map opens, every row rolls its `Chance`, and each row that hits applies a value between `Min` and `Max`.

| Column | What to put | Example |
|---|---|---|
| `Map` | A map `Name`, or `*` for every map | `*` |
| `Text` | What the player reads | Monsters have +20% life |
| `Target` | `Monsters`, `Players` or `Area` | Monsters |
| `Stat` | For Monsters and Players: an itemstatcost `Stat` name. For Area: a built-in effect name (`Density`, `MonsterLevel`, `ExtraBoss`, `Champions`, `Uniques`, `MagicFind`, `Experience`) | `maxhp%` |
| `Min`, `Max` | The value range | 10, 20 |
| `Chance` | Percent chance this stat appears (default 100) | 30 |

Rolled stats show in the entry message and on the map-stat panel.

**Advanced:** a mod that rolls its own affixes onto map items can leave this file out. Cartographer then reads the item's own stats and applies each one to its target. It sorts an item stat by its Target: player stats go to the player, `Area` effects to the area, and everything else to monsters.

### 3.3 `mapevents.txt` (optional)

| Column | What to put | Example |
|---|---|---|
| `Map` | A map `Name`, or `*` | `*` |
| `Chance` | Percent chance per opened map | 15 |
| `Spawn` | A monstats `Id`, superuniques `Name` or objects `Name` | `Bishibosh` |
| `Count` | How many (default 1) | 1 |
| `Message` | Text shown when the event starts | A dark presence stirs… |

At most one event is chosen per map. The event triggers partway along the main path, at a built-in event room.

### 3.4 What ships ready-made, so authors never edit it

- **The slot levels** (Act 5, unlinked) and **Cartographer's own map item**, which is usable and opens a random map from `maps.txt`.
  - **How they get into the game:** if D2RLoader can't add table rows from a plugin [V], the release ships a one-step installer.
  - **What the installer does:** it adds these rows to the mod's `levels.txt` and `misc.txt`. It is safe to run twice.
- **The tileset catalogue** of pieces with their open sides, sizes and roles. Our tool generates it, and it ships inside the plugin; authors only use the tileset names.
- **Shape and size presets.**
- **Expert overrides.** An optional `shapes.txt` lets an expert define a new shape with the numeric rules: path length, straightness, turns, branches, branch length, loops, bounds and boss room. The README covers it in one "Advanced" section, after everything else.

### 3.5 Tileset list (author-facing names)

Release 1 gives each tileset a plain name for its D2R LevelType:

- **Indoor:** `Cave`, `Crypt`, `Lava`, then `Barracks`, `Jail`, `Lair`, `Durance`, `Dungeon`, `Sewer`, `Ice`, `Harem`, `Basement`, `Spider` and `Temple` as each passes its game check.
- **Outdoor:** `Jungle` first, then `Snow`.

A tileset that isn't ready is refused at load, with a message that says so.

## 4. Map stats and events: how they work

- **Monster stats** are applied as a state on every map monster when it spawns.
- **Player stats** are added while the player is in the map.
- **Area effects** use region formulas from today's core-maps (`map_stats.cpp:309-382`), rewritten for Cartographer. Stat ids come from the table, never from C++.
- **Event state** lives in the game's map record.

## 5. Phases

Each phase gets one issue and a PR, plus a game check in Hell on a level 90+ character, and must pass the table-diff and unit gate. Test content lives in a small Cartographer example pack, apart from PD2's data.

| # | Phase | Work | Done when |
|---|---|---|---|
| 0 | Glossary + ADR | Glossary entries for *slot level*, *layout graph*, *map record*. An ADR records: standalone, never loaded with core-maps, one map per game, and the §3.0 simplicity rules as a release requirement | ADR merged |
| ✅ | Spikes | DRLG steering (#929), level regeneration (#930) | Done |
| 1 | Skeleton | `plugin/cartographer` target, standalone (no `pd2_engine.h`). Table loader with the §3.0 checks, `cartographer-report.txt` and `cartographer check`. Slot rows, the default map item and the installer (or D2RLoader row injection), gates, the one-map rule, the portal, and one built-in preset map | A 2-cell `maps.txt` opens a map; a typo gives a plain-English error; a second open is refused |
| 2 | Layout graph | The pure generator, the shape and size presets, and `shapes.txt`. Unit tests in a new C++ test harness | 10k seeds per preset meet their rules |
| 3 | Guided maze | Productise the #929 prototype: hooks, ring-grower no-op, special-room steer, cache, fallback | `Cave`/`Corridor` and `Crypt`/`Branching` build in shape in 50 games and stay the same after an unload |
| 4 | Own outdoor builder | [cartographer-outdoor-builder.md](cartographer-outdoor-builder.md), Act 3 jungle first | `Jungle`/`Corridor` (Shorelines) builds in shape in 50 games |
| 5 | Map stats | `mapstats.txt`, the item-stat mode, the three targets, area effects, the panel | Each target and each area effect is right in game |
| 6 | Map events | `mapevents.txt`, the event room, spawn kinds, message | Each spawn kind fires |
| 7 | Multiplayer | `{map, seed}` message, client build and confirm, kill credit | A two-client LAN check shows the same layout |
| 8 | Release | A one-page "Your first map in 5 minutes" README, a table reference (every column, default and example), the example pack, the installer, and a release zip | **Novice test:** a fresh agent with no project context, given only the zip and the README, adds a new map to a clean mod and opens it in game, without reading any code |

Phases 2, 5 and 6 can run alongside 3 and 4.

## 6. Risks

- **D2RLoader updates.** The hooks are byte-checked, D2RCore is bypassed only for the slot level, and outdoor maps switch to their safe layout on a mismatch.
- **Cross-act tilesets in an Act 5 slot level.** The client preloads tile files for the current act only. Act 5 Ice drew fine in Act 1; other pairs are unchecked. Mitigation: a preload hook for the map's LevelType.
- **Getting slot rows into the game** [V]. Prefer a D2RLoader row-injection route if one exists; otherwise use the installer.
- **Simplicity creep.** Every new column needs a default and a plain-English check. The phase 8 novice test gates the release.
- **License.** D2RHUD-2.4 has no license, so no code is copied. All addresses come from our own research.

## 7. Decisions

| # | Decision | Status |
|---|---|---|
| D1 | Name: Cartographer | ✅ |
| D2 | One map per game | ✅ |
| D3 | Items left in a map: gone at the end of the game | ✅ |
| D4 | No content interface; fully standalone | ✅ |
| D5 | Our own outdoor builder is in scope | ✅ |
| D6 | Author-facing tables are extremely simple (§3.0); the novice test gates the release (2026-11-11) | ✅ |
