# Plan: Cartographer's own outdoor level builder

2026-11-11. This is part of [cartographer-plugin.md](cartographer-plugin.md), phase 4. D2R's own outdoor builders can't build a map level, as [#929](https://github.com/synpoox/pd2-resurrected/pull/929) §3 shows, so Cartographer builds outdoor levels itself, from D2R's own pieces and primitives.

Each claim is labelled:

- **[K]** known: from the research docs, or from decompiles in `C:\ghidra-work\decomp`.
- **[A]** assumption.
- **[V]** still to verify, in Ghidra or in game.

The LvlPrest ids and sizes quoted are checked against vanilla `lvlprest.txt` (93847).

## 1. Builder design

### 1.1 Slot level: size and origin

- **The slot row.** It is a `levels.txt` row in Act 5 (index 4), with `DrlgType` 3, no waypoint, no Vis/Warp links and no neighbours.
- **Grid block.** `0x3267c0` allocates the level's 0x340-byte grid block through `0x3ee6e0` [K].
- **The rectangle.** The rectangle at `+0x24..+0x30` stays 0, because only act start-up (`0x3f1c60`) places outdoor levels [K]. Cartographer's build hook writes it before the build:
  - origin = the slot row's `OffsetX/OffsetY` (LevelDefs `+0x24/+0x28`), chosen far from every Act 5 level;
  - size = cells × cell size × 8 tiles, taken from the solved layout.
- **Neighbour list.** The neighbour list at grid block `+0x338` stays null, so the level is an island. The removable test `0x327e70` then finds no linked levels.
- [V] What D2RCore's act lookup `0x3e2b5c8` returns for a new level id. Expected: 4, from levels.txt `Act`.
- [V] The largest safe level size. Start at a cap of 12×12 jungle blocks (384 tiles).

### 1.2 Grid allocation

**Recommended: call `0x3ee320` itself.** It dispatches on act; for Act 5 it calls `0x3fe2e0`.

1. A byte-checked inline hook on `0x3fe2e0` runs Cartographer's stamper when the level is the slot. For every other level it runs the original.
2. `0x3ee320` then calls `0x3ee780` itself, which makes the rooms.

**Fallback:** re-implement the head of `0x3ee320` (about 40 lines) [K]:

1. Set `+0xa8 = 0`, and `+0xb0/+0xb4` = the size in cells.
2. Call `0x3fa7f0` once for each of the four grids: `+0x08` (piece id at its origin cell), `+0x30`, `+0x58` (flags: `0x200` covered, file index in bits 16-19) and `+0x80`.
3. Build the outline with `0x3f9b80(+0xb8, rect, 0, +0x338)`, then convert it to cells and dedupe. Dedupe frees go through the allocator vtable `0x140a20bb0`/`0x140a20c80`.

### 1.3 From the layout to placed pieces

- **Stamping.** Each piece is stamped with `0x3ecfc0(dataFlag, level, cx, cy, lvlprest, file|-1, 0)` [K]. That call sizes the piece from LvlPrest, marks the cells it covers, and draws the file from the level RNG at `+0x1e4`, which is deterministic.
- **Macro-cells per family:** jungle 4×4 cells (32 tiles); barricade 2×2; Kurast and wilderness 1×1.
- **Act 3 jungle edge rule.** The piece is `0x211 + mask`, as in the native jungle builder `0x3f0490` [K].
  - The mask bits are W=1, E=2, S=4, N=8.
  - LvlPrest 530–544 are "Jungle W" … "Jungle NSEW", all 32×32, files `RivW.ds1` … `RivNSEW.ds1` [K, checked].
  - Two neighbouring blocks connect when each sets the bit that faces the other.
  - Mask 0 is 529 (Act 3 Town) and must never be emitted.
- **Clearings** (Webby 575–584, Boggy 585–594, Pygmy 595–604) exist only for one or two exits. They serve as boss and event cells [K].
- **Pieces to avoid.**
  - Head 573 and Tail 574 are 64×32 transitions to the Docks and Travincal [K].
  - "Jungle W E" … 545–572 (`RivWoE.ds1`, …) look like neighbour-level edge pieces [A].
- **Central open question: river or path?** The files are named `Riv*`, so the mask may describe *river channels*, not walkable exits [V, spike S1].
  - **If the mask means walkable exits:** the graph maps 1:1 onto masks.
  - **If it means rivers:** land is walkable across every shared edge, so the shape comes from which cells are occupied, and the graph becomes the river network.
  - **Either way,** the generator forbids side contact between cells that are not graph neighbours.

### 1.4 Fill and border

- **Cells without a piece.** `0x3ee780` makes a tile-fill room for each (`0x3f93a0`, mode 4 for LevelType 21), unless flag bit `0x100` is set; then it skips the cell [K].
- **Plan:** set `0x100` on every cell outside the layout, so those cells stay empty (void) [V: the bit's meaning, and how a void edge looks and walks].
- **Fallback:** a one-cell fill ring, or `0x3efa20`, the border-from-outline call the vanilla Act 3 builder makes first [K].
- **Jungle needs no LvlSub pass.** LvlSub goes through D2RCore's TileSub callback `0x3e2b5ec`. Later families may need it.

### 1.5 Rooms

`0x3ee780` turns each piece-origin cell into a preset room [K]. The stamping order sets the room order, which keeps the saved room bits at `+0x258` valid after an unload and rebuild.

### 1.6 Entrance, boss and waypoint

- **Entrance:** a dead-end cell at the start of the graph. The red portal targets its centre, then the nearest free subtile [V: how D2Game's portal picks its coordinates].
- **Boss:** a clearing at the end of the main path. Cartographer spawns the boss at the cell's centre.
- **Event slots:** cells on the main path.
- **No waypoint and no exit warp.** Players leave by town portal or the map portal.

### 1.7 Monsters

D2Game populates the rooms from the slot's levels.txt row, plus each DS1's own preset units [K: the regen spike saw fresh monsters]. Cartographer may write the slot row's mon columns when the map opens. Superuniques are not used. [V] Jungle blocks have `Populate` 1 [K, checked in lvlprest.txt].

## 2. Integration with D2RLoader's D2RCore

**What `0x3271c0` does** [K], in order:

1. reseeds `+0x1e4`;
2. dispatches on `+0x00`; type 3 goes to the D2RLoader stub `0x3e2b5ce` (`JMP [0x3e2ac38]`, into D2RCore);
3. runs the tail: the saved bits at `+0x258`, then the warp-spot scan.

Its entry bytes carry no loader patch.

**Hook `0x3271c0`.** The hook is byte-checked, through the SDK's `InstallInlineHook`. For any level other than this drlg's slot it calls the original. For the slot it does this:

1. Open the seed window: `drlg+0x860` = the map's seed base, with a nested-build guard.
2. Reseed the level (`0x367110(level+0x1e4, …)`).
3. Write the rectangle, then call `0x3ee320`. That call reaches our `0x3fe2e0` hook, then `0x3ee780`.
4. Set `level+0x00 = 0` and call the original `0x3271c0`. No branch matches type 0, so only the native tail runs. Then restore `+0x00 = 3`.
5. Close the window.

D2RCore's `GenerateOutdoorLevel` is never called for the slot, and every other level is untouched.

**Fallbacks:**

- **If the type-0 pass-through is unsafe:** copy the tail (about 30 lines; it calls `0x3daad0` and `0x334ed0`).
- **If the slot is built with no map open:** log it and build a fixed two-block placeholder.

**Probe step** (S0): a `cartographer probe-outdoor` console command, run on the owner's install.

1. Byte-check the entries of `0x3271c0`, `0x3ee320`, `0x3fe2e0`, `0x3ecfc0`, `0x3ee780` and `0x3fa7f0`.
2. Log which module holds `[0x3e2ac38]`.
3. Count calls to `0x3fac10` while entering the Blood Moor. This shows whether D2RCore still calls D2R's per-act builders.
4. Build the slot once with a fixed one-line jungle, and log every room on both drlgs.

If any byte check fails, outdoor maps are turned off and use their `fallback` presets.

## 3. Tileset families

| Rank | Family (LvlTypes id) | Pieces | Edges connect? | Feasibility |
|---|---|---|---|---|
| **1** | Act 3 Jungle (21) | 15 blocks 32×32 = `0x211+mask`; 30 clearings (≤2 exits); Head/Tail 64×32 [K] | Yes, by the mask rule; the native `0x3f0490` does the same | **High: do first** |
| 2 | Act 5 Barricade/Snow (31) | Barricade 1–16 (915–930, 16×16) and Snow 987–1002; cliff/ravine borders; entrance, exit and to-cave pieces; waypoints | [A] the 16 variants look like a 4-bit mask. [V] decompile `0x3fe2e0`/`0x3feb80` | Medium-high once verified |
| 3 | Act 5 Siege (30) | Strips 866–878 (16×48), To Town, To Barricade | A one-dimensional chain only | Medium: straight maps only |
| 4 | Act 3 Kurast (22) | Slums/Burbs/Metro borders and corners (8×8), gates N/S, fill, Temple, Bridge | Bordered districts; gates N/S only | Medium-low |
| 5 | Act 1 Wild (2), Act 2 Desert (16), Act 4 Mesa (27) | Borders, cliffs, fills, named pieces; one open rectangle plus LvlSub | No socket edges; shape would need a custom outline polygon fed to `0x3efaf0`/`0x3ede60` | Low: research spike |

Act 4 Lava and Act 5 Lava are maze tilesets, already steerable as mazes.

**Cross-act tiles in an Act 5 slot level.** The client's act load `0x86bc0` preloads only the DT1s of the current act's LvlTypes rows. The room loader `0x3f3970` adds a room's DT1s from `level+0x1e0` [K]. Act 5 Ice drew fine in Act 1 [K]. Act 3 Jungle in Act 5 is [V] in spike S1. Fallback: hook the end of `0x86bc0` to add the map's LvlTypes DT1s.

**HD.** All the pieces are vanilla, so HD presets should exist [A]. The catalogue tool and the plugin's load check confirm it [V: the HD path for each DS1].

## 4. Data model

**New `maps.txt` columns:**

- `drlg=outdoor`
- `tileset`: the key into `tilesets.txt`
- `slot`: the slot level id
- `cellsize`
- `pathpieces`, `bosspieces`, `eventpieces`, `entrypieces`: piece tags
- `void`: `void`, `fill` or `border`
- `maxsize`: in macro-cells

All values are integers, and keys are case-sensitive.

**`tilesets.txt`, one row per piece:**

- `tileset`, `leveltype`, `lvlprest`
- `w`, `h`: size in macro-cells
- `edgeN`, `edgeE`, `edgeS`, `edgeW`: one socket code per macro-cell along each side: `x` closed, `p` path or river, `c` cliff, `r` ravine
- `role`: `path`, `clearing`, `entry`, `boss`, `event`, `filler` or `border`
- `weight`, `tags`
- `hd`: filled in by the tool

**How the catalogue is made:** the tool `tools/cartographer/catalogue.py` reads `lvlprest.txt` (sizes, files, names). Then:

- **(a)** For families named by mask (`Jungle NSE`, `Lava NS`, the clearings), it turns the names into masks.
- **(b)** It parses each DS1, plus the DT1 subtile walk flags along each side. This classifies the sockets of unnamed families (Barricade 1–16), checks (a), and settles the river-or-path question.

Hand-written `override` rows win over both. The output is committed and diffed in CI.

## 5. Constraint algorithm

1. **Layout graph** (main plan, phase 2): a seeded self-avoiding walk on the macro-cell grid. It follows the shape rules (straightness, turn limit, branches, loops, bounds) and forbids side contact between cells that are not linked.
2. **Piece assignment:**
   - **Mask families** (jungle): a direct lookup per cell. The mask comes from the graph's edges, the role picks the variant, and weights break ties. No search is needed.
   - **Socket families** with multi-cell pieces or typed edges: backtracking with forward checking (MRV and AC-3), in a fixed cell order, with a node budget of about 20k. Wave function collapse adds nothing once the graph fixes most constraints.
3. **Post-check:**
   - path length, turns and dead ends;
   - bounds;
   - required pieces;
   - connectivity;
   - no mask 0.
4. **Retry:** `seed_k = splitmix64(seed ^ k)` for k < 32, then the map's `fallback` preset. Every failure happens in the pure layer, before any engine call; stamping itself cannot fail.
5. **Determinism:**
   - its own PCG32/splitmix random numbers, with no `<random>` distributions;
   - no floats and no unordered iteration;
   - the map seed only, never the level RNG.

   Host and client run the same code from `{map id, seed}`.
6. **Tests outside the game:**
   - the code lives in `plugin/cartographer/layout/`, with no engine headers;
   - a host test exe, with doctest added in `third_party` and run by CTest (the repo has no C++ test harness yet);
   - 10k seeds per test map, checked against the shape rules;
   - golden layout hashes and ASCII dumps;
   - an edge-consistency property: neighbouring cells have equal facing bits.

## 6. Phases (Hell, level 90+ character)

| # | Phase | Size | Done when / game check |
|---|---|---|---|
| S0 | Probe (§2) | 1 d | Byte checks pass on the owner's install; the module behind the thunk is logged; the `0x3fac10` count is known |
| S1 | Spike: a fixed jungle line `[E][EW][EW][EW][W]` in the Act 5 slot | 3 d | Walked end to end; jungle tiles render; automap correct; monsters spawn; host and client room logs equal; leave and return gives the same layout; a new game does not crash; river-or-path answered |
| S2 | Spike: L-shape plus a branch, void bit `0x100`, clearing boss cell, portal target | 2 d | Edges walkable as predicted; void edge acceptable (or fill/border chosen); the portal lands on the entry cell |
| 3 | Catalogue tool, jungle `tilesets.txt`, HD check | 2 d | Tool output matches hand-checked masks for 530–604 |
| 4 | Layout graph, piece assignment, tests | 4 d | 10k seeds per map pass; golden hashes stable |
| 5 | Builder: map columns, hooks, cache, retry, fallback | 3 d | A "Shorelines" jungle map opened in 50 games is within its shape each time, with no crash |
| 6 | Boss, events, multiplayer | 3 d | The boss sits at the end of the path; a two-client LAN check shows the same layout |
| 7 | Spike, then family 2 (Barricade/Snow) | 3–4 d | Mask rule verified; one snow map within its shape |
| 8 | Optional: outline-polygon spike for families 4–5 | 3 d time box | Yes or no on a non-rectangular cliff border |

Total about 24–25 days for jungle plus snow, before families 3–5.

## 7. Risks and open questions

| Risk | Mitigation |
|---|---|
| A D2RLoader update changes the stub or its helpers | Byte checks; bypass D2RCore only for the slot; switch outdoor maps off on a mismatch |
| The jungle mask means river, not path | Use the graph as the river network, with the no-side-contact rule |
| The type-0 pass-through has side effects | Copy the tail instead |
| `0x3fe2e0` can't be hooked | Re-implement the head of `0x3ee320` |
| Void edges are walkable or look wrong | Fill ring, or the `0x3efa20` border |
| A cross-act DT1 is missing | Preload hook on `0x86bc0` |
| The level is too big | A size cap; probe the maximum |
| The slot is built before a map is open | A placeholder layout and a log entry; the slot is never linked |
| A rebuild after an unload differs | A pure, cached layout with a fixed RNG order |
| A client builds before it has the seed | The portal waits for the client's ack, with a resend every 25 frames |

**Still to check, in Ghidra or in game:**

1. Entry bytes and patch state of `0x3fe2e0`, `0x3ee320`, `0x3ecfc0` and `0x3fa7f0`.
2. What `0x3e2b5c8` returns for the slot id.
3. What flag bit `0x100` in grid `+0x58` means, and what sets it.
4. Whether the outdoor-type free handles a null or unusual `+0xb8`/`+0x1d8`.
5. Whether the tail of `0x3271c0` reads the level RNG or `+0x00`.
6. How a portal picks its target coordinates.
7. What the mask means in `Riv*.ds1` (DS1 walk flags).
8. How Barricade 915–930 are picked in `0x3fe2e0`/`0x3feb80`.
9. What the special level id `DAT_142a9a5cc` in `0x3ee320` is.
10. The level-size limit.
11. Whether jungle DT1s preload in Act 5.
12. The HD preset path for each DS1.

## Key references

- `docs/research/drlg-steering.md` (#929) and `docs/research/level-regeneration.md` (#930)
- `plugin/cartographer-steer-spike/plugin.cpp` on `spike/cartographer-steer-proto`: the pattern for the `0x3271c0` hook and its byte checks
- `third_party/PluginSDK/include/D2RLPlugin/hooks.h`
- `d2rAI-toolkit/d2r-data/93847/data/global/excel/lvlprest.txt`: input for the catalogue tool
- Decompiles in `C:\ghidra-work\decomp\`: `0x3271c0`, `0x3ee320`, `0x3ee780`, `0x3ecfc0`, `0x3f0490`, `0x3fd170`
