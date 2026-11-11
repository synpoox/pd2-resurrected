# Orchestrator handoff and plan, 2026-11-10 (night): HD:legacy map parity

This branch holds only this note and is never merged.

**The goal:** each PD2-specific map's floors and edges (walls, cliffs, treelines, water, fences, pillars) match
legacy closely in height, visuals, spacing, design and type, built from D2R pieces. The PD2 Palace (Horadrim
Tower) is unplayable: leave it out of map work.

## 1. Start here

1. Run the repo skill **`/orchestrator`** (`.claude/skills/orchestrator/SKILL.md`). It holds the roles, the review
   cycles, the merge rule and the finished-worker rules.
2. Work section 4 top to bottom. Every ticket below is labelled `ready-for-agent` and linked under its epic with
   real sub-issue and blocked-by links.
3. Give each map ticket the warp list: branch **`docs/warp-locations`**, `docs/agents/warp-locations.md`. It holds
   `cheat goto` lines for all 64 PD2-specific maps (134 DS1 variants). It isn't merged; read it with
   `git show origin/docs/warp-locations:docs/agents/warp-locations.md`.

## 2. State at handoff

- `main` is **9665a1e**. Open PRs: only #607 (draft, parked with #533; leave it).
- **Agents:** none running. **Slots:** all free.
- **The workflow blockers are cleared**:
  - merge speed: #818, so the presets rule now takes 47% of its old CPU time;
  - CI and agents share one merge cap: #817;
  - a coverage tolerance of 3 points with a drop classifier: #812;
  - screenshot rules in game-check.md: #811;
  - presets-hd split into `merge/presets-hd.d/`, one file per tile set: #816;
  - the skill's worker rules: #814.
- **The owner's game** (`C:\Games\Diablo II Resurrected Infernal Edition`) has main a0ae2bc installed. The old
  `filter.json` was backed up beside it as `filter.json.bak-2026-11-10`. That build's `modkit build` reported 62
  validation errors (tracked by #171). The installer treated them as main's existing state and installed anyway;
  no earlier build was compared.

## 3. Where parity stands (main a0ae2bc gate, Palace out, 128 DS1 variants)

All edge kinds: **62.7%** of 296,512 legacy edge tiles (1 point ≈ 2,965 tiles).

| Kind | Score | Tiles lost | Biggest losses |
|---|---|---|---|
| cliff | 11.0% | 27,301 | riverofblood terraces, descend, frozenriver, kyovoshad/realmoftorment (wrong height), throne, hellcaves |
| wall | 79.5% | 23,978 | arcanemap (0%), demonroad, tomb, hellcaves; 8.7k at the wrong height |
| treeline | 72.2% | 20,205 | frozenforest `waypt.dt1` (3.7k), tristmap `town/trees`; 13.7k spacing holes |
| water | 55.4% | 17,241 | ruinedcistern oasis, darktemple (0%), descend |
| fence | 35.5% | 10,998 | demonroad chain walls, pandemonium foregate |
| pillar | 47.7% | 10,765 | mesa, descend (act1 stones), torturehalls, tristmap |
| floor | 98.6% (presence only) | 5,711 | |

Coverage: hellcaves 2.8%, lucionarena 3.0%, hellcavefortress 20.5%, uberdiablolvl 22.7%, and mesa1-6 at 33-43%.

**Visibly wrong in game, but not scored:**
- **Liquid surfaces:** only 47.3% of 176,751 legacy liquid floor tiles are under a water or lava piece.
- **View blocking:** about 26.6% of walkable floor sits under tall HD pieces (the Kurast/jungle canopy).
- **Lighting:** Tomb and Dark Temple are at 55-70% of legacy brightness.
- **Floor material:** one biome per tile set.
- **Void floors, ground-step height, and white fences.**

**The scorer is the first bottleneck.** It types HD pieces by folder name and mistypes about 20k tiles (6.7 points).
Typed by D2R's own stock presets instead, edges go from 62.7% to 69.4%. Until that lands, gate drops and fidelity
numbers mislead workers.

## 4. The plan, ranked (gain per effort)

Lanes can run in parallel only where the files don't overlap. Each row names the files it touches.

| # | Ticket | Gain | Blocked by | Files / lane |
|---|---|---|---|---|
| 1 | **#822** scorer: stock-evidence typing, per-tile liquid, drop Palace, report-only occlusion and liquid-interior buckets | measurement (+6.7 pts reclassified) | none | `pd2_fidelity.py`, `pd2_coverage.py`. **Land alone, first**: it moves main's baseline |
| 2 | **#826** (#692a) per-DT1 `walls:` rule | builds the rule for #827-#829 and #780 | none | `presets/apply.py`, `passes/edges_walls.py`. Parallel with #1 |
| 3 | **#827 / #828 / #829** Hellcaves, Hellcave Fortress, Lucion Arena walls | about 4.7k tiles, +1.6 pts | #826 | one `presets-hd.d` file each; parallel with each other |
| 4 | **#739** treelines (fold in #779): frozenforest `waypt` tree tile first (about 3.9k), then tristmap, ashenplains, spacing holes | +2.5-3 pts | #822 | `pd2_painter.py`, `passes/trees.py`, `passes/border.py`, `layout.py` |
| 5 | **#830 / #831 / #832** (#735 slices) cliffs: terraces; wrong height; throne underwall | +3 pts or more | #822 | new `passes/edges_cliffs.py`; parallel with #4 |
| 6 | **#823** liquid surfaces (oasis, dtprivate, fallengarden, rivbank, descend, aer_canal) | about 90k surface tiles, +1.7 pts of edges | #822 | `water.tsv`, `presets/liquids.py`; parallel with #4 and #5 |
| 7 | **#738** water and lava edges. Code exists on `synpoox/738-liquid-edges` (no PR yet) | +1 pt, cheap | #823 | `passes/edges_liquids.py` |
| 8 | **#824** walls at the wrong height (low front walls) | up to +2.9 pts | #692 | `passes/edges_walls.py`; after #826-#829 |
| 9 | **#783** void floors, plus Arcane walls (#802) | +1.2 pts (arcanemap at 0%) | #822 | `passes/ground.py` and the walls pass; after #824 |
| 10 | **#825** Descent hell-area kit (broadens #721) | about +2 pts | #692 | descend's `presets-hd.d` files |
| 11 | **#780** LP4 wall stand-ins, one slice per area | per area | #826 | per-area `presets-hd.d` files (see #819) |
| 12 | **#740** ground tiles (branch `synpoox/740-ground-tiles`; #702, #703 and #720 hang under it) | visual: floor material | a floor-material measure (consider adding it to #822) | `passes/floor_ground.py`; conflicts with #783 and #764 |
| 13 | **#777 / #805** pillars | +1.5 pts | #822 | `presets/pillars.py`, `place.py` |
| 14 | **#804** lighting follow-ups, **#767** fence material and bridge rails | visual | none | lighting, `fence_kit.json`, `edges_fences.py` |

**Suggested first wave:** #822 and #826 in parallel. Once #822 lands: #739, #830, #823 and #827-#829 side by side.
Then #738, #824, #825 and #831/#832.

**Process tickets to fit in early:**
- **#833** docs: one gate rule, stale facts, reviewer worktree isolation, one merge per commit, CPU-time speed
  measure. Docs-only.
- **#834** code cleanup: ruff in CI, the `pd2_coverage` globals, unused imports, edge-tour output out of `out/`.
- Merge speed, in order: **#809** (second-round hot spots, with the #818 leftovers in its comments), then **#807**
  (parallel DS1 builds; blocked by #809), **#808** (cache key by the code that runs), **#821** (single-flight
  cache, so concurrent merges of one commit share work) and **#820** (CI replays the rule cache).
- **#815** and **#819** are small follow-ups from #812 and #816.

## 5. Questions for the owner

1. **#734 / #737 / #773:** their main PRs landed. Are the parents done under the "match legacy" bar, or do they stay
   open until #802, #805/#777 and #804 close?
2. **#702 / #703:** their 90% floor-coverage bar is met (lava 93-96%, fortress 98-99%). Close them, or keep them under
   #740's material goal?
3. **#738's blocker #768:** what is left is item 4 (a `coverage-drop-ok` call) and item 5 (a game check). Drop the
   blocker?
4. **#797:** in stock presets, `act2_maggot_stalagmites` stand on walls (426 of 513 tiles). Is the premise still right?
5. **Game check vs PR order:** the skill says push, open the PR, then game check; a later rule says code, game check,
   then the PR. Which one stands? (#833 item 5.)
6. **Non-map issues the audit flagged:**
   - #592: did the uber one-shot reproduce? If not, close it.
   - #85: close it, and keep #120 standalone?
   - #358: still wanted, given `docs/deviations.md:28`?
   - #339 Guilds and #300 XP bar: close them?
   - #208: still waiting for your plugin?
   - #224/#225: move them out of #143?
   - #447: still wanted?
   - #611: unpark it, since #610 is closed?
7. **`PD2R_MERGE_SLOTS`:** not set in the runners' environment, so CI uses the default of 4. Set one machine-wide
   value (`setx`, then restart the runners).

## 6. Done this session (issue hygiene)

- Closed #792 (solved) and #686 (superseded by #734).
- Added 20 sub-issue links (most map follow-ups were "Part of #707" in text only) and moved #702/#703 from the closed
  #695 to #740.
- Added blocked-by links: #213 ← #734 and #739, and #807 ← #809.
- Opened the parity tickets #822-#832 and the process tickets #820, #821, #833 and #834.

## 7. Hazards

- **Never remove a worktree with `git worktree remove`.** It follows the `pd2-data` junction and once emptied
  `C:\pd2r-cache\extract\2c99b00fbdb4f95d` this session; the agent re-extracted it. Use only
  `tools/worktree_remove.ps1`, and say so in every brief.
- **Reviewers need `isolation: worktree`.** One reviewer without it ran in the orchestrator's checkout and moved its
  HEAD.
- **Don't run two full merges of the same commit at once.** They don't share work yet (#821); reuse one worktree's
  `generated/`.
- **Time merge speed in CPU time** (`time.process_time`), not wall time: the load from 4 merge slots plus CI skews it.
- **Runner `pd2r-6` is offline.** The owner manages the runners: report it, never start or stop it.
- **Slot8's install may be stale.** Reinstall it before use.
- **Worktrees left in `.claude/worktrees/`:**
  - `agent-a1626d6c099b06d6a` and `agent-a41926f9bd3af0c21` are still locked by their finished agents' processes;
  - four finished auditor/reviewer worktrees: `agent-a144a17e66b4681e1`, `agent-a61bb6365e50a7ae7`,
    `agent-a9bca1b3aee8dd9b2`, `agent-ae9c6d461b585906a`;
  - `autocompact-300k-c604f1`, `pd2-resurrected-issue-89-9b011b`, `pd2-resurrected-pr-queue-b28b28` and
    `orchestrator-handoff-review-7b0ddd` belong to other sessions.
  Remove any of them only with `tools/worktree_remove.ps1`, and only when no process holds them.
- **Branches never to delete:** `research/*`, `issue-assets`, `docs/warp-locations` (owner reference), and this
  handoff branch.
