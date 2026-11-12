# Cartographer: steering D2R's level builder (DRLG)

Phase 2 of the Cartographer plan (`docs/plans/cartographer-plugin.md` on `docs/map-kit-plan`), issue #928. Sibling
spike: #927 (erase and rebuild a level).

Can D2R 93847's own level builders be steered to follow a room graph that Cartographer generates, and can a slot
level get a different tileset and maze parameters for each build?

Every address below is an RVA in our own Ghidra project (`C:\ghidra-work\project\D2R.gpr`, the 93847 dump, image
stamp `0x6ab3782c`), found with the toolkit's `tools/ghidra_query.ps1` (`StringXrefs`, `NearRefs`, `Refs`,
`FuncsInRange`, `Decompile`, `Disasm`, `FindDisp`). D2Common's assert strings name the source files:
`D2Common\src\Drlg\{Drlg, DrlgRoom, DrlgWarp, Preset, DrlgLogic, Maze, OutPlace, RoomTile, TileSub, DrlgVer, OutWild,
OutDesr, OutJung, OutSiege}.cpp`. Each file's assert stubs sit right before its code, which gives these code ranges:
Drlg.cpp `0x3261a0-`, Preset.cpp `0x3db000-`, Maze.cpp `0x3e67c0-0x3ef850`, OutPlace.cpp `0x3ef850-`, OutWild.cpp
`0x3fabb0-`, OutDesr.cpp `0x3fbce0-`, OutJung.cpp `0x3fcb80-`, OutSiege.cpp `0x3fe100-`. No code is taken from
locbones/D2RHUD-2.4.

## Verdict

| Builder | Steer to a graph? | How |
|---|---|---|
| **Maze** (indoor: caves, crypts, sewers, tombs, jail, catacombs, temples, ice caves, ...) | **Yes** | Replace the room walk `0x3e96d0` for the slot level. D2R's own room placer (`0x3e77a0`) places each graph cell and D2R's own piece picker (`0x3eb960`) gives it the piece whose exits match the cell, by construction. Steer the entrance/exit placer `0x3eb850` onto the graph's ends. Tileset, room size and merge chance are per-level fields: no shared record is written. A prototype is on branch `spike/cartographer-steer-proto` (see "Prototype"). |
| **Outdoor**, all acts (wilderness, desert, jungle, Kurast, act 4 plains, act 5 siege) | **No** with D2R's builders; **partly** with our own placement on D2R's primitives | Every outdoor builder is a hand-written routine for named level ids, run on a level that act start-up placed and sized from fixed tables. A new slot level id gets no size, no position and an empty builder. On top of that, D2RLoader's D2RCore replaces the outdoor entry point and five of its helpers. What could work: stamp LvlPrest pieces on the slot's 8x8-tile cell grid ourselves with D2R's cell stamp `0x3ecfc0` and let D2R turn the grid into rooms (`0x3ee780`). That is a small generator of our own (decision D5), so outdoor maps use their `fallback` preset for now. |
| **Preset** (`drlg = preset`) | not needed | One fixed DS1 per build; the file is picked from LvlPrest `Files` with the level seed. |

## 1. Where the level allocator reads its tables

### The level record ("DrlgLevel", one per level per act per DRLG)

| Offset | Field | Set by |
|---|---|---|
| +0x00 | DRLG type (1 maze, 2 preset, 3 outdoor) | `0x3267c0` from levels.txt `DrlgType` (LevelDefs +0x30) |
| +0x04 | flags (0x10: the client's DRLG) | `0x3267c0` |
| +0x08 | room count | `0x360ba0` (adds a room at the head of the list) |
| +0x10 | first room (list linked at room +0x48, newest first) | `0x360ba0` |
| +0x18 | type data: **maze: the LvlMaze row pointer**; preset: {.., file pick}; outdoor: a 0x340-byte grid block | `0x3e7760` / `0x3dd6a0` / `0x3ee6e0` |
| +0x24, +0x28, +0x2c, +0x30 | origin x, y and width, height in tiles | maze and preset: `0x327650` from LevelDefs Offset/Depend and SizeX/SizeY[difficulty]; outdoor: act start-up placement (`0x3f1c60`) |
| +0x1c8 | the DRLG (act) | `0x3267c0` |
| +0x1d0, +0x1d8 | act 3 jungle: block count and block array | `0x3f0490` (act start-up) |
| +0x1e0 | **LevelType (tileset)** | `0x3267c0` from levels.txt `LevelType` (LevelDefs +0x34) |
| +0x1e4 | the level's random seed (2 dwords) | `0x367110(level+0x1e4, drlg+0x860 + level id)` in `0x3267c0` and again at the start of `0x3271c0` |
| +0x1f0 | per-piece file rotation list | `0x3ea820`, `0x3ecfc0` |
| +0x1f8 | level id | `0x3267c0` |

### The functions

- **`0x3267c0` (dataFlag, drlg, level id) -> level**: finds the level in the act's list (`drlg+0x868`, linked at
  level +0x1b8) or allocates it. On allocation it reads the levels row once:

  ```c
  piVar15[0x78] = *(int *)(FUN_14032c200(param_1,param_3) + 0x34);   // level+0x1e0 = LevelType
  *piVar15      = *(int *)(FUN_14032c200(param_1,param_3) + 0x30);   // level+0x00  = DrlgType
  FUN_140367110(piVar15 + 0x79, *(int *)(drlg + 0x860) + level id);  // level seed
  if (type == 1) FUN_1403e7760(...);   // maze:   level+0x18 = LvlMaze row (0x32c6a0)
  else if (type == 2) FUN_1403dd6a0(...); // preset: picks a DS1 file from the LvlPrest row (0x32c410)
  else if (type == 3) FUN_1403ee6e0(...); // outdoor: allocates the 0x340-byte grid block
  ```

  `0x3e7760` (maze init) is `level+0x18 = 0x32c6a0(dataFlag, level id); 0x327650(dataFlag, drlg, level)`, and
  `0x327650` writes the level's size and origin from LevelDefs (`+0xc/+0x18` SizeX/SizeY by difficulty `drlg+0x830`,
  `+0x24/+0x28` OffsetX/Y, `+0x2c` Depend).
- **`0x3271c0` (dataFlag, level)**: builds the rooms. It reseeds the level, then dispatches on level +0x00:
  maze `0x3e69e0`, preset `0x3dd3c0`, outdoor through D2RLoader's thunk `0x3e2b5ce` (see 3.0). Callers: act start-up
  `0x326ba0`, `0x327400`, `0x3608a0`, the level-entry finder `0x3da650`, `0x3dd3c0`. Entry
  `48 89 6C 24 20 56 41 56 41 57 48 83 EC 20` (14 bytes, no loader patch), hookable.
- Table getters (all take the data flag first): LevelDefs row `0x32c200` (`dataTables+0x14a0`, stride 0x9c), LvlMaze
  row `0x32c6a0` (a linear search of `dataTables+0x18c0` by its `Level` column, stride 0x1c: `{Level, Rooms[3],
  SizeX, SizeY, Merge}`), LvlPrest row `0x32c360` (+0 Def, +0x3c Files), LvlTypes row `0x32c300`.

### Who reads what, after allocation

- **LevelType** is read from **level +0x1e0** by every builder (`0x3e69e0`, `0x3eb960`, `0x3ea820`, `0x3ebd90`,
  `0x3ee780`, `0x3efaf0`, `0x3f1c60`, `0x3f95e0`) and by the room tile loader `0x3f3970` (RoomTile.cpp), which takes the
  room's DT1 files from the LvlTypes row of `room->level->+0x1e0` and the piece's `Dt1Mask`:

  ```c
  lVar1 = FUN_14032c300(param_1, *(undefined4 *)(*(longlong *)(param_2 + 0x90) + 0x1e0));
  for (uVar3 = 0; (uVar2 != 0 && (uVar3 < 0x20)); uVar3++) if (uVar2 & 1) FUN_141460250(param_2 + 0xb0, uVar3 * 0x3c + lVar1);
  ```

- **LvlMaze** is read only through **level +0x18**: `Rooms[difficulty]` in the walk `0x3e96d0`, `SizeX/SizeY` for every
  new room in `0x3e69e0`/`0x3e77a0`/`0x3e9820`, `Merge` (per mille) in `0x3e77a0`/`0x3e9820`.
- **LvlPrest** for mazes is **not a table column**: the piece is `base(LevelType) + exit mask`, hardcoded (see 2.3).
  The DS1 file of each piece comes from that LvlPrest row's `File1-6`.
- **DrlgType** is fixed at allocation: the type data at +0x18 is allocated for that type in `0x3267c0`.

### Can a build be given other parameters without leaking? Yes: per-level fields and a private record

Writing the shared records would leak: the data tables are one copy per data flag for the whole process, read by the
host and by the single-player client (`0x32c200` and `0x32c6a0` both resolve through `0x300a90(dataFlag)`). But nothing
needs writing there. **Pick: point the slot level at a private record copy, and write the per-level fields.** Before the
original `0x3271c0` runs for the slot level:

| Parameter | Write | Notes |
|---|---|---|
| Tileset | level +0x1e0 = LevelType | Read by the builders and the tile loader from the level. All pieces must belong to this LevelType (their `Dt1Mask` indexes its DT1 files). |
| Maze rooms, room size, merge | level +0x18 = a private 0x1c-byte LvlMaze row | Copy a vanilla row of the target tileset (room size must match its pieces: Cave 24x24, Crypt 8x8, Jail and Catacombs 12x12), set `Level` to the slot. Read-only for D2R, so one copy can serve host and client. |
| Level size and origin | level +0x24..+0x30 | `0x327650` set them at allocation; overwrite them for a bigger or smaller grid. Bounds are checked by `0x360520` against +0x2c/+0x30 for each new room. |
| DRLG type | none | Keep one slot per DRLG type (as the plan says): +0x18 is allocated for the type at allocation time. |
| Seed | `drlg+0x860` around the call | `0x3271c0` reseeds the level from `drlg+0x860 + level id` at its start, and `drlg+0x860` is shared by the act's levels, so write it, call, restore (the plan's approach), on the build thread only. |

No shared record is touched, so there is nothing to restore.

## 2. The maze builder (Maze.cpp)

### 2.1 Overview: `0x3e69e0` (dataFlag, level)

1. Makes the first room (`0x361140(level, 2)`), sized from the LvlMaze row, centred in the level.
2. `switch (level +0x1e0)` per tileset: grows the maze and places that tileset's special rooms. Most tilesets run the
   walk `0x3e96d0` and then `0x3eb850` once or twice with a 16-byte table entry (entrance, exit). Some add a ring
   first (`0x3e9820(level, n)`, an n x n loop: Barracks, Jail, Sewer, Tomb, Lair, Durance, Dungeon, Ice), some have
   their own grower (Arcane `0x3e7a80`, Catacombs `0x3e8610`, Tomb `0x3ea0e0`, A2 Sewer `0x3eab30`, Tomb `0x3eb610`),
   and a few are not grown at all (Harem 14, Basement 15, Spider 23, Temple 32: a 2x2 ring; Baal 35: two fixed rooms).
   Level ids are tested inside the cases for quest levels (8 Den of Evil, 0x3d, 0x40, 0x5c, 114-119, ...).
3. Normalises room positions (`0x327880`; Barracks 28 and River of Flame 107 have their own `0x3ec2c0`/`0x3ec820`).
4. `0x3ebd90`: swaps `max(2, rooms/5+1)` random rooms to the "+15" variant of the same exit mask (same exits, other art).
5. `0x3ea820`: turns each maze room into a preset room (`0x3dd1c0` + `0x3dcb20`): the DS1 file is the piece's next in a
   per-piece round robin with a seeded start (level +0x1f0), so the same piece repeats its files in turn.

Maze-capable tilesets (cases of `0x3eb960`; any other LevelType asserts): 3 Cave, 4 Crypt, 7 Barracks, 8 Jail,
10 Catacombs, 13 A2 Sewer, 14 Harem, 15 Basement, 17 Tomb, 18 Lair, 19 Arcane, 22 Kurast (Durance), 23 Spider,
24 Dungeon, 25 A3 Sewer, 28 A4 Lava, 32 Temple, 33 Ice Caves, 34 Baal, 35 A5 Lava (LvlTypes `Id`).

### 2.2 The room-adding loop and its direction picker: `0x3e96d0` (level)

```c
iVar1 = maze->Rooms[*(byte *)(drlg + 0x830)];             // LvlMaze Rooms by difficulty
if (level id == *(int *)(drlg + 0x120)) iVar1 *= 3;        // act 2 tomb levels picked at act start-up
iVar7 = (level id == *(int *)(drlg + 0x874)) ? iVar1 * 2 : iVar1;
while (level->roomCount < iVar7) {
    room = the (seed % roomCount)-th room of the list;     // level seed
    dir  = room->seed & 3;                                 // the room's own seed (room +0x30): 0 W, 1 N, 2 E, 3 S
    if (!(room->piece->flags & 2) && (n = FUN_1403e77a0(room, dir, 1)) != 0) {
        FUN_1403eb960(room, 1); FUN_1403eb960(n, 1);       // re-pick both pieces from their new exits
    }
}
```

Entry `48 89 7C 24 20 41 56 48 83 EC 20 4C 8B 81 C8 01 00 00` (18 bytes), called only by `0x3e69e0`.

**`0x3e77a0` (room, dir, merge) -> room or null** places one room next to another: dir 0 W, 1 N, 2 E, 3 S (4-7
diagonals, unused by the walk), sized from the LvlMaze row; it fails (frees the room, returns null) when the room
leaves the level (`0x360520`), overlaps another (`0x360470`, `0x360c80`); else it links the two rooms (`0x360bb0`)
and, with `merge`, links the new room to each adjacent free room with chance `Merge/1000` (this is where loops come
from), and adds it to the level.

### 2.3 The piece picker: `0x3eb960` (room, clearFixed)

```c
for (link = room->links; link; link = link->next)          // exit mask from the room's links
    mask |= {0: 1 /*W*/, 1: 8 /*N*/, 2: 2 /*E*/, 3: 4 /*S*/}[link->dir];
switch (level->+0x1e0) {
case 3:  piece = mask + 0x34;  break;   // Cave:  LvlPrest 53 "Cave W" ... 67 "Cave NSEW"
case 4:  piece = mask + 0x6c;  break;   // Crypt: 109 ... 123
case 7:  piece = mask + 0xa7;  ...      // Barracks 168-182, Jail 0xcd, Catacombs 0x101, A2 Sewer 0x12d, Tomb 0x19d,
                                        // Lair 0x1e1, Arcane 0x1fd, Durance 0x2f1, Dungeon 0x298, A3 Sewer 0x2c0,
                                        // A4 Lava 0x344, Ice 0x3ea, Baal 0x422; Harem, Basement, Spider, Temple,
                                        // A5 Lava use small tables (0x141d0ebf0, 0x141d0ecb0)
}
room->piece = {piece, file -1}; if (clearFixed) room->piece->flags &= ~2;
```

The LvlPrest rows confirm the layout: `Def` base+1..base+15 are `<tileset> W, E, EW, S, SW, SE, SEW, N, NW, NE, NEW,
NS, NSW, NSE, NSEW` for every maze tileset (vanilla `lvlprest.txt`). So **a room's piece is a pure function of its
exits and the tileset: whatever graph the rooms follow, each piece's exits match its cell.** Nothing needs limiting.

### 2.4 Special rooms: `0x3eb850` (level, entry[4], &rotation)

Each tileset case calls it with a 16-byte entry `{from, to, file, dir}` picked as `group + rotation`, where groups hold
the four rotations of one special room. Cave's table `0x141d0ee30` (dumped):
`{60,86,-1,3} {54,84,-1,0} {56,85,-1,1} {53,83,-1,2}` (entrance), `{60,98..}` (Den of Evil), `{60,94..}` (exit). It
turns the newest free room whose piece is `from` (a dead end of one orientation, e.g. 53 "Cave W") into `to` and marks
it fixed; if none exists it grows a new room in `dir` from the newest free room that has space. Then it advances the
rotation. Entry `41 56 48 83 EC 30 44 8B 0A 4D 8B F0 48 89 5C 24 40` (17 bytes).

### 2.5 Answer: yes

A hook on `0x3e96d0`, active only for the live slot level, replaces the walk:

1. Map graph cell (0,0) to the first room (`level +0x10` at entry; `0x3e69e0` made it and nothing else yet).
2. Walk the graph breadth-first; for each edge call `0x3e77a0(parentRoom, dir, 0)`, then `0x3eb960` on both rooms.
   A null return means the cell is out of bounds or overlaps: fail the build (re-roll). Pass `merge = 1` only for the
   graph's loop edges, or link loop edges explicitly with `0x360bb0`.
3. Special rooms: the graph's entry and exit cells are dead ends; `0x3eb850` needs one whose orientation matches the
   rotation it drew. Either hook `0x3eb850` to switch to the rotation that matches the cell (the prototype does this),
   or grow the end cells last so the "newest free room" scan finds them. The boss or event room is a `required` piece:
   write it into its cell with the fixed flag after the walk, as `0x3eb850` does.

Limits:

- Only the 4 cardinal directions, one room per grid cell, all rooms the size of the LvlMaze row. The grid must fit
  the level size (+0x2c/+0x30), with the first room at the centre: `bounds` in rooms is about `levelSize / roomSize`
  (200 tiles / 24 = 8 cave rooms across, 200 / 8 = 25 crypt rooms).
- The tileset cases still run their level-id tests; a slot level id matches none, so a slot gets the tileset's
  generic entrance/exit only (no Den of Evil, Tal Rasha or quest pieces).
- Only Cave (3), Crypt (4) and A4 Lava (28) grow by the walk alone. Barracks, Jail, Lair, Durance, Dungeon, A3 Sewer
  and Ice first lay a 2x2 ring with `0x3e9820(level, 2)` and then walk; Harem, Basement, Spider and Temple lay only
  the ring. For those, also hook `0x3e9820` to do nothing for the slot (and run the graph there when no walk follows).
  Arcane, Catacombs, the tombs and A2 Sewer run their own growers (`0x3e7a80`, `0x3e8610`, `0x3ea0e0`/`0x3eb610`,
  `0x3eab30`) and Baal places two fixed rooms: leave them out until those are studied.
- The variant swap (`0x3ebd90`) and the file rotation (`0x3ea820`) stay random by the level seed, which is fine: they
  never change exits.

## 3. The outdoor builders

### 3.0 D2RLoader owns the outdoor entry

In the running image, the outdoor call in `0x3271c0` does not go to D2R: it calls `0x3e2b5ce`, a stub in the `.maho`
section D2RLoader adds, `JMP [0x3e2ac38]`, a pointer filled at start-up. D2RCore.dll holds the strings
`GenerateOutdoorLevel`, `PlaceOutdoorPresetGroup`, `PlaceOutdoorShrines`, `PlaceOutdoorWaypoint`,
`PlaceRandomOutdoorPreset` and `Outdoor map generation stopped because %s` ("the outdoor layout is null", "the outdoor
dimensions are too small to contain the map border", ...). The same kind of stub replaces calls inside D2R's outdoor
code: `0x3e2b5d4` (in `0x3fac10`, `0x3fbd10`: shrines), `0x3e2b5da` (`0x3fb0d0`, `0x3fb1a0`), `0x3e2b5e0` (about 70
calls from every outdoor builder: random preset placement), `0x3e2b5e6` (waypoint), `0x3e2b5ec` (in TileSub
`0x3f4e40`), and `0x3e2b5c8` (the level's act, called all over the game). D2R's own outdoor entry `0x3ee320` has no
other caller.

So D2RCore reimplements the outdoor entry and five helpers. Whether its `GenerateOutdoorLevel` still calls D2R's
per-act builders below, or replaces them, cannot be read from a dump (D2RCore is packed on disk and its code is in a
heap block at run time). **Unanswered within the time box**: an inline hook on `0x3fac10` that counts calls while
entering the Blood Moor would settle it. Any outdoor steering hook must sit on functions D2RCore still calls, or go
through D2RLoader if it ever offers a DRLG service (the SDK has none).

### 3.1 Shape of D2R's outdoor build: `0x3ee320` (dataFlag, level)

1. The grid block (level +0x18, 0x340 bytes): four cell grids at +0x08, +0x30, +0x58, +0x80, `+0xb0/+0xb4` the size in
   8x8-tile cells (`level size / 8`), `+0xb8` the level's outline (DrlgVer.cpp `0x3f9b80`, the vertices it shares
   with neighbouring levels; `+0x338` the neighbour links).
2. `switch (act of the level)`: act 1 `0x3fac10`, act 2 (and level 134) `0x3fbd10`, act 3 `0x3fcbe0`, act 4 `0x3fdc00`,
   act 5 `0x3fe2e0`.
3. `0x3ee780`: turns each grid cell into a room: a LvlPrest piece (`0x3dd1c0` + `0x3dcb20`) or tile fill
   (`0x3f93a0`).

Primitives the per-act builders share:

- **`0x3ecfc0` (dataFlag, level, cellX, cellY, lvlprest, file or -1, flag)**: stamps one LvlPrest piece onto the cell
  grid (its size in cells from the piece), the file round-robin as in mazes.
- `0x3efa20` / `0x3efaf0`: the border with neighbouring levels from the outline (cliffs and walls; `0x3ede60` picks
  the border piece).
- `0x3ee010` (dataFlag, level, layer, piece) and `0x3fe2e0`'s own call: the **LvlSub** pass, TileSub.cpp `0x3f4e40`
  with callbacks (`0x3ee0b0`, `0x3ee0d0`, `0x3edda0`, `0x3ede00`, `0x3ecfc0`): replaces groups of fill cells with
  substitution pieces (levels.txt SubType/SubTheme/SubWaypoint/SubShrine).
- Random preset placement (`0x3e2b5e0`, D2RCore's): finds free cells for a piece.

### 3.2 Act start-up decides each outdoor level's place and size

`0x326ba0` (act start-up, called by the act allocator `0x2ef1c0` for host and client) calls `0x3f0130(dataFlag, drlg,
act)`, which places the act's outdoor levels with `0x3f1c60(table, callbacks)`. Each table row is `{fn, level id,
link-from row, ...}` (24 bytes; dumped): act 1 `0x141d0fd60` = Cold Plains 4 <- 3 <- 2 <- 1, Burial Grounds 17;
`0x141d0fed0` = 39, 26, 7, 6, 5; act 2 `0x141d10040` = 40-45, 46; act 4 `0x141d10490` = 103-106, 108; act 5
`0x141d10770`/`0x141d108e0`/`0x141d10320` = 109-112, 117, 134, 136; act 3 is special (3.4). The row's function grows
the level off its link (a side, by the seed) and sizes it from LevelDefs SizeX/SizeY. **A level id that is not in
these tables is never placed: its +0x24..+0x30 stay 0 and its builder has no case for it.**

### 3.3 Act 1-2 wilderness and desert

- Act 1, `0x3fac10`: levels 2-7 (Blood Moor to Tamoe Highland) get `0x3faf60` (outline cliffs), the border, LvlSub
  layers 0-3 (`0x3ee010`), `0x3fb710` and `0x3fb910` (the river and its bridges, stamped with `0x3ecfc0`),
  `0x3ff5c0`; 39 (Cow Level) a fill; 3-6 a waypoint; shrines; then `0x3fb1a0`, a per-level-id switch of fixed
  pieces (Tristram stones, towers, caves, the Cold Plains bridge: LvlPrest 0x26-0x32, 0xa0-0xa3).
- Act 2, `0x3fbd10`: a switch on level id 41-46 and 134 with fixed piece lists (`0x141d129e0`-`0x141d12a40`) for the
  tomb and sewer entrances, the Far Oasis and Lost City, Valley of Snakes, Canyon of the Magi.

Layout = LvlSub fill on a rectangle + border + a list of named pieces. There is no room graph to steer: the level is
one open rectangle.

### 3.4 Act 3 jungle and Kurast

The owner's guess holds: the jungle is chained preset blocks. At act start-up, `0x3f0130` case act 3 places Kurast
Docks (75) and calls **`0x3f0490`** (5.6 KB), which places three level rectangles the size of Spider Forest's row
(levels.txt 76 SizeX/SizeY) at random sides of each other (`0x3f03c0`, `rand % 5`, no overlap via `0x360470`), lays a
grid of 32x32-tile blocks over their union and fills it with jungle block pieces that connect, then gives each of 76, 77,
78 its share: `level+0x1d8 = blocks`, `level+0x1d0 = count`. At build time `0x3fcbe0` -> `0x3fd170` stamps them
(`0x3ecfc0` at `(x*4, y*4)` cells), adding a per-level offset to blocks above 0x23e (variants) and a file from
`0x141d12a60`, then `0x3fd430` adds Kurast: Lower Kurast 79 (`0x3fcf40`), Bazaar 80 (a fixed ring of wall pieces
0x26b-0x274 around a block field, orientation by `drlg+0x864`), Upper Kurast 81 (`0x3fcd20`), then the temples
and town blocks by `0x3fd950` and random placement; Causeway 82 is one piece 0x28c; Travincal 83 six fixed pieces.

Jungle steering would mean writing our own block array (+0x1d8) on a level that is one of 76-78 and whose rectangle
act start-up placed. A slot level id is not.

### 3.5 Act 4 and act 5

- Act 4, `0x3fdc00`: 104-106 (Outer Steppes, Plains of Despair, City of the Damned) get the border, LvlSub fills with
  piece 799 and `0x3fdd90`, about 30 random-placement calls of mesa pieces; Chaos Sanctuary 108 is a fixed 5x5 grid of
  pieces from `0x141d12b70` (836 fill, 857-862 the seals and star).
- Act 5, `0x3fe2e0` (OutSiege.cpp): Bloody Foothills 110 a row of siege pieces from 0x361; the others border,
  `0x3ff0e0`, fixed pieces 0x370/0x380 (Frigid Highlands), LvlSub with its own callbacks, `0x3feb80`, and a
  per-level table of random placements.

### 3.6 Answer: no, partly with our own placement

Outdoor layouts are open rectangles of fill and LvlSub with named pieces; the level's place, size and neighbours come
from act start-up tables, and every builder is a switch on vanilla level ids. A graph cannot be fed to any of them.
The workable route (later, D5) is a builder of our own for the slot level that uses D2R's primitives:

1. Set the slot's size (+0x2c/+0x30) and origin; allocate the grid block as `0x3ee320` does (`0x3fa7f0` per grid).
2. Stamp the graph: one LvlPrest block piece per cell with `0x3ecfc0` (act 3 jungle blocks are 32x32 tiles = 4x4
   cells and already connect at their edges, which fits a graph of blocks well; Kurast's town blocks too).
3. Optionally run the LvlSub pass (`0x3ee010`) for fill, then `0x3ee780` to make the rooms.

That skips D2RCore's `GenerateOutdoorLevel` only if Cartographer replaces the outdoor dispatch for its slot (the
D2RLoader stub in `0x3271c0`), which needs care with D2RLoader. Until then, outdoor maps use their `fallback` preset,
as the plan says.

## 4. What a client needs to rebuild the same layout

The client builds its own DRLG: the act allocator `0x2ef1c0` is called by the host's game (`0x48aa50`, seed
`game+0x12c`, difficulty `game+0x104`) and by the client's act loader `0x86bc0` (seed from the load-act message,
`isClient = 1`, stored at `drlg+0x110`). Both run the same D2Common code, so the same hooks run on both sides.

| Input | Where | Notes |
|---|---|---|
| Act seed | `drlg+0x840` (start), `drlg+0x860` (first draw: the level seed base) | Level seed = `drlg+0x860 + level id`. The map seed replaces it for the slot (1). |
| Difficulty | `drlg+0x830` | Room count by difficulty, level size. |
| Tables | levels, lvlmaze, lvlprest, lvltypes, lvlsub | Same mod data on both sides (D2RLoader already requires matching mods). |
| Act start-up draws | `drlg+0x120`, `+0x874` (act 2 tombs), `+0x864` (act 3 Bazaar side) | Rebuilt by the client's own act allocation from the act seed. |
| Cartographer's overrides | tileset (+0x1e0), private LvlMaze row (+0x18), size (+0x24..+0x30), the graph | **Not** in any D2R message. The client derives them from `{map id, seed}`: the graph generator must be pure and deterministic, so it runs on the client too. |
| Build order | | The level seed is drawn in order by every room, piece file and variant: client and host must run the same hook code (same plugin version, `Shared` flag). |

So `{map id, seed}` is enough, as the plan says, if the graph is a pure function of them and the map row. The client
also needs the level rebuilt in its own DRLG, which is phase 1's question (#927).

The client preloads tile files per act: `0x86bc0` adds the DT1 files of every LvlTypes row whose `Act` is the
current act (first 36 rows only). A slot in act 1 that builds with an act 2 tileset may then miss its tiles, or load
them late. In game, act 5 Ice tiles in an act 1 level rendered fine (Prototype, build 3), so the HD client loads
them some other way too; other pairs are unchecked.

## Prototype

`plugin/cartographer-steer-spike/` on branch `spike/cartographer-steer-proto` (never merged): a standalone plugin (plugin_common + SDK) with three
hooks, all byte-checked:

- `0x3271c0`: for the configured level, writes the tileset into level +0x1e0 and points level +0x18 at a private
  copy of another level's LvlMaze row, then builds and logs every room.
- `0x3e96d0`: places a hand-written straight graph (`west` rooms west and `east` rooms east of the first room) with
  `0x3e77a0(room, dir, 0)` and `0x3eb960`.
- `0x3eb850`: switches to the rotation of the special-room group that matches an existing dead end.

### Game check (Slot4, Hell, level 99 Paladin, 2026-11-11)

All three builds were of level 10, Underground Passage Level 1 (act 1, native tileset Cave), with the graph "3 west,
3 east" of the first room. Each run was a new game, entered with `cheat goto 10`. The plugin logs every build twice,
once for the server and once for the client, and in every run the two logs list the same rooms at the same places
with the same pieces. So the client rebuilds the steered layout from the same hooks and the same seed (section 4).

| Build | Config | Result |
|---|---|---|
| 1 | native tileset (Cave) | The guided walk placed 6 rooms: a 7-room straight west-east line. The special hook moved Down (54 -> 92) to rotation 1. One extra room grew north: level 10 needs three specials (Prev, Down, Next), and a straight line has only two dead ends, so `0x3eb850` added a room for "Next" (Cave Next S, 89). Pieces: 3x Cave EW (55), Theme EW 70, Theme NEW 78, Prev 83, Next S 89, Down E 92. Cave tiles in game, automap correct, no crash. |
| 2 | tileset 4 (Crypt), LvlMaze row of level 18 (8x8, merge 500) | Exactly the 7-room straight line (grid `#######`). Pieces: 4x Crypt EW 111, Theme E 125, Theme EW 126, Crypt Prev W 139. Only an entrance: the Crypt special table has no exit row for level 10, as predicted (2.4). Crypt tiles in an act 1 Cave level, no crash. |
| 3 | tileset 33 (act 5 Ice), LvlMaze row of level 113 (16x16) | Ice tiles render in the act 1 level, no crash (cross-act preload is not a problem for this pair). But the build was not on the graph: the first room's east neighbour "did not fit", the guided walk placed 3 rooms, and the level ended with 36 rooms of 8x8 in 2x2 blocks. Ice uses the ring grower `0x3e9820`, which the prototype does not no-op; this is the "Tileset growers" risk below, now confirmed. |

So: a graph steers a native-tileset maze level (build 1) and a level given another tileset (build 2), identically on
host and client. Two limits showed up. The graph needs one dead end for each special room the level wants. A
tileset with its own grower (build 3) needs that grower hooked too.

Logs (outside the repo): `C:\D2RTestSlots\runs\drlg-steer-928\` (`build1-cave.log`, `build2-crypt.log`, `build3-ice.log`); screenshots in
its `screenshots\` folder (`cave_ingame.png`, `cave_automap.png`, `crypt_ingame.png`, `ice_ingame.png`).
`...\screenshots\` (`cave_ingame.png`, `cave_automap.png`, `crypt_ingame.png`, `ice_ingame.png`).

## Risks

- **D2RLoader's outdoor replacement.** D2RCore owns the outdoor entry and several helpers; a D2RLoader update can
  change them under us. Outdoor steering must wait for a probe (3.0) and should go through a D2RLoader service if one
  appears.
- **Special rooms.** Entrances and exits pick a dead-end orientation at random; a graph whose ends do not offer it
  grows an extra room. Hook `0x3eb850` (as the prototype does) or post-check and re-roll.
- **Tileset growers.** Steerable now: Cave, Crypt, A4 Lava (walk only); with a no-op `0x3e9820` hook also Barracks,
  Jail, Lair, Durance, Dungeon, A3 Sewer, Ice, Harem, Basement, Spider and Temple. Not yet: Arcane, Catacombs, the
  tombs, A2 Sewer (own growers) and Baal (fixed). Cartographer should refuse other tilesets for `maze` maps at load.
- **Cross-act tilesets.** Tile preload per act (4); and the plan puts slots in the opening town's act. Act 5 Ice in
  an act 1 level rendered fine (build 3); other pairs are still unchecked.
- **Seed field shared by the act.** `drlg+0x860` seeds every level of the act; write it only around the slot's build,
  on the thread that builds.
- **Bounds.** A graph larger than the level fails in `0x360520`; the graph generator must use the slot's real size and
  the tileset's room size.
- **Multiplayer.** The graph must be identical on host and client; any random draw in the hook must come from the map
  seed, never from the level seed's state in a different order.
