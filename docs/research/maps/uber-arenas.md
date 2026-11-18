# Uber and event areas after the converter: how to build them

Request: research-uber-arenas. Read-only research, 2026-11-15. Sources: pd2-resurrected `origin/cartographer-migration`,
d2r-tools `origin/main` (d78952d), d2rAI-toolkit (with its D2R 93847 data cache `d2r-data/93847/`), GitHub issues #958,
#960, #961, #962, #1027 and PR #1030. Claims marked **[suspicion]** were not checked in game or in code.

> **Update after the owner's follow-up: every uber boss needs a square, fully enclosed arena** (see the last section).
> None of the single-room picks below meets that rule. They are all enclosed, but none is a square with at least
> 12x12 open tiles, and none can be made one while keeping HD/legacy parity. For the boss arenas, the recommendation
> becomes approach B, limited: a fixed small plan of whole stock pieces inside Blizzard's own border ring. C stays
> for the areas that have no boss rule (the Temple). Alternatively, the owner relaxes the rule to "enclosed".

## Recommendation (first pass)

**Approach C, done as data: give each reserved level (209-215) one whole, closed, stock D2R preset DS1, through a new
`lvlprest` row, as D2R itself does for Uber Tristram.** Pick the stock room by what the fight needs (open centre, a
square of floor, portal spots, chests), not by PD2's arena shape. Edit only the DS1 object list (strip stock bosses
and quest objects, add chests) and copy the stock HD preset JSON unchanged next to the copy. The tile layers stay
byte-for-byte Blizzard's, so the HD view and the legacy view match the same way they match in the stock game. No
converter, no new art, no Cartographer change.

Why:

- **D2R ships this exact pattern.** Level 136 (Uber Tristram, "Act 5 - Pandemonium Finale") is an Act V row with
  `LevelType 11`, `Layer 25`, `DrlgType 2`, the same as Tristram (38). Its own `lvlprest` row 1090 points at Tristram's
  `Act1/Tristram/Tri_Town4.ds1`, an Act I DS1 (`d2r-data/93847/data/global/excel/levels.txt`, `lvlprest.txt` row 1090
  vs row 300). The toolkit lists it as the clone pattern ("Stock DS1s already have HD presets, so a clone needs no new
  art", `d2rAI-toolkit/knowledge/new-game-systems.md:138-142`).
- **HD is per DS1, not per tile.** D2R draws a level from one preset per DS1 path,
  `data/hd/env/preset/<ds1 path>.json`, holding a sculpted `terrain.model` and placed models
  (`knowledge/hd-graphics.md:169-173`; `research/pd2-only-levels` doc lines 15-18). A stock DS1 brings its own
  Blizzard-made preset. A DS1 Blizzard did not make has none, and that gap is what the converter tried to fill.
- **That gap is where parity failed.** #1030 froze the converted arenas. Dclone's HD and legacy matched, but Lucion's
  lava rim did not: "solid lava where legacy draws thin cracks ... thin cracks need new HD art" (PR #1030 game-check
  comment). The owner closed it: "frozen arenas did not reach legacy/HD parity". Any approach that makes a new HD scene
  for a non-Blizzard tile layout ends up in the same place.
- **The fights need little geometry.** All they read from `placements.h` is a centre, a square of free floor (Dclone
  souls 50x50 subtiles = 10x10 tiles, Lucion adds 44x44 = about 9x9 tiles), boss spots, a marker and 1-3 portal spots
  (`plugin/placements/dclone.cpp`, `rathma.cpp`, `lucion.cpp`; `bitterdepths.cpp` has only a binding). Every
  candidate room below has more than that. Rathma bosses already "take the nearest free spot"
  (`docs/deviations.md:111`).

What this costs: **PD2's own arena shapes are lost.** The arenas look like D2R places (Throne of Destruction, Halls of
Vaught, and so on). The owner must accept that trade for each area (see the table). PD2's layouts cannot get exact
parity without new HD art (approach D).

## Approaches compared

| | Parity risk | Effort | Tooling exists / to build | PD2 shape kept |
|---|---|---|---|---|
| **C. Whole stock DS1 per level (recommended)** | Lowest: tiles and HD scene are Blizzard's pair, unchanged. Leftover risks: a copied preset at a new path (check once), stock objects left behind | Small: about 7 table rows plus 1 DS1 object edit per area; one small tool | Exists: `levels`/`lvlprest` set ops (`merge/ids-levels.tsv`, `merge/ids-lvlprest.tsv`), DS1 reader `tools/pd2_ds1.py` (`ds1_header`, `object_list`), modkit's D2R cache. To build: a DS1 object-list rewriter and a preset copy (below) | No: the size and theme are close, the layout is D2R's |
| A. New DS1 and preset composed from several stock rooms | Medium to high: parity holds inside each room. Seams between rooms Blizzard never joined show mismatched cliffs and walls, and terrain meshes overlap. This is the converter's "room paste" (`tools/presets/rooms.py`, `knowledge/hd-graphics.md:182`) under another name | High: a composer for tile layers and terrain entities, plus seam fixes per area | Mostly the converter, which is being retired (#965) | Partly |
| B. A "fixed plan" mode in Cartographer (fixed stamp list for a level id) | Medium: each stamped piece draws its own preset (engine stamp call `0x3ecfc0`, `plugins/cartographer/src/outdoor/stamp.h`). Edges need border rings checked side by side in game (`docs/cartographer/adding-a-tileset.md` step 8) | High, across two repos: an outdoor-only builder (Siege/Snow/Mesa/Jungle), a PD2-neutral feature upstream (ADR 0006 allows only "narrow optional SDK-mediated integration"), and "one map per game" to work around | Stamp/plan code exists upstream; the fixed mode, the id binding and placement export do not | No (stock pieces) |
| C' (C, but wholesale stock *random* levels, as Pandemonium 133-135) | Low for visuals | Small | Exists | No; and the layout changes every game, so the frozen absolute `Place`s cannot hold. Rejected for these fights |
| D. New legacy DT1 plus new HD art | Hand-matched each time; #1030 shows how it goes | Very high: DT1 authoring, `.model` meshes, `terrain.physics`, model index entries (`knowledge/hd-graphics.md:160-167`), art skills nobody here has. Every fix redone in two views | None | Yes, if done perfectly |

Escalation: use B (or A) only if a later area needs more floor than any single closed stock room has. None of the
seven does.

## Per-area table

PD2 sizes come from `origin/research/pd2-only-levels:docs/research/pd2-only-levels.md` (PD2 ids; the port's id is
shown first). Stock sizes and objects come from the 93847 DS1 headers and object lists, read with `tools/pd2_ds1.py`;
every candidate has an HD preset (checked on disk).

| Area (port id → new id) | PD2 shape and what the fight needs | Pick (stock DS1) | Alternative | Risks |
|---|---|---|---|---|
| Diablo Clone 138 → 209 | `uberdiablolvl.ds1` 48x24, a long Act IV box. Needs a centre, a 10x10-tile souls square, waves room, arrival/exit | Throne of Destruction `Expansion/baalLair/wthrone.ds1` 41x53 (lvlprest 1086, LevelType 34). Long hall, Baal-lair floor cracks in both views | Worldstone Chamber `wstone01.ds1` 56x56 | 76 objects: strip Baal, the throne/portal and the Worldstone portal; keep braziers. Baal's waves key on level 131, so a clone should not start them **[suspicion]** |
| Rathma stages 160-162 → 210-212 | `necropolis_1/2/3.ds1`, 180x160, 180x160, 151x151: clear, boss, clear, boss, clear, both bosses. Needs a marker, boss spots, arena rooms, 1 portal (stages 1-2) or 3 portals (stage 3), room activation | Halls of Vaught, one variant per stage: `Expansion/wildtemple/NihlE.ds1`, `NihlN.ds1`, `NihlS.ds1`, 85x85 each (lvlprest 864, LevelType 32). Three layouts, one theme; Nihlathak's end room is the boss arena | Stage 3 on `NihlW.ds1` or Worldstone Chamber | About 1/4 the PD2 floor, so a shorter "clear" phase (stage progress counts rooms). Strip Nihlathak's preset (type 1, id 49) and his quest objects. Cartographer holds RoomPopulate, so the "arena rooms' empty populate" goes unserved (`docs/deviations.md:111,125`) |
| Lucion arena 184 → 213 | `lucionarena.ds1` 50x50, hellcaves/void tiles with a thin lava rim. Needs a centre, the 9x9-tile adds square, the Temple portal spot, Royal Fire region | Worldstone Chamber `Expansion/baalLair/wstone01.ds1` 56x56 (lvlprest 1087). Square, near PD2's size; its preset has 111 floor-crack entities (Blizzard's crack art in both views, which is the very thing #1030 could not draw) | Throne of Destruction, if Dclone takes Worldstone | 43 objects: strip Baal's preset monsters (type 1, ids 47 and 33) and the Worldstone portal. The look is Baal's lair, not a lava cave. Note: Mephisto's Durance (`mephcomp`) has no lava in HD (its preset holds water, not lava), so it is out |
| Lucion's Temple 185 → 214 | `lucionvault.ds1` 61x61, holds `LucionChest` objects (PD2 623, port 626, `plugin/core-maps/lucion_chests.cpp`) and the Desecration cube spot. Needs chests, return portal, arrival | Act III Zakarum temple `Act3/Temple/Temple0.ds1` 25x25 (lvlprest 748, LevelType 22). Zakarum fits "Triune". Smallest area here | Act III dungeon treasure room `Act3/Jungle/DungRm1A.ds1` 41x41 (lvlprest 703), for more room | Chests: adding a DS1 object needs an `objpreset` row for that act that maps to object 626 **[suspicion: not checked]**. If no row exists, spawn the chests at `chest*` places with the existing `SpawnObjectAt` (`plugin/core-maps/map_events.cpp:347`), and note the contract change in `docs/cartographer-contracts.md:65` |
| Bitter Depths 198 → 215 | `bitterdepths.ds1` 101x51, Act II Tomb tiles. Needs an arrival, a return portal pair, the retained boss/marker | Duriel's Lair `Act2/Tomb/Duriel.ds1` 33x48 (lvlprest 481, LevelType 17). Tomb art, closed, lava in its preset | Tal Rasha's chamber pieces (16x16): too small and edge-open | Strip Duriel's preset and the Tyrael-door/orifice objects **[suspicion: which ids]**. About 1/3 of PD2's floor |

Shape fidelity per fight: Rathma's three stages keep three distinct layouts. Temple chests and the Desecration cube
need only floor. Lucion's adds square and Dclone's souls square need about 10x10 open tiles, which both rooms have. No
fight reads PD2 geometry beyond its `Place`s (`docs/cartographer-contracts.md:63-65`).

Rules each new row follows (from `knowledge/new-game-systems.md:144-162` and the 136 precedent):

- `Act=4`.
- `LevelType` and `Layer` copied from the stock level (shared automap layer; ids up to 255; `Layer` under 100).
- `DrlgType=2`, `Position=0`, `SizeX/SizeY` (all difficulties) = DS1 size minus 1.
- The reserved `OffsetX/OffsetY` kept (6000, 4200-6000, every 300 tiles), so the world rectangles do not overlap.
- The new `lvlprest` row copies the stock row's `Populate`, `Outdoors`, `Animate`, `KillEdge`, `FillBlanks` and
  `Dt1Mask`, and sets `LevelId` to the new level and `File1` to one DS1 (no random variants).
- The reserved rows today are copies of Bloody Foothills (`+110`, `merge/ids-levels.tsv:220-226`): every field above
  must be set, the outdoor DRLG fields included.

## Tooling to build (one shared step)

1. **Reserve 7 `lvlprest` ids** in `merge/ids-lvlprest.tsv` (shared, serial; the next free id is after 1156,
   `TombEventMap 1153`...`Descent Map 1156`).
2. **`tools/static_levels.py` plus one data file** (for example `merge/static-levels.tsv`: level, stock DS1, objects to
   strip, objects to add with tile x/y). At merge it:
   - reads the stock DS1 from modkit's cache (as `pd2_coverage.py` already does) and writes a copy under a new path
     (for example `data/global/tiles/pd2r/static/<name>.ds1`). Only the object block is rewritten; the header and
     tile layers stay byte-identical (`object_list` gives the offset);
   - copies the stock preset JSON byte-for-byte to `data/hd/env/preset/pd2r/static/<name>.json`. Its `terrain.model`
     and `terrain.physics` paths stay absolute paths to the stock files, so no mesh is copied
     (`wstone01.json` terrain filename `data/hd/env/preset/expansion/baallair/wstone01/terrain.model`);
   - emits the `levels` `set` ops and the new `lvlprest` row;
   - prints each named DS1 tile's world subtile coordinates (origin `OffsetX*5`, 5 subtiles per tile **[suspicion:
     check one against an existing Place]**), so `Place`s are copied, not guessed.
   - When no object needs changing, `File1` names the stock DS1 itself and nothing is written (the 136 way).
   - Generated, not committed (ADR 0002). Blizzard files stay out of git.
3. **Test** (`tools/tests/test_static_levels.py`): for every row, the copy's tile block hash equals the stock DS1's, the
   preset bytes equal the stock preset, the size and `LevelType` match the source level, and every `Place` of the
   bound encounter lies on a walkable tile of the DS1 (collision bit `0x20000`, `knowledge/hd-graphics.md:189`). This is
   the parity gate in CI: parity holds by construction, and the test proves the construction.

## Phased plan (issue-sized)

1. **Shared: static-level tool, reserved `lvlprest` ids, test, first proof on the Temple (214, 25x25, smallest).**
   Data only: the row, a copied DS1 with stock objects stripped, a copied preset. Proof: `cheat goto 214,x,y` and
   the stock Act III temple at the same tile, HD and legacy shots. This settles the two open questions: does a
   preset copied to a new path draw, and does a stripped DS1 load. No fight wiring (lane S; #962 owns the chests).
2. **D #958 Dclone on 209 (Throne).** Row, strip list, `dclone.cpp` places moved (`level` → `target` in the same PR,
   `docs/cartographer-contracts.md:63`), fight tiers 0-2.
3. **C #960 Bitter Depths on 215 (Duriel's Lair).** Row, strip list, arrival and return places in `bitterdepths.cpp`,
   Void/Tomb portal pair, return.
4. **D #961 Rathma on 210-212 (Halls of Vaught E/N/S).** Three rows, portal/marker/boss places per stage, stage
   transitions and shared life.
5. **D #962 Lucion on 213 (Worldstone) and Temple 214 (adds chests, from step 1).** Places for centre, adds,
   temple-portal and `chest*`; chests via DS1 objects or `SpawnObjectAt`; Desecration.
6. **#965** then removes the converter: no reserved level reads converted output.

Lane D runs 2 → 4 → 5 in series (tickets 14 → 17 → 18, `docs/cartographer-migration-tickets.md:77-85`). Lane C's
step 3 runs alongside.

## What to prove in game (per area)

- **Same spot, four shots:** new level HD, new level legacy (`g`), stock source level HD, stock source level legacy.
  Take them at the centre, at the boss/souls/adds spot, and at the N, S, E and W edges (`cheat goto <level>,x,y`,
  formula in the warp-to-coordinates note). Pass: new = stock in each view, and HD = legacy in layout, walls,
  cliffs, floor and lava/cracks.
- **Collision:** walk the edge where HD shows a wall; a player stops at the same line in both views.
- **Places:** each `Place` lands on free floor. Spawn the boss with cheats, open the portals, and check the summons
  land inside the square.
- **No leftovers:** no stock boss, quest object or waypoint left in the room.
- **The fight, per ticket:** tiers 0-2, entry by key at the Blood Fountain, re-entry, cleanup, rewards, return. Hell,
  level 90+, slots 2-5, shots attached to the PR.

## Open questions and suspicions

- A preset JSON copied to a new path, still pointing at the stock `terrain.model`, draws in HD. **[suspicion]**
  Step 1 proves it. Fallback: keep the stock DS1 path (no object edits) and strip objects at room init in the plugin.
- Stock objects keyed on the level id (Baal's waves, Nihlathak's quest) stay quiet on a new id. **[suspicion]**
- An `objpreset` row exists for `LucionChest` in the DS1's act. **[suspicion: not checked]**
- Rathma's shorter "clear" stages change pacing; PD2's progress is per room. Owner call.
- Each pick is a theme choice. Throne/Worldstone could swap between Dclone and Lucion; the owner should confirm the
  picks before the lanes start.

## Follow-up: square, fully enclosed arenas (owner rule)

### Method

Each outline is read from the 93847 DS1 tile layers, with the subtile flags of the level type's DT1 files (`lvltypes`
`File 1-32`; bit 0 blocks walking). A tile counts as floor when at least 13 of its 25 subtiles are walkable.

Legend: `.` floor, `w` floor carrying a wall on its N or W edge, `#` blocked (wall or block), `S` special tile
(orientation 10/11: stairs or warp), blank = no tile.

- **Enclosure.** `FillBlanks=1` in every pick's `lvlprest` row, and the data guide says "all blank tiles ... will be
  filled with unwalkable tiles" (`data/global/dataguide/data/files/lvlprest.js`). So blank means unwalkable.
- **Square.** "maxsq" is the largest all-floor square.
- **Cross-check.** The Throne outline was plotted against the HD preset's entities (10 HD units per tile). The 589
  `expansion_baallair_walls` entities sit on the same lines as the DS1 walls, so both views show this outline.
- **Limit.** The DT1 read is not reliable for lava. It reads all of Act IV `lavax.ds1` as walkable, while
  Cartographer's game check found that the closed Lava cell 836 has no walkable ground
  (`d2r-tools docs/reverse-engineering/cartographer/23-maze-lava.md`, "Edge fill"). The open-floor numbers for Act IV
  outdoor pieces are therefore **[suspicion]**.
- The scripts are in the scratchpad `t/` folder (`outline3.py`, `rank.py`).

### (1) The rooms as they are

| Room | Enclosed? | Outline | Openings | Largest open square |
|---|---|---|---|---|
| Worldstone Chamber `wstone01` 56x56 | Yes. Walls on every side: N/W walls in the floor cells, S/E walls in the next cell. The rest of the 56x56 is blank (unwalkable) | Not square and not open. Cross/H-shaped chambers 6-10 tiles wide (x12-42, y22-44) around a walled central block (x22-32, y22-38), the Worldstone, with a 4-wide walkable strip inside it | One `S` special tile at (34,25), the Baal portal/arrival. No walk-through exits. Corner marker tiles at the far diagonal (0,0)...(55,55) | 9x9 |
| Throne of Destruction `wthrone` 41x53 | Yes, as above | Not an open hall. A grid of walkways 3-6 tiles wide around solid blocks about 5x4 tiles; HD has no entities inside the blocks. Throne dais 9x14 at the north (x15-23, y0-14). The "waves" area is two 34-wide bands only 3 tiles deep (y24-26, y32-34) | Entry stairs `SS` at bottom centre (x18-19, y47), the warp from Worldstone Keep 3. The Worldstone portal is an object. No other gaps | 10x10 (the dais) |
| Halls of Vaught `NihlE/N/S/W` 85x85 | Yes | A plus shape: four halls 8-14 wide around a centre | Entry warp `SS` in the centre | 8x8 |
| Duriel's Lair 33x48 | Yes | Main hall 22x20 (x4-26, y2-22), but with a 9x10 central block and an inner wall ring | One 6-wide gap south to a 4-wide corridor and the warp room (`S`) | 8x8 |
| Act III Temple `Temple0` 25x25 | Yes | Rooms 3-7 tiles wide | `S` on the west edge | under 8 |
| Mephisto Complex 42x30 | Yes | Many chambers | stairs `SS`, `S` | 8x8 |
| Arreat / Colossal Summit 21x29 | Walls on the north only; W, S and E are cliff edges (unwalkable void) | An irregular plateau | `S` arrival | 10x10 |

### (2) Can they be squared and keep parity? No

- **What a preset holds.** A D2R HD preset has one terrain entity, which draws a single baked mesh for the whole DS1
  (`wstone01.json`: `terrain.filename = data/hd/env/preset/expansion/baallair/wstone01/terrain.model`, plus
  `terrain.physics`). Everything else in the preset is placed one entity per spot (walls, pillars, cracks:
  `knowledge/hd-graphics.md:171`, "a terrain entity plus placed entities"). The converter's room paste moved whole
  stock rooms because the terrain only comes whole: "it brings its own sculpted terrain" (`hd-graphics.md:182`).
- **Cropping the DS1** cuts the legacy view, but HD keeps drawing the baked terrain past the new edge. The two views
  stop matching.
- **Opening the floor** (removing the Throne's blocks or the Worldstone's centre) needs a terrain edit, because the
  mesh draws those blocks and pits. That breaks HD.
- **Closing a gap with stock walls** is possible tile by tile. Add the DT1 wall tile in legacy and copy the same wall
  entity from the same preset in HD (the override `add` op, `hd-graphics.md:212`). That holds only over flat floor,
  and it is hand-matched, not parity by construction. Neither the Throne nor the Worldstone needs it: both are
  already closed. What they lack is a square of open floor.

### (3) Stock rooms that are already a square enclosed arena of at least 12x12

**None among the stock single-room levels.** I scanned every `lvlprest` DS1 that has an HD preset (`rank.py`). The
only rooms tied to a level with a 12x12 or larger open square are the Act I courtyards (`cat_court`, `courtN/E/W`).
Those are outdoor monastery courtyards, open to the level edge.

Open squares of 12x12 and more exist only in maze and outdoor pieces, which Blizzard made to join their neighbours:

- Act IV Mesa fills, 16x16, read as fully open: `Outer`, `Mid` and `Chaos16X16_*` (Outer Steppes, Plains of Despair,
  City of the Damned). Their HD presets do hold ruin walls, gates and stairs (`chaos16x16_0.json`: 20 gates, 11
  ruin walls), so the open floor is **[suspicion]**.
- Chaos Sanctuary Arm W, about 12x12.
- Act II Tomb and Act I Catacombs rooms, 13-17.
- River of Flame pieces: lava read unreliable.

So **a square, enclosed arena with parity by construction can only come from stamping whole stock pieces inside the
border ring Blizzard made for them** (approach B, kept small):

- **Dclone: a Mesa fixed plan.** An interior of 2x2 grid cells (16x16 tiles) holding one 16x16 Mesa 3 ("Chaos", City
  of the Damned) fill, inside a ring of the 8x8 border pieces (W 800, N 801, E 802, S 799, corners 803-806,
  `d2r-tools docs/cartographer/tilesets/a4-mesa.md`). That makes a 32x32-tile square level, fully closed by cliffs,
  with 16x16 open floor. Two gaps: Cartographer's Mesa has no game check yet, and its smallest interior is 6 cells
  (`kMinCells`, `plugins/cartographer/src/outdoor/mesa_plan.h`). A fixed 2x2 mode needs to be built.
- **Lucion: the Lava/Chaos route.** Chaos Sanctuary is LevelType 28 (level 108 in `levels.txt`), the same as
  Cartographer's Lava tileset (`maze_tilesets.h:53`). Its lava filler ring (closed cell 836) is proven in game
  (#372). So a fixed plan with one Chaos piece in the middle (Heart 862, 24x24, the pentagram; or Entry 857), ringed
  by closed lava cells, gives a 72x72 level: the piece enclosed by lava, the same lava art in both views. Two risks:
  the Heart's four sides open to wings that will not be there, so the piece meets lava filler on a seam Blizzard
  never paired; and its clear square reads only about 9x9 between the pentagram blocks. **[suspicion; check in
  game]**
- **Rathma's boss stages and Bitter Depths** fail the rule in their C picks too (largest open squares of 8). If the
  rule covers them, they take the same Mesa or Lava plan, and the clear-the-area stages get smaller.
- **The Temple** has no boss. It can stay C (Act III Temple), unless the owner wants it square too.

Cost of B, as before:

- a "fixed plan" mode upstream in d2r-tools: a plan given as a list of stamps per level id, where the seed changes
  nothing, plus `Place`s exported in absolute tiles;
- ADR 0006's narrow-integration rule, and Cartographer's "one map per game" limit, have to allow the uber levels;
- the reserved rows 209-215 become slot levels of LevelType 27 or 28.

### Owner decision needed

Pick one:

- **(a) Relax the rule to "fully enclosed, open centre at least about 9x9".** Every C pick already meets it, with
  parity by construction. Best fits: Throne dais 10x10, Worldstone 9x9, Duriel's hall.
- **(b) Keep "square, at least 12x12".** Build Cartographer's fixed-plan mode first: Mesa for Dclone, Chaos/Lava for
  Lucion. Then game-check the seams and the ring (the same four-shot proof as above, plus every ring side).

New hand-made art (approach D) stays out: #1030 showed that its parity has to be matched by hand, every time.
