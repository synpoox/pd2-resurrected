# Cartographer table reference

> **Draft.** Features land in later phases. See [README.md](README.md) for a walk-through.

All tables are tab-separated `.txt` files with a header row. They live in the mod's `data/global/excel/d2rloader/cartographer/` folder.

- Words that are Cartographer's own (tileset, shape, size, target, kind) ignore case.
- Names from the game's own tables (monster `Id`, item codes, stat names) must match exactly. The check suggests the right spelling.
- A blank cell or a missing optional column takes the default.

## maps.txt (required)

One row per map.

| Column | Required | What to put | Default | Example |
|---|---|---|---|---|
| `Name` | yes | The map's name, shown on entry | none | Shorelines |
| `Tileset` | yes | A tileset name (see README) | none | Jungle |
| `Shape` | yes | `Corridor`, `Winding`, `Branching`, `Loop`, `Sprawl`, or a name from `shapes.txt` | none | Corridor |
| `Size` | no | `Small`, `Medium` or `Large` | Medium | Large |
| `Level` | no | Monster level in the map | 85 | 90 |
| `Item` | no | Item code of the map item | Cartographer's own map item | `shm1` |
| `Boss` | no | A monster `Id` placed at the end of the path (case must match) | no boss | `fallenshaman1` |
| `Monsters` | no | Up to 10 monster `Id`s, separated by commas | the tileset's own monsters | `quillrat1,zombie3` |
| `Difficulty` | no | `Normal`, `Nightmare` or `Hell` | Hell | Nightmare |
| `MinCharLevel` | no | Character level needed | 1 | 80 |
| `Properties` | no | Random properties for this map | the `cartographer.txt` value (6) | 4 |
| `EventChance` | no | Percent chance of an event | the `cartographer.txt` value (20) | 25 |
| `Density` | no | Percent more monsters than the tileset's normal | 0 | 50 |
| `Champions` | no | Percent more champion packs | 0 | 50 |
| `Uniques` | no | Percent more unique packs | 0 | 25 |

Smallest map: `Name`, `Tileset` and `Shape` filled in.

**Open question (owner call pending):** the examples use `Bishibosh`, a unique monster, but `Boss` takes a monster `Id`. `Boss` may need to accept unique monster names too.

## mapstats.txt (optional)

One shared pool of random properties. Each map draws `Properties` of them. A property is never drawn twice in one map. If the file is missing, a small built-in pool is used.

There is no text column. The text comes from the game's own stat description.

| Column | What to put | Default | Example |
|---|---|---|---|
| `Target` | `Monsters` or `Players` | none (required) | Monsters |
| `Stat` | A stat name from the game's stat table | none (required) | `item_maxhp_percent` |
| `Min` | Lowest rolled value | none (required) | 10 |
| `Max` | Highest rolled value | none (required) | 20 |
| `Weight` | How often it is drawn compared with the others | 1 | 3 |

## mapevents.txt (optional)

What events exist. A map has `EventChance` percent chance of one event. One row is drawn by `Weight`. Without this file there are no events.

| Column | What to put | Default | Example |
|---|---|---|---|
| `Name` | Shown to the player when the event starts | none (required) | The Drowned King |
| `Kind` | `Boss`, `Ambush`, `Horde`, `Treasure` or `Shrine` | none (required) | Boss |
| `Spawn` | A monster `Id`, a unique monster name or an object name | none (required) | `Bishibosh` |
| `Count` | Minions (Boss), wave size (Ambush) or horde size (Horde) | 1 | 6 |
| `Waves` | Number of waves (Ambush only) | 3 | 4 |
| `Drop` | A treasure class name. For Treasure, the drop. For Boss, extra loot | none | Act 5 (H) Super C |
| `Message` | Extra text shown when the event starts | none | The water churns... |
| `Weight` | How often it is drawn compared with the others | 1 | 2 |

The kinds:

| Kind | What happens |
|---|---|
| `Boss` | A boss waits in the event room with `Count` minions |
| `Ambush` | When a player reaches the event room, `Waves` waves of `Count` of `Spawn` attack |
| `Horde` | The event room is packed with `Count` of `Spawn` |
| `Treasure` | A treasure monster flees and drops from `Drop` when killed. If `Spawn` is an object, a chest that drops from `Drop` is placed |
| `Shrine` | An object (shrine, well or chest) is placed in the event room |

## cartographer.txt (optional)

Global settings. Two columns, `Setting` and `Value`.

| Setting | Meaning | Default | Example |
|---|---|---|---|
| `Properties` | Random properties per map | 6 | 4 |
| `EventChance` | Percent chance of an event per map | 20 | 30 |
| `ItemStats` | 1 means a map item's own stats are used in place of a draw | 0 | 1 |
| `Town` | The town the portal opens from | Harrogath | Harrogath |

## tilesets.txt (optional, advanced)

Adds your own room-piece set to a built-in tileset. Only for maze tilesets. Your rooms are used only by Cartographer maps.

| Column | What to put | Default | Example |
|---|---|---|---|
| `Name` | The name to use in `maps.txt` | none (required) | Mossy Cave |
| `Base` | The built-in tileset it copies | none (required) | Cave |
| `First` | The `Def` value of the "W" piece in the game's room piece table (`LvlPrest`); the other 14 follow it | none (required) | 1200 (illustrative) |

## shapes.txt (optional, advanced)

Adds a new shape defined with numbers. Use its name in the `Shape` column.

The plan names these rules: path length, straightness, turns, branches, branch length, loops, bounds and boss room. The exact column names are not set yet.

| Column | What to put | Default | Example |
|---|---|---|---|
| `Name` | The name to use in `maps.txt` | none (required) | Snake |
| path length, straightness, turns, branches, branch length, loops, bounds, boss room | Numbers for each rule | to be set in phase 2 | to be set in phase 2 |
