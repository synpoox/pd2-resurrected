# Cartographer: your first map in 5 minutes

> **Draft.** Cartographer is being built in phases. Most of what is described here does not work yet. The words and columns are the plan. Each feature is marked with the phase it lands in.

Cartographer adds random maps to your mod. A player uses a map item in Harrogath. A red portal opens. Behind it is a new random level. Every game builds a new one.

Each game opens **one map**. After that, more maps are refused until the next game. A refused map item is kept, and the player is told why.

You make a map by adding one line to one small table.

## What you need

- Cartographer's plugin file, in D2RLoader's plugins folder.
- A spreadsheet tool, or any editor that handles tab-separated text.

If the release comes with an installer, run it once. It is safe to run twice.

## Step 1: write `maps.txt`

Make a file called `maps.txt` in your mod's `data/global/excel/d2rloader/cartographer/` folder.

It is a tab-separated table with a header row, like the game's own tables. Put this in it:

```
Name	Tileset	Shape
Shorelines	Jungle	Corridor
Tunneling Depths	Cave	Loop
```

That is two maps. Three cells each is all a map needs.

- **Name** is what the player sees when they enter.
- **Tileset** is the kind of place. `Jungle` is outdoors. `Cave` is a maze of rooms.
- **Shape** is how the path feels.

You do not pick indoor or outdoor. The tileset does that for you.

## Step 2: test it

1. Start a Hell game and go to Harrogath.
2. Open the console and type `cartographer check`. It should say "2 maps loaded, no problems".
3. Type `cartographer open Shorelines`. A portal opens. Walk in.

You get a new, nearly straight jungle path. `Tunneling Depths` gives you a new cave that loops back on itself. Start another game and you get different ones.

If something is wrong, `cartographer check` tells you in plain words. For example: "maps.txt row 3: tileset 'Jungel' is not known. Did you mean 'Jungle'?" The same messages are in `cartographer-report.txt`. A bad row is skipped. The other maps still work.

Cave maps come first (phase 3). Jungle maps need the outdoor builder (phase 4).

## What you get without trying

Leave a cell blank, or leave a column out, and you get the default:

| Setting | Default |
|---|---|
| Size | Medium |
| Monster level | 85 |
| Random properties | 6 |
| Chance of an event | 20% |
| Difficulty | Hell |
| Opens from | Harrogath |
| Map item | Cartographer's own map item, which opens one of your maps at random |

## Step 3: tune it (optional)

Add only the columns you care about:

```
Name	Tileset	Shape	Size	Level	Boss
Shorelines	Jungle	Corridor	Large	90	Bishibosh
Tunneling Depths	Cave	Loop	Small		quillrat5
```

The blank cell under `Level` for Tunneling Depths keeps level 85.

### The shapes

| Shape | Feels like |
|---|---|
| `Corridor` | One long path, mostly straight, few dead ends |
| `Winding` | One long path with many turns, few dead ends |
| `Branching` | A main path with side rooms and dead ends |
| `Loop` | A ring that comes back to where it started, with a few short side rooms |
| `Sprawl` | Wide and open, with loops and many branches |

### The tilesets

- **Indoor:** `Cave`, `Crypt`, `Lava`. More follow: `Barracks`, `Jail`, `Lair`, `Durance`, `Dungeon`, `Sewer`, `Ice`, `Harem`, `Basement`, `Spider`, `Temple`.
- **Outdoor:** `Jungle`, then `Snow`.

A tileset that is not ready is refused when the game loads, with a message that says so.

### More columns

| Column | What it does |
|---|---|
| `Size` | `Small`, `Medium` or `Large` |
| `Level` | Monster level in the map |
| `Item` | The map item that opens this map. Blank: Cartographer's own item |
| `Boss` | A monster placed at the end of the path. Use its `Id` from the game's monster table; upper and lower case must match |
| `Monsters` | Up to 10 monster `Id`s, separated by commas, with the case matching. Blank: the tileset's own monsters |
| `Difficulty` | `Normal`, `Nightmare` or `Hell` |
| `MinCharLevel` | The character level the player needs |
| `Properties` | How many random properties this map gets |
| `EventChance` | Percent chance of an event |
| `Density`, `Champions`, `Uniques` | Percent more monsters, champion packs and unique packs |

See [tables.md](tables.md) for every column of every file.

## Random properties (optional, phase 5)

Every map gets a few random properties. Some make monsters tougher. Some change what happens to players. They come from one shared list.

Without a `mapstats.txt`, Cartographer uses a small built-in list. To use your own, add the file:

```
Target	Stat	Min	Max	Weight
Monsters	item_maxhp_percent	10	20	3
Players	fireresist	10	25	1
```

The text players see comes from the game's own stat description. You do not write any.

## Events (optional, phase 6)

A map can have an event, such as a boss waiting in a room or an ambush. Until you add a `mapevents.txt`, no map has events. To add some:

```
Name	Kind	Spawn	Count	Message
The Drowned King	Boss	Bishibosh	6	The water churns...
```

The kinds are `Boss`, `Ambush`, `Horde`, `Treasure` and `Shrine`. Events with puzzles or stages are not possible.

## Global settings (optional, phase 5)

`cartographer.txt` has two columns, `Setting` and `Value`. You can change the default number of properties, the default event chance, and the town the portal opens from.

## Advanced

You do not need this part for a normal map.

### Your own shape: `shapes.txt`

If the five shapes are not enough, add a `shapes.txt` that defines a new one with numbers: path length, straightness, turns, branches, branch length, loops, bounds and boss room. Then use its name in the `Shape` column. See [tables.md](tables.md).

### Your own room pieces: `tilesets.txt`

For a maze map, you can use rooms you drew yourself.

1. Make 15 room pieces, one for each way a room can open. They must be the same size as the built-in tileset you copy.
2. Add them to the game's room piece table.
3. Add one row to `tilesets.txt` that names your set and the built-in tileset it copies.
4. Use your set's name in the `Tileset` column.

Your rooms then show up only in Cartographer maps. The game's own levels are untouched. When the game loads, the check confirms all 15 pieces exist, the files exist and every piece has the same size.

A brand-new kind of tile art is not supported yet.
