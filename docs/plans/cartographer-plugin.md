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
  - **Where they appear:** at load, every problem is written to `cartographer-report.txt`, and the in-game console command `cartographer check` shows them too. `cartographer open <Name>` opens a map's portal on the spot, for testing, without needing the item. An example: "maps.txt row 3: tileset 'Jungel' is not known. Did you mean 'Jungle'?"
  - **What happens to a bad row:** it is skipped, and the other maps still work. A bad row never crashes the game.
- **Familiar format.** Tab-separated `.txt` with a header row, like D2's own tables, so authors can edit it in the same spreadsheet tools. Our own words (tileset, shape, size, target) ignore case. Names from D2's tables (monstats `Id`, item codes) must match exactly, and the check suggests the right spelling and case.

### 3.1 `maps.txt` (required)

| Column | Required | What to put | Example |
|---|---|---|---|
| `Name` | yes | The map's name, shown on entry | Shorelines |
| `Tileset` | yes | A name from the tileset list (§3.5) | Jungle |
| `Shape` | yes | `Corridor`, `Winding`, `Branching`, `Loop` or `Sprawl` | Corridor |
| `Size` | no | `Small`, `Medium` (default) or `Large` | Large |
| `Level` | no | Monster level in the map (default 85) | 90 |
| `Item` | no | Item code of the map item. Blank: the map can drop from Cartographer's own map item | `shm1` |
| `Boss` | no | A monstats `Id` placed at the end of the path. Blank: no boss | `fallenshaman` |
| `Monsters` | no | Up to 10 monstats `Id`s, comma-separated. Blank: the tileset's own monsters | `quillrat1,zombie3` |
| `Difficulty` | no | `Normal`, `Nightmare` or `Hell` (default) | |
| `MinCharLevel` | no | Character level needed (default 1) | 80 |
| `Properties` | no | How many random properties this map gets (§3.2). Blank: the `cartographer.txt` default | 4 |
| `EventChance` | no | Percent chance of a map event (§3.3). Blank: the `cartographer.txt` default | 25 |
| `Density`, `Champions`, `Uniques` | no | Area: percent more monsters, champion packs and unique packs than the tileset's normal (default 0) | 50 |

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
| `Loop` | The path runs in a ring and comes back to where it started, with a few short side rooms |
| `Sprawl` | Wide and open, with loops and many branches |

### 3.2 `mapstats.txt` (optional): the property pool

This file is **one shared pool of random properties**. Every map that opens draws `Properties` of them: the per-map value, or else the default in `cartographer.txt` (6). Each property affects either monsters or players. A property can't be drawn twice in one map.

| Column | What to put | Example |
|---|---|---|
| `Target` | `Monsters` or `Players` | Monsters |
| `Stat` | An itemstatcost `Stat` name | `maxhp%` |
| `Min`, `Max` | The rolled value range | 10, 20 |
| `Weight` | How often it's drawn compared with the others (default 1) | 3 |

There is no text column. Each property's text comes from the game's own stat description in itemstatcost, the same text players see on items. It shows in the entry message and on the map-stat panel.

Area effects are fixed per map, not random. They are the `Density`, `Champions` and `Uniques` columns in `maps.txt`.

If the file is missing, Cartographer uses a small built-in pool, for example monster life, monster damage, monster resistances, player resistances, player life regen and player curse effects.

**Advanced:** a mod that rolls its own affixes onto map items can set `ItemStats=1` in `cartographer.txt`. The item's own stats are then used in place of a draw.

### 3.3 `mapevents.txt` (optional): what events exist

Each map has `EventChance` percent chance of one event: the per-map value, or else the default in `cartographer.txt`. This file defines what those events are. When a map rolls an event, one row is drawn by `Weight`.

**What is hardcoded and what is data:**
- **The event kinds are built into the plugin.** There are five, all generic, so any mod can use them.
- **Everything about an event is data:** what spawns, how many, the drops, the message.
- **What a table can't express:** scripted events with puzzles, stages or custom AI, like PD2's Altar of the Catalyst or Spire. These would need code, which a standalone plugin with no content interface doesn't take (D4).

| Kind | What happens |
|---|---|
| `Boss` | A boss (a monstats `Id` or superuniques `Name`) with `Count` minions waits in the event room |
| `Ambush` | When a player reaches the event room, `Waves` waves of `Count` × `Spawn` attack |
| `Horde` | The event room is packed with `Count` × `Spawn` |
| `Treasure` | A treasure monster flees and drops from `Drop` when killed. Or, if `Spawn` is an object, a chest is placed that drops from `Drop` |
| `Shrine` | An object (shrine, well, chest) is placed in the event room |

| Column | What to put | Example |
|---|---|---|
| `Name` | Shown to the player when the event starts | The Drowned King |
| `Kind` | One of the kinds above | Boss |
| `Spawn` | A monstats `Id`, superuniques `Name` or objects `Name` | `Bishibosh` |
| `Count` | How many (minions, horde size or wave size; default 1) | 6 |
| `Waves` | Ambush only (default 3) | |
| `Drop` | A treasureclassex name, for Treasure, or extra loot for Boss | `Act 5 (H) Super C` |
| `Message` | Optional extra text when it starts | The water churns… |
| `Weight` | How often it's drawn (default 1) | 2 |

The event room sits partway along the main path, and the layout graph reserves it. With no `mapevents.txt` there are no events.

### 3.3a `cartographer.txt` (optional): global settings

Two columns, `Setting` and `Value`. Every setting has a default.

| Setting | Meaning | Default |
|---|---|---|
| `Properties` | Random properties per map | 6 |
| `EventChance` | Percent chance of an event per map | 20 |
| `ItemStats` | 1 = use the map item's own stats in place of a draw | 0 |
| `Town` | The level the portal opens from | Harrogath |

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

**The tileset decides the builder.** Authors never choose indoor or outdoor. Each tileset name is tagged maze or outdoor in the shipped catalogue, so `Cave` builds a maze and `Jungle` builds outdoor. A private piece set (option C in §3.6) uses its `Base` tileset's builder.

### 3.5a Worked example: two maps in five minutes

A mod author wants two maps:

- **Shorelines:** an outdoor jungle map that is basically a straight line.
- **Tunneling Depths:** an indoor cave map that loops.

**1. Install.** Copy `Cartographer.dll` into D2RLoader's plugins folder. If the release has the installer (§3.4), run it once.

**2. Write the maps.** Create `maps.txt` in the mod's `data/global/excel/d2rloader/cartographer/` folder. It is tab-separated, so spreadsheet tools and D2's table editors open it:

```
Name	Tileset	Shape
Shorelines	Jungle	Corridor
Tunneling Depths	Cave	Loop
```

That is all. Both maps get the defaults:

- size Medium and monster level 85;
- 6 random properties from the built-in pool, and a 20% chance of an event (none until `mapevents.txt` exists);
- opened in Harrogath in Hell;
- they drop as Cartographer's own map item, which opens one of the two at random.

**3. Test.** Start a Hell game and go to Harrogath. Type `cartographer check` in the console; it should report "2 maps loaded, no problems". Then type `cartographer open Shorelines` and walk in. Every new game gives a new straight jungle path, and `Tunneling Depths` a new cave ring.

**4. Optional tuning.** Add only the columns you want:

```
Name	Tileset	Shape	Size	Level	Boss
Shorelines	Jungle	Corridor	Large	90	Bishibosh
Tunneling Depths	Cave	Loop	Small		quillrat5
```

A blank cell keeps the default; Tunneling Depths keeps level 85 here.

**Availability.** `Cave` works from phase 3, and `Jungle` from phase 4, the outdoor builder.

### 3.6 Maze maps and the author's own pieces

**What Cartographer reads.** For a maze map it reads D2's tables and writes to none of them.
- **The tileset's 15 room pieces.** D2 picks a maze room's piece as *tileset base + exit mask*: LvlPrest `Def` base+1 … base+15 are `<tileset> W, E, EW, S, … NSEW` (#929 §2.3). The base per tileset is hardcoded in D2's piece picker `0x3eb960`, not stored in a table.
- **The tile files** from LvlTypes.
- **The room size and merge chance** from the LvlMaze row of a vanilla level of that tileset.

**What Cartographer writes.** Its choices go only into the slot level's own record: tileset, room size, merge chance and the graph walk. No shared table record is written, so vanilla levels are never affected.

**The author's options, from easiest to hardest:**

| Option | What the author does | Effect | Who it's for |
|---|---|---|---|
| **A. Pick a built-in tileset** | `Tileset=Cave` in `maps.txt`. Nothing else | Vanilla Cave rooms, Cartographer's layout | Everyone (the default) |
| **B. More variety in a tileset** | Add more DS1 files to that tileset's piece rows (LvlPrest `File1`–`File6`). D2 already rotates through a piece's files | New-looking rooms in Cartographer maps, **and also in vanilla levels of that tileset** | Modders who already edit DS1s |
| **C. Their own piece set** (Advanced) | Add 15 LvlPrest rows (their own DS1s, one per exit mask, same room size and tiles as a built-in tileset). Add one row to an optional `tilesets.txt`: `Name`, `Base` (the built-in tileset), `First` (the LvlPrest `Def` of the "W" piece). Then use `Tileset=<Name>` in `maps.txt` | Rooms used **only** by Cartographer maps; vanilla levels are untouched | Experienced modders |

**How option C works in the plugin.** For the slot level, a hook on the piece picker `0x3eb960` returns `First + mask − 1` in place of D2's hardcoded base. This is the same hook family as the maze steering, scoped to the slot.

**What C can't do.** The pieces must share the base tileset's room size and its LvlTypes tile files. A brand-new LevelType with new DT1s is out of scope for release 1, because cross-act tile preload is still unverified (§6).

**What the load check verifies for option C:** all 15 rows exist, the DS1 files exist, every piece has the same size, and the HD presets exist.

**Special rooms.** Maze maps need no level exits, so the vanilla Prev, Next and Down rooms are never placed. The entrance is the portal room at the start of the path. The end of the path holds the boss and the reserved event room, built from ordinary mask pieces.

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
| 3 | Guided maze | Productise the #929 prototype: hooks, ring-grower no-op, special-room steer, cache, fallback, and the option C piece-set hook with `tilesets.txt` (§3.6) | `Cave`/`Corridor` and `Crypt`/`Branching` build in shape in 50 games and stay the same after an unload |
| 4 | Own outdoor builder | [cartographer-outdoor-builder.md](cartographer-outdoor-builder.md), Act 3 jungle first | `Jungle`/`Corridor` (Shorelines) builds in shape in 50 games |
| 5 | Map stats | `mapstats.txt` pool and draw, `cartographer.txt`, the `ItemStats` mode, area columns, the panel | N properties drawn per map, each showing its game text; area columns right in game |
| 6 | Map events | `mapevents.txt`, `EventChance`, the five kinds, the event room, message | Each kind fires in game |
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
