# HD:legacy map parity: a strategy for one week

Research for the owner, 2026-11-10, on main 9665a1e. This branch (`research/hd-parity-strategy`) is never merged and
never deleted. Prototype scripts and their outputs are in `docs/research/hd-parity-strategy/`.

**The goal:** each PD2-specific map (64 maps; the Palace is out) looks like legacy. Its floors and edges should match
legacy in height, visuals, spacing, design and type, built from D2R pieces.

## 1. Verdict

**Yes: generation plus per-map overrides can reach parity, but not for all 64 maps in one week.** A realistic week
looks like this:

1. **Every map gets better through generator fixes.** Three systemic defects make up most of what the eye sees:
   - void drawn as ground;
   - floating pieces;
   - wall cells drawn as nothing.

   Fixed in the generator, they lift the prototype's mean parity from **63.5% to about 84%** over all 128 scored
   variants (a what-if score with those checks left out). Void alone is worth +15.6 points and floating +3.5.
2. **About 20 maps get tuned to owner approval:** the top of the order in section 7. That is five lanes at about
   1.5 maps per lane-day over days 4-6, plus the maps already close. The other ~44 maps end the week with the
   generator fixes, a parity number, a contact sheet and a ranked defect list, but without owner approval.
3. **The bottleneck is the owner's review, not capture or merges.** One full screenshot sweep of all variants takes
   about 1 to 1.5 hours on 5 slots. The owner can only grade so many contact sheets a day.

**The current scores don't match the eye, and this research confirms it.** On main, floor coverage reads 98.6%, yet:

| Defect (visible tiles, all 128 variants) | Tiles | Share |
|---|---|---|
| void: legacy draws nothing, HD draws ground or a piece | 329,792 | 23.7% |
| edge: missing, wrong type or wrong height (pd2_fidelity) | 99,839 | 7.2% |
| floating: a piece's base 1+ units over the ground plane | 44,559 | 3.2% |
| invisible: a legacy wall cell with nothing drawn in HD | 42,581 | 3.1% |
| clutter: a prop on open walkable floor where legacy has nothing | 14,538 | 1.0% |
| solid: a 4+ unit piece standing on open walkable floor (a passable wall) | 9,641 | 0.7% |
| occlusion: a raised 4+ unit piece (canopy) over open walkable floor | 7,155 | 0.5% |
| liquid: legacy liquid floor with no water or lava model | 5,790 | 0.4% |

"Visible" here means within 7 tiles of walkable floor: 1,392,818 tiles in all.

**Two caveats, stated plainly:**
- **The scorer is a prototype.** The owner flags Jungle and Kurast as visually off, yet the prototype puts them at
  82-84% and 74-76%. The checks catch floating ferns and canopy, but not "the wrong kind of object" or ground
  material. Its weights must be calibrated against the owner's grades on day 2 (ticket P10). Until then the owner's
  eye stays the authority, and the number is a ranking aid.
- **Map count.** There are 128 distinct non-Palace DS1 files with a generated preset. The warp list counts 134
  variants for 64 maps because it lists a DS1 under each level that loads it.

## 2. A score that matches the eye

Prototype: `docs/research/hd-parity-strategy/parity_score.py`. It reuses `pd2_coverage` (model boxes, the prefab
walk, ground planes) and `pd2_fidelity` (legacy edge classes). It scores all 128 variants in about 110 s, with
images.

### Checks

Every check works per DS1 tile (10 HD units). A piece covers a tile through `coverage.core_footprint`, at least half
the tile both ways, so a box that only grazes a tile doesn't count. Heights are measured from the flat ground plane
under the piece (`coverage.plane_ground`). Only tiles within `SIGHT` = 7 tiles of legacy walkable floor are scored:
what the camera can show.

| Check | A tile is bad when | Notes |
|---|---|---|
| **edge** | a legacy edge tile is missing, the wrong type or the wrong height (`pd2_fidelity.score_type`) | Keeps the existing per-kind scorer; #822 swaps in better HD typing (below) |
| **solid** | open walkable floor (no legacy edge on or beside it) lies under a piece 4+ units tall with its base on the ground, not a tree | A wall or rock you walk through |
| **clutter** | open walkable floor lies under a grounded prop 1-4 units tall, not ground cover (grass, ferns) | The "objects on floor where legacy has nothing" |
| **occlusion** | open walkable floor lies under a 4+ unit piece whose base is raised (canopy, overhangs) | Replaces #822's report-only occlusion bucket |
| **floating** | a piece's base is 1+ units above the plane (not trees, lights, chains, banners, water surfaces, or pieces of pasted stock rooms) | Tiles under its core |
| **invisible** | a legacy wall cell (not `inv_wall`, not liquid) has no HD piece of 1+ units | The "passable the other way": you bump into air. Legacy blocked floor drawn as plain ground is left out, since it looks walkable in legacy too |
| **void** | legacy draws nothing (no floor, no wall), but HD draws a ground plane or a piece | #783's bucket |
| **liquid** | legacy liquid floor has no water or lava model over it | Replaces #822's liquid-interior bucket |

**Collision can't mismatch, by design.** HD presets are visual only, and D2R takes collision from the DS1 (ADR 0005).
So the "collision mismatch" the strategy asked for is a visual-versus-collision mismatch, and two checks score it:
`solid` (HD looks blocked where legacy walks) and `invisible` (HD looks open where legacy blocks).

**Edge typing by stock evidence (#822).** A quick vote in the prototype (`--vote`) typed each HD folder by the legacy
edge its pieces stand on in D2R's 2,041 stock presets. Unguarded, it retyped 499 of 712 folders, mostly wrongly:
- tomb vases and Travincal gore hug walls in stock, so they voted "wall";
- dead trees and act2 big cliffs voted "floor".

Even guarded (an explicit folder name wins; the vote needs 60% of 50+ tiles), it made edge defects worse (99.8k to
115k). **#822's own prototype (`stockvote.py`, +6.7 points) is the reference.** Keep #822 as it is, and plug its
typing into the edge check.

### One number per map

`parity = 1 - Σ w(t) · [t bad in any check] / Σ w(t)` over the visible tiles. A tile counts once, however many
checks flag it. The weight w is:
- 3 on a legacy edge tile (what the eye follows);
- 2 on walkable floor (a defect there misleads play);
- 1 elsewhere (scenery, void).

There is no coverage term. Drawing more pieces can't raise the number: extra pieces show up as clutter, solid,
occlusion, void or floating. Drawing fewer shows up as edge, invisible or liquid.

The number comes with:
- per-check counts;
- a defect overlay (`<map>_parity.png`; colours in the script);
- the worst check, so a reviewer sees why a map scores low.

**Calibration (ticket P10).** On day 2 the owner grades 12 contact sheets 1-5. We fit the three weights, SIGHT and
FLOAT so the parity order matches the grades (Spearman ≥ 0.8). Then the gate uses the number. If it can't match,
the missing checks get added (see the gaps below).

**Gaps the eye sees and the prototype doesn't:**
- **foreign kit:** pieces from another act than the tile set's, like Tristram houses in Descent (#721, #825). This
  is cheap: count each map's pieces whose folder act differs from the tile set's act;
- **floor material:** #740;
- **lighting:** the harness's luma metric covers it.

### Sample maps, scored the new way

Overlays are beside this doc: `tomb1_parity.png`, `jungle1_parity.png` and `ruinedcistern_parity.png`.
- red: edge; orange: solid; cyan: invisible; purple: void; yellow: floating; blue: occlusion; pink: clutter;
  teal: liquid;
- green: right; dim: other drawn tiles.

| Map | Visible tiles | Parity | edge | solid | invisible | void | floating | occlusion | clutter | Old scores (fidelity) |
|---|---|---|---|---|---|---|---|---|---|---|
| **tomb1** (Tomb Map) | 9,835 | **44.7%** | 680 | 41 | 283 | **5,398** | 142 | 4 | 28 | wall 57.9%, pillar 4.0%; floor coverage ~99% |
| **jungle1** (Jungle Map) | 16,370 | **84.1%** | 212 | 166 | 25 | 99 | **1,076** | 479 | 401 | wall 73.0%, treeline 93.7%, water 100% |
| ruinedcistern | 10,726 | 62.5% | 2,270 | 66 | 55 | 1 | 987 | 119 | 199 | wall 64.6%, water 3.3% |

- **tomb1.** The old floor score said "done". In fact the ground plane runs under all the black void around the
  corridors (purple in the overlay), and the south and east walls are missing (the red rim). With void, floating
  and invisible fixed by the generator, tomb1 scores 86.2%.
- **jungle1.** 1,076 tiles of floating pieces, along with canopy and props on open floor. The floating folders are
  `act3_cinchona` 717, `act3_ferns` 560, `act3_jungle_debris` 212 and `act3_jungle_rocks` 202. The pieces are
  kit and prefab pieces copied at their stock height: a cypress prefab stands at y = 10.0 over a plane at 0.
  That is a generator bug repeated across maps, not an override job.

### Why the order changed

The new number ranks maps differently from wall/floor coverage. For example:
- Pandemonium (39%), Throne (43%) and Tomb (44%) are void-dominated;
- Ruined Cistern and Dark Temple were floating-dominated until water surfaces were exempted; they are now 62% and
  66%, edge-dominated.

The whole ranking is in section 7.

## 3. Screenshot harness

### What already exists

- **`tools/pd2_edge_tour.py` (#733)** does most of it:
  - `list` plans `cheat goto` points per edge or floor type;
  - `shoot` warps through the toolkit's `game_ctl.py` (console, `g`, window shots) and saves HD|legacy side by
    side.
- **The toolkit's `testing/`:**
  - `game_slots.py`: lock, unlock and status;
  - `game_ctl.py`: start, stop, shot, key, click (in fractions of the window) and console;
  - `game_checks.py`: a check registry, results JSON and screenshots.
- **`pd2_force_ds1.py`** pins a variant.
- **`knowledge/cheat-commands.md`** has the cheats: `goto`, `god`, `nogethit`, `speedup`, `killmonster`,
  `monfreeze`, and `stat item_lightradius`.
- **What's missing:**
  - a scripted way into a Hell game;
  - a loop over pinned variants;
  - points chosen from the parity defects;
  - contact sheets;
  - image metrics.

### Measured on Slot4 (main's install as found, Hell, PdPaladin level 99)

| Step | Time |
|---|---|
| `game_ctl start` until the process is up | 13.8 s |
| menu ready, `click 0.394 0.898` (Play), then `click 0.5 0.543` (Hell), until the console answers | about 15 s to the menu, then 10.1 s |
| `pd2_edge_tour.py shoot`, 12 points (warp, 3 s settle, shot, `g`, shot, `g`) | 104.3 s: **8.7 s per pair** |
| `game_ctl stop` | 4.2 s |

**Variant lesson, measured.** The tour was not pinned, and Jungle rolled jungle3. So 8 of the 12 pairs (the jungle1
and jungle2 points) landed off the drawn map: legacy 68-70% black. **Pin every variant; never shoot unpinned.**

**Estimate per variant:**
- stop 4 s, pin 2 s, start and menu 30 s, enter 12 s, cheats 5 s: about 53 s;
- 10 pairs at 8.7 s: 87 s;
- **about 2.3 min per variant.**

| Throughput | Time |
|---|---|
| 128 variants on one slot | about 4.9 h |
| On 5 slots (Slot4-8; reinstall Slot8 first) | about 1 h of capture, plus about 10 min per slot to install a build (estimated) |
| A day | 3 or more full sweeps, or one sweep per merge batch |

**So 5 slots cover all variants within a day, with room to spare.**

### Design (ticket P4)

Extend `pd2_edge_tour.py` with a `sweep` command, rather than adding a new tool:
1. **Shard the variants** over the free slots (`game_slots.py status`). One process per slot locks it, installs
   the build (`install.ps1 -NoValidate`) and loops over its variants: stop, `pd2_force_ds1`, start, enter Hell,
   the cheats, then the points.
2. **Get into the game** with `game_ctl click` at the Play and Hell buttons (fractions measured above, for
   1920x1080). Then wait for the console to answer.
3. **Cheats:** `nogethit 1`, `god 1`, `speedup 300`, and `stat item_lightradius, 25` for structure shots. In the
   measured run the frames were still dark: check the stat took effect, and add `killmonster` before each shot.
4. **Points:** about 10 per variant:
   - 2 fixed references (centre, waypoint or entry, from the warp list);
   - about 8 at the worst defect clusters of the parity overlay, worst first, spread apart.
5. **Shots:** HD shot, `g`, legacy shot, `g` back (#795). Lighting shots are separate: 3 frames, no light-radius
   stat, then the median.
6. **Outputs:**
   - per variant, `coverage/sweep/<map>/<ds1>_<n>.png`;
   - per map, one contact sheet JPEG with metrics (`contact_sheet.py` here builds them; see
     `jungle-map-contact-sheet.jpg`);
   - a `sweep.json` index.
   PNGs are pruned after the sheets are made. A sweep is about 5 GB of PNGs; keep C: above 30 GB.
7. **Close out:** stop, `--reset` the pin, and unlock every slot. This runs in a `finally`, so a crash can't leave
   a slot locked.

### Image metrics

| Metric | Status | How |
|---|---|---|
| luma (lighting) | **reliable** | Mean luma of the playfield: HUD cut off, legacy cut to its 4:3 picture. HD/legacy should be about 0.85-1.15. The prototype shows jungle3 at 0.95-1.31. Use the median of 3 frames (#804). |
| black share (void) | **reliable as a cross-check** | The share of near-black pixels. Legacy void reads black; HD ground there lowers the share. It confirms the void check in game. |
| walkable overlap, edge masks | **not reliable yet** | The two cameras frame different areas: legacy's view is narrower and its projection differs. A pixel overlap first needs a world-to-screen fit per resolution: warp to 4 known tiles and fit the affine (ticket P4b, optional). Until then those are judged by eye. |

The Poisoned Well hangs when you toggle to legacy (#772). The sweep skips legacy shots there and says so.

## 4. Per-map override layer

### Format and placement

Prototype: `docs/research/hd-parity-strategy/preset_overrides.py`, with an example in `example/` and the merge wiring
in `pd2_merge-wiring.patch`.

- **Anchor `merge/presets-overrides.tsv`** (no lines); the ops go in **`merge/presets-overrides.d/<family>/<ds1
  stem>.json`**. It is not `preset-overrides/`: the toolkit's merge driver runs rule files in name order
  (`sorted(rules_dir.glob("*"))`, files only). `preset-overrides.tsv` would sort *before* `presets-hd.tsv` and run
  before the presets exist; `presets-overrides.tsv` sorts after it. A folder per map family gives each lane its own
  files.
- **It is its own pd2_merge rule** (`RULES["presets-overrides.tsv"]`). It reads the presets-hd rule's output in
  `generated/`, applies the ops, and writes the presets back. It is not wired into `tools/presets/apply.py`, for
  three reasons:
  - with its own rule, an edit to an override reruns only this cheap rule: the 9-minute presets rule replays from
    the cache;
  - the override code stays out of the `tools/presets/` package, whose files are all in the presets rule's cache
    key;
  - the gate's uncached merge (#820) applies the overrides like any other rule, so the parity score always measures
    presets with their overrides.
- **File format:**

```json
{"ds1": "jungle1",
 "ops": [
  {"op": "ground", "area": [30, 160, 50, 190], "model": "*/act3_jungle_debris/*", "why": "sticks float 6-7 units"},
  {"op": "ground", "at": [39, 181], "model": "*/pf_act3_tree_cypress04_01.json", "expect": 1, "why": "prefab floats 10 units"},
  {"op": "ground", "area": [80, 160, 99, 179], "model": "*/act3_ferns/*"},
  {"op": "add", "at": [39, 181], "from": "data/hd/env/preset/act3/jungle/boge.json", "pick": "*/act3_jungle_rocks/rock_pile*"},
  {"op": "remove", "at": [0, 0], "model": "*/no_such_piece.model", "why": "deliberately stale: shows the lint"}
 ]}
```

- **Selectors:**
  - `at` (one DS1 tile) or `area` (a box of tiles), by the entity's top-level position;
  - `model` (a glob over model and prefab paths) and `name` (a glob);
  - `pos`: the generator's x, z within 0.01 units, a stable identity;
  - `expect`: how many entities the op must match.

  Ids and array order are **never** selectors. An editor renumbers ids, and the generator's ids shift with every
  change.
- **Ops:**
  - `remove`;
  - `ground`: snap to the plane under the piece (the floating fix);
  - `move`: `by` a vector, or `to` a tile;
  - `swap`: replace the model or prefab path; `from` merges in the dependencies of the stock preset that holds it;
  - `add`: copy a stock entity at a tile centre, on the ground, with a fresh id, and merge that preset's
    dependencies;
  - `entity`: an inline entity, as written by `diff`.
- **Lint:**
  - An op that matches nothing, or not `expect` entities, is **stale**. The merge prints it, and the rule writes
    `generated/presets-overrides-report.tsv` (under `generated/` so the rule cache keeps it, outside `data/` so it
    never ships).
  - `preset_overrides.py lint` exits 1 on any stale op. The gate runs it after the merge, so a stale op fails the PR.
  - A file for a DS1 with no generated preset fails the merge, like a stale `presets-hd.d` line.

### End-to-end prototype result (jungle1)

The rule was wired in locally (the patch), the example was put in `merge/`, then `pd2_merge.py` was run three times:

| Run | Result |
|---|---|
| 1 (op 1 aimed at `rock_pile01` on tile 39,181) | **the lint caught it:** `STALE op 1 remove ... matched 0, expected 3`. The rocks there belong to a cypress **prefab**, so the op was re-aimed at the prefab. |
| 2 | `presets-overrides: jungle1: 5 ops, entities 17940 -> 17941, 1 stale` (the deliberate one). `rule cache: 34 of 35 rules replayed; ran presets-overrides.tsv`; **the merge took 5 s.** |
| 3 (nothing changed) | `rule cache: 35 of 35 rules replayed; ran none`. |
| `preset_overrides.py roundtrip before after` | `21 ops, 0 stale, replay equals the edited preset: True`. Diffing the merged preset against the generated one gives ops that rebuild it exactly. |
| Parity of jungle1 | floating 1,098 to 1,091 tiles; parity 83.6% to 83.7% |

**What the parity line shows:** 21 entity edits barely move a systemic defect. Overrides are for per-map leftovers.
Repeated patterns belong in the generator (section 5).

**The HD preset JSON diff** (`jungle1.json`, the entity array in order; dependencies unchanged, because the added
model was already listed):

```diff
 {"type":"Entity","name":"pf_act3_tree_cypress04_01_01", "components":[{"type":"TransformDefinitionComponent",
-  "position":{"x":398.35582,"y":10.000611305236816,"z":1811.4674}, ...
+  "position":{"x":398.35582,"y":0.0,"z":1811.4674}, ...
 {"type":"Entity","name":"stick01_..." ... "position":{"x":384.825,
-  "y":6.523, ...
+  "y":0.0, ...                         (8 debris pieces, 11 ferns alike: y -0.65 to -10.0)
+{"type":"Entity","name":"pd2r_override:rock_pile03_02","id":17941,"components":[{"type":"TransformDefinitionComponent",
+  "position":{"x":395.0,"y":0.0,"z":1815.0}, ...},{"type":"ModelDefinitionComponent",
+  "filename":"data/hd/env/model/global/prop/act3/jungle/act3_jungle_rocks/rock_pile03.model", ...}]}
```

### External editor (placeholder)

A separate researcher looked at `reimagined-level-editor` (MIT, C#/.NET WPF, Windows only). Their summary:
- Agents can't drive it: it has no CLI or headless mode.
- The owner could use it by hand to move, delete or place models in a generated preset, with `out/data` as its
  workspace.
- Its preview isn't faithful enough to replace the in-game harness.
- Never ship its `granny2.dll`.

**The override format is ready for it.** `preset_overrides.py diff` (and `roundtrip`, to verify) turns any edited
copy of a preset into ops, keyed by model and position, never by id or array order. The editor re-serialises the
whole file and assigns random ids.

The full capture tool, `tools/pd2_override_capture.py`, is optional ticket P12. It would:
- ignore `.rle-*.json` sidecars and `.bak` files;
- write `presets-overrides.d/<family>/<ds1>.json`.

An owner trial on one map is optional ticket P13. The orchestrator merges the researcher's findings into this
section.

## 5. Map-by-map tuning at scale

### Rules

1. **Generator first, then overrides.** A defect pattern seen on **3 or more maps** goes to the generator:
   - floating kit pieces;
   - void ground;
   - empty wall cells;
   - a tile set's wrong kit.

   Anything else is an override. Generator fixes land on days 2-3, before any override work, so overrides don't go
   stale under them; the lint catches any that do.
2. **One lane per map family.** Each lane owns `merge/presets-overrides.d/<family>/`, so lanes don't conflict. A
   lane that finds a pattern on 3+ of its maps files a generator ticket, and doesn't patch it 3 times.
3. **Batch the PRs.** Each lane opens one override PR per day. That keeps the CI gate's full merges near 6 a day,
   well under 4 merge slots.
4. **The owner approves from contact sheets.** Agents don't decide "looks like legacy".

**Families**, by tile set (`pd2_only_presets`):

| Lane | Maps |
|---|---|
| **A: Act 3** | Jungle, Kurast, Zhar 3-Kurast, River Of Blood, Ureh City, Lucion Vault, Nemyr Monastery, Necropolis Jungle and Swamp, Spider, Sewers (11) |
| **B: Act 2** | Tomb, Tomb Event, Imperial Palace and Boss Room, Zhar Main, Arcane, Desert, Ruined Cistern, Caldeum, Dark Temple, Poisoned Well (11) |
| **C: Act 4 and hell** | Mesa family (Mesa, Diamond Gates, Necropolis Void, Pandemonium, Pit Of Despair, Realm Of Torment, Void Event), Lava, Black Abyss, Sanctuary of Sin, Descent, Demon Road, Hellcaves, Hellcave Fortress, Lucion Arena, Halls of Torture, Uber Diablo (17). Split into C1 (Mesa family, 8) and C2 (the rest, 9) when a sixth worker is free |
| **D: Act 5** | Fallen Gardens, Throne, Zhar 1-Rivers, Frozen Forest, Ice, Zhar 2-Ice, Fortress, Siege, Na-Krul's Abyss, Kyovoshad (10) |
| **E: Act 1 and the rest** | Westmarch, Ashen Plains, Kanemith Dungeon and Outside, Library, Monastery and its Basement, Crypts, Pandemonium Finale, Graveyard, Skovos, Djinns Domain (3 maps) (14) |

### Evidence before the owner sees a map: the map card

A map goes to the owner only with all of these, posted as one issue comment:
1. **Parity** now vs main: the number, the per-check counts and the overlay PNG. No check worse than main by more
   than the tolerance.
2. **The contact sheet** of a pinned sweep on the PR's build:
   - every variant;
   - 2 reference spots and about 8 worst-defect spots per variant;
   - quiet spots (`killmonster`) with the light-radius stat;
   - plus one lighting frame set with luma HD/legacy.
3. **The override file's ops**, each with its `why`. `lint` is clean, and so is the gate.
4. **A "not fixed" list:** the remaining worst clusters with tile coordinates, so the owner can say "fine" or
   "fix".

The owner answers per map:
- **approve:** it gets the `parity-approved` label;
- **fix:** with the spots named;
- **park.**

## 6. Tickets

Lanes: **S** = scorer, **O** = override layer, **H** = harness, **G** = generator, **T** = tuning lanes A-E,
**I** = infrastructure.

| # | Ticket (maps to) | Goal | Done when | Files | Blocked by | Lane |
|---|---|---|---|---|---|---|
| P1 | **#822** as written | HD typing by stock vote, liquid per tile, drop the Palace | as in #822; its two report-only buckets move into P2 | `pd2_fidelity.py`, `pd2_coverage.py` | none | S, day 1 |
| P2 | **New: parity scorer** (absorbs #783 item 3 and #822's buckets) | `tools/pd2_parity.py` from `parity_score.py`: 8 checks, one number per map, overlays, a baseline artifact | in the gate's report and step summary. It gates on a parity drop past the tolerance; coverage stays as an information column, not a gate | new `tools/pd2_parity.py`, `tools/tests/test_parity.py`, gate workflow | P1 (rebase) | S, days 1-2 |
| P3 | **New: preset override layer** | `presets-overrides.tsv` rule, ops, lint, `diff` and `roundtrip` | unit tests cover each op and stale detection; the anchor doc is written; the gate fails on a stale op; one real override proven in game | new `tools/pd2_overrides.py`, `merge/presets-overrides.tsv`, `pd2_merge.py` RULES, gate | none | O, day 1 |
| P4 | **New: parity sweep** (absorbs **#795**, **#747**) | `pd2_edge_tour.py sweep`: 5 slots in parallel, pinned variants, scripted Hell entry, defect-driven points, contact sheets, luma and black share | one full sweep of 128 variants on main in under 2 h, with all slots unlocked | `pd2_edge_tour.py`, `docs/research/.../contact_sheet.py` moved to `tools/` | none (P2 for the defect points; it falls back to plan points) | H, days 1-2 |
| P4b | New, optional: world-to-screen fit | pixel overlap and edge-mask metrics | the fit is within 1 tile at 4 spots | `pd2_edge_tour.py` | P4 | H, day 5+ |
| P5 | **#783, re-scoped** | no ground plane or piece on legacy-void tiles, or a stock abyss piece where legacy shows a pit | void defects fall at least 80% across all maps; pairs posted for Tomb, Throne and Arcane | `passes/ground.py`, `pd2_terrain.py` | P2 (measure) | G, days 2-3 |
| P6 | **New: floating kit pieces** (folds **#722**, the floating beams of **#721**) | kit and prefab pieces copied at stock height stand on the plane, unless stock hangs them (an allowlist of hanging folders) | floating falls at least 80%; jungle1, Westmarch and River Of Blood pairs posted | new `tools/presets/hooks/grounded.py` or the height pass | P2 | G, days 2-3 |
| P7 | **#826, then #827/#828/#829 and #780 slices** | wall cells drawn as nothing get stand-in walls | invisible falls at least 60%; Hellcaves, Hellcave Fortress, Lucion Arena, Imperial Palace and Frozen Forest pairs posted | `edges_walls.py`, per-area `presets-hd.d` files | none | G, days 2-4 |
| P8 | **#824, #830, #831, #832** | edge height and type: wall heights and cliffs | as written | `edges_walls.py`, new `passes/edges_cliffs.py` | P1 | G, days 3-5, only if a worker is free; else after the week |
| P9 | **#823**, then **#738** | liquid surfaces and edges | as written, measured by P2's liquid check | `water.tsv`, `liquids.py`, `edges_liquids.py` | P1 | G, days 3-4 |
| P10 | **New: parity calibration** | the owner grades 12 sheets 1-5; weights and thresholds are fitted | Spearman ≥ 0.8 against the grades, or the missing check named and added (foreign kit first) | `tools/pd2_parity.py` | P2, P4 | S, day 2 |
| P11A-E | **New: tuning lane A/B/C/D/E** (one per family) | per-map overrides plus map cards to owner approval | each map is owner-approved or parked, with its card posted; parity ≥ 90% where approved (to be confirmed by P10) | `merge/presets-overrides.d/<family>/` only | P3, P4, P5, P6 | T, days 4-6 |
| P12 | New, optional: `tools/pd2_override_capture.py` | turn an external editor's saved preset into ops | `roundtrip` exact on one edited map; ignores `.rle-*.json` and `.bak` | new tool | P3 | O, day 4+ |
| P13 | New, optional: owner trial of the level editor on one map | try authoring by hand | one override captured from the owner's edit | none | P12 | owner |
| P14 | **#820** (CI replays the rule cache), then **#821** (single-flight) | gate speed for a week of PRs | as written | `pd2_merge.py`, `pd2_cache.py`, gate | none | I, days 1-2 |

**What to do with the existing tickets:**

| Action | Tickets |
|---|---|
| **Close when P4 lands** | #795, #747 (folded in) |
| **Re-scope** | #822: typing only; its buckets go to P2. #783: the void fix only; its scorer bucket goes to P2. #707: done when every map is owner-approved from a map card or parked with a reason. #722 and #721: fold the floating parts into P6; #721's houses become lane E/C2 work under #825. |
| **Keep, inside the plan** | #826-#829, #780 (P7); #823, #738 (P9); #824, #830-#832 (P8); #825 (lane C2's kit fix); #820, #821 (P14). The epics #734-#740 and #773 stay open as parents. |
| **Park until after the week** | #740 ground material (it needs a floor-material measure; conflicts with P5 in the ground pass); #764 ground steps (same file as P5); #739 treelines (the parity edge check shows treelines mostly right; revisit after P10); #777/#805 pillars; #763 swamp; #768 crust follow-ups; #766; #730; #804 (except that the harness luma feeds it); #809/#807/#808 merge speed (#818 already halved it). |

## 7. Order and the 7-day plan

### The order maps get tuned

Priority = (100 - parity after the generator fixes) × walkable tiles per run / 1000. That is the gap left for
overrides, times how much of the map a run walks: a proxy for play value until the owner gives tiers. Top 25:

| # | Area | Variants | Parity now | After generator fixes (what-if) | Walkable per run | Lane |
|---|---|---|---|---|---|---|
| 1 | Frozen Forest Map | 3 | 61.8% | 68.5% | 3,794 | D |
| 2 | Ruined Cistern Map | 3 | 62.1% | 64.8% | 3,353 | B |
| 3 | Ureh City Map | 1 | 65.5% | 78.0% | 5,183 | A |
| 4 | FallenGardens | 1 | 64.2% | 70.0% | 3,656 | D |
| 5 | Dark Temple Map | 3 | 66.2% | 77.7% | 4,373 | B |
| 6 | Hellcaves | 1 | 45.6% | 75.2% | 3,893 | C |
| 7 | Graveyard Map | 3 | 76.5% | 83.8% | 5,938 | E |
| 8 | Descent Map | 4 | 68.8% | 70.9% | 3,024 | C |
| 9 | HellcaveFortress | 1 | 54.5% | 74.3% | 3,417 | C |
| 10 | Mesa | 6 | 59.3% | 89.0% | 7,369 | C |
| 11 | Demon Road Map | 4 | 74.1% | 76.3% | 3,218 | C |
| 12 | Pandemonium Map | 3 | 39.4% | 75.8% | 2,705 | C |
| 13 | Siege Map | 2 | 73.2% | 83.4% | 3,900 | D |
| 14 | Ice Map | 3 | 81.4% | 82.6% | 3,502 | D |
| 15 | Kyovoshad Map | 2 | 48.6% | 82.0% | 3,290 | D |
| 16 | Kanemith Dungeon | 1 | 56.6% | 72.9% | 2,146 | E |
| 17 | Kurast | 3 | 74.8% | 87.1% | 4,512 | A |
| 18 | Realm Of Torment | 3 | 46.6% | 83.9% | 3,597 | C |
| 19 | River Of Blood | 4 | 68.6% | 77.5% | 2,475 | A |
| 20 | Arcane | 2 | 65.1% | 65.8% | 1,499 | B |
| 21 | Sanctuary of Sin Map | 3 | 56.8% | 85.2% | 3,359 | C |
| 22 | Kanemith Outside | 1 | 70.9% | 83.0% | 2,862 | E |
| 23 | Djinns Domain Map | 1 | 59.2% | 73.4% | 1,817 | E |
| 24 | Fortress Map | 3 | 81.0% | 84.2% | 2,973 | D |
| 25 | Westmarch Map | 2 | 46.2% | 83.1% | 2,736 | E |

The full list of all 63 scored areas is in `docs/research/hd-parity-strategy/priority.md`.

**The owner's own flags go first, whatever their score.** Jungle Map (now rank 38), Tomb (35) and Kurast (17) join
the first tuning batch. They also anchor the calibration: if the owner rates Jungle low while the scorer says 92%
after the fixes, the scorer is missing a check.

### Day by day

The workers are W1-W5 (Sonnet). The orchestrator reviews and merges, with Opus reviewers in worktree isolation.
Never more than 4 merges at once.

| Day | W1 | W2 | W3 | W4 | W5 | Owner checkpoint |
|---|---|---|---|---|---|---|
| **1** | P1 #822 (lands alone, first) | P2 scorer, branched from this prototype | P3 override layer | P4 sweep: scripted entry, pinned loop, sheets; reinstall Slot8; install main on Slot4-8 | P14 #820 | **CP1 (evening):** approve this plan and the scorer design; give map tiers (play value), or accept walkable size as the proxy |
| **2** | P5 void (#783) | P2 lands after P1; baseline on main | P6 floating | **Sweep 1** on main: 128 variants, sheets for every map | P7 #826, then #827 | **CP2:** grade 12 sheets 1-5 (Jungle, Tomb, Kurast, Ruined Cistern, Frozen Forest, Hellcaves, Mesa1, Throne, Dark Temple, Graveyard, Sewers, Lava) for P10 |
| **3** | P5 lands; pairs posted | P10 calibration fit; the foreign-kit check if needed | P6 lands | P4 polish: defect-driven points; **sweep 2** after P5 and P6 | P7 #828, #829, then #780 slices | **CP3:** the before/after sheets of the three generator fixes (void, floating, walls), one per family. Approve or send back |
| **4** | lane A (Act 3) | lane B (Act 2) | lane C (Act 4 and hell) | lane D (Act 5) | lane E (Act 1 and the rest) | **CP4 (evening):** the first map cards, about 5 maps (one per lane: Ureh, Ruined Cistern, Hellcaves, Frozen Forest, Graveyard) |
| **5** | A | B | C | D | E, plus a sweep of the changed families (whoever finishes a PR first) | **CP5:** about 7 maps, the fixes from CP4 included |
| **6** | A | B | C | D | E | **CP6:** about 8 maps |
| **7** | final sweep of all 128 variants on main; the parity report | lanes close: unfinished maps get a card and a `ready-for-agent` ticket | P8 or P9 only if lanes finished early | | | **CP7:** the final approvals, the week's report (parity per map, approved list, parked list), and the next week's order |

**Running totals:** about 20 maps approved by day 7, at roughly 5 + 7 + 8 across CP4-CP6. The rest have a card and
a ticket.

**Rules that keep it moving:**
- Generator PRs land one at a time per file area:
  - P5 owns `ground.py`;
  - P6 owns hooks and placement;
  - P7 owns `edges_walls.py` and the per-area files.
- Override PRs only touch their own family folder. A lane that needs a generator change files it, and doesn't make
  it.
- Each PR rebases on main and reruns the gate on its final commit (AGENTS.md).
- Game checks follow `docs/agents/game-check.md`: lock, install, pin, shoot, then unlock in a `finally`.
- If the same blocker hits twice, stop and fix its cause (CLAUDE.md).

## 8. Risks and fallbacks

| Risk | Sign | Fallback |
|---|---|---|
| The scorer doesn't match the owner's eye (Jungle 84% but "off") | CP2 grades vs parity: Spearman < 0.8 | Add the foreign-kit and canopy-density checks; reweight. Until it matches, the owner's grades rank the maps and the number only gates regressions |
| The void fix shows black holes where HD needs an abyss (bridges, balconies) | CP3 sheets of Arcane and Throne | A stock abyss or void piece where legacy shows a pit (#783 option 2); keep ground under bridge decks |
| The floating fix lowers pieces meant to hang (beams, lanterns, cloth) | CP3 sheets; P2's floating check flags 0 while the eye sees sunken pieces | An allowlist of hanging folders taken from stock (a piece whose stock base is over its stock ground at its stock spot) |
| Scripted entry into the game breaks (UI moves, a dialog, a different resolution) | the sweep stalls at the menu | A screenshot check before each click (`game_ctl shot` plus a mean-luma test); fall back to `key enter` presses; at worst the owner starts each slot once and the sweep goes on from the game |
| Warp points land off the map or in a wall | legacy black share over 60% | Points only on walkable tiles from the variant's own layout, with the variant pinned (measured: unpinned wastes 2/3 of the shots) |
| The Poisoned Well hangs on `g` (#772) | a frozen slot | Skip legacy shots there; HD only, compared with an owner-supplied legacy reference |
| Gate load: every override PR costs a full uncached merge until #820 | gate queue waits | P14 on day 1; batch override PRs to one per lane per day |
| Overrides go stale when a generator fix lands | `lint` fails on stale ops | Generator fixes land on days 2-3, before the lanes start; a lane re-aims its stale ops in its next PR |
| Owner review bandwidth | cards waiting more than a day | Cards grouped by family, at most 8 a day, with the worst defects first on each sheet |
| Disk (5 slots and sweep PNGs) | C: under 30 GB | Keep JPEG sheets only; prune PNGs after each sheet; slots move to F: as planned |
| Slot8 is stale | the install differs | Reinstall it on day 1 (W4) |

## 9. Files on this branch

All under `docs/research/hd-parity-strategy/`:
- `parity_score.py`: the scorer prototype. Run it from the repo root after `tools/pd2_merge.py`:
  - `--maps a,b` scores only those maps;
  - `--tsv` writes one line per map;
  - `--skip` gives what-if numbers;
  - `--vote` turns on the stock-vote typing.
- `preset_overrides.py`: the override rule prototype, with `lint`, `diff` and `roundtrip`.
  - `pd2_merge-wiring.patch` wires it into `RULES`;
  - `example/` holds `presets-overrides.tsv` and `presets-overrides.d/jungle/jungle1.json`; copy them to `merge/`
    to reproduce.
- `contact_sheet.py`: a contact sheet with luma and black share from edge-tour pairs.
- `jungle-map-contact-sheet.jpg`: the measured Slot4 run (12 pairs, unpinned; jungle3 loaded).
- `tomb1_parity.png`, `jungle1_parity.png`, `ruinedcistern_parity.png`: defect overlays.
- `parity-main-9665a1e.tsv`, `priority.md`: every variant's parity on main, and the tuning order.
