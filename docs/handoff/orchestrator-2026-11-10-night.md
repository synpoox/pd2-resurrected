# Orchestrator handoff and plan, 2026-11-10 (night): HD:legacy map parity in one week

This branch holds only this note and is never merged.

**The goal:** each PD2-specific map looks like legacy in HD: its floors and edges match in height, visuals, spacing,
design and type, built from D2R pieces. The PD2 Palace (Horadrim Tower) is out.

**The owner's verdict (2026-11-10):** we are not close visually, despite the scores. Coverage is irrelevant if parity
is off. Everything the eye judges must be scored: clutter, floating or passable objects, void, occlusion, liquid
surfaces and material.

## 1. Start here

1. Run the repo skill **`/orchestrator`** (`.claude/skills/orchestrator/SKILL.md`).
2. Read the plan: branch **`research/hd-parity-strategy`**, `docs/research/hd-parity-strategy.md` (commit 918fc8a).
   Its tickets are P1-P14 (section 6), followed by the day-by-day plan (section 7). Its prototypes are in
   `docs/research/hd-parity-strategy/`. This note is the short form; that doc decides.
3. Every map ticket uses the warp list, now on main: `docs/agents/warp-locations.md` (generator `tools/pd2_warps.py`).
4. Work section 3 below. First get the owner's day-1 checkpoint: approve the plan and give map tiers.

## 2. State at handoff

- `main` is **e295720**. Open PRs: only #607 (draft, parked with #533; leave it).
- **Agents:** none running. **Slots:** all free (Slot8 needs a reinstall before use).
- **The workflow blockers are cleared:**
  - merge speed: #818, so the presets rule takes 47% of its old CPU time;
  - CI and agents share one merge cap: #817;
  - a coverage tolerance of 3 points: #812;
  - game-check screenshot rules: #811;
  - presets-hd split into `merge/presets-hd.d/`: #816;
  - the skill's worker rules: #814;
  - the warp list: #836.
- **The owner's game** (`C:\Games\Diablo II Resurrected Infernal Edition`) has main a0ae2bc installed. Its `filter.json` was
  backed up beside it as `filter.json.bak-2026-11-10`. That build's `modkit build` reported 62 validation errors (#171).

## 3. The plan in short

**The verdict:** parity is reachable with generation plus a per-map override layer, but not for all 64 maps in a week.
A realistic week ends with:
- every map lifted by three generator fixes;
- about 20 maps tuned to owner approval with overrides;
- the other ~44 each with a parity number, a contact sheet and a ticket.
**The owner's review is the bottleneck.**

**Where it stands, on the new parity score:** the mean is 63.5% over the 128 non-Palace DS1 files; Tomb is at 44.7%,
while its floor coverage reads 98.6%.
- Three systemic defects carry most of what the eye sees:
  - void drawn as ground: 23.7% of the visible tiles;
  - floating pieces: 3.2%;
  - legacy wall cells drawn as nothing: 3.1%.
- Fixing them lifts the mean to about 84% (a what-if).
- The scorer still misses wrong-theme objects and floor material: Jungle scores 84% while the owner calls it off. So
  calibrate it against the owner's grades on day 2.

| Day | Work (P-numbers from the plan doc) | Owner checkpoint |
|---|---|---|
| 1 | P1 #822 (lands first), P2 parity scorer, P3 override layer, P4 sweep harness (+ reinstall Slot8), P14 #820 | approve the plan; give map tiers |
| 2 | P5 #783 void, P6 floating fix, P7 #826/#827 walls; sweep 1 of all variants on main; P10 calibration | grade 12 contact sheets 1-5 |
| 3 | generator fixes land; sweep 2 | before/after sheets of the generator fixes |
| 4-6 | P11A-E: five family lanes, each owning its own `merge/presets-overrides.d/<family>/` folder, one PR a day | daily map cards (about 5, then 7, then 8 maps) |
| 7 | final sweep and report | final approvals; next week's order |

- **Lanes:** each lane owns its own override folder, so lanes never conflict. A pattern repeated across 3 or more maps
  goes to the generator instead.
- **Tuning order:** worst gap times walkable size: Frozen Forest, Ruined Cistern, Ureh City, Fallen Gardens, Dark Temple,
  Hellcaves, Graveyard, Descent, Hellcave Fortress, Mesa. Jungle, Tomb and Kurast join the first batch: the owner
  flagged them.
- **Override layer:** `merge/presets-overrides.d/<family>/<ds1>.json`, with anchor `merge/presets-overrides.tsv`. The
  rule name sorts after `presets-hd`. Ops are add / remove / move / swap / ground. Entities are selected by tile, area,
  model or name, or by their generated `pos`, never by id or array index. A stale op fails the lint.
- **Harness:** about 2.3 min per pinned variant, so 128 variants take about 1 h on 5 slots. Every variant must be pinned:
  an unpinned tour wasted 8 of 12 shots.
- **External editor:** `D2R-Reimagined/reimagined-level-editor` (MIT, Windows GUI). Agents can't drive it, but the owner
  could hand-edit a map in it. P12 (`tools/pd2_override_capture.py`) and P13 (an owner trial) are optional. Never ship
  its `granny2.dll`.

**Re-scope, close or park, per the plan:**
- Re-scope #822 to typing only and #783 to void only, and rewrite #707's done-condition. Fold #722 and #721 into P6.
- Close #795 and #747 when P4 lands.
- Park until after the week: #740, #764, #739, #777/#805, #763, #768, #766, #730, #804 (except its luma measurement), and
  #807-#809.

## 4. The owner's decisions (2026-11-10)

- #734, #737 and #773 stay open.
- Coverage is not a metric unless it visually represents legacy. #702 and #703 stay open under #740.
- #738's blocker #768 is dropped. #797's premise stands.
- **The order for every ticket:** push, open the PR, then game check while CI runs, attaching images to the PR. Never
  merge unchecked. (#833 aligns the docs.)
- **Non-map issues:**
  - #592 is untested, so it stays open;
  - #85 is closed, and #120 is standalone;
  - #358 is labelled owner-review;
  - #339, #300, #208, #224 and #225 stay;
  - #447 is wanted, ideally building from origin's `synpoox/d2rAI-toolkit`;
  - #611 is unparked, at low priority.
- `PD2R_MERGE_SLOTS` is still unset on the runners, so CI uses the default of 4. Ask the owner to set one machine-wide
  value.

## 5. Other open tickets from this session

- **#833** docs: one gate rule, stale facts, reviewer worktree isolation, one merge per commit, CPU-time speed measure.
- **#834** code cleanup: ruff in CI, `pd2_coverage` globals, unused imports, edge-tour output out of `out/`.
- **#821** single-flight rule cache (concurrent merges of one commit share work).
- **#815** and **#819**: follow-ups of #812 and #816.

## 6. Hazards

- **Never remove a worktree with `git worktree remove`.** It follows the `pd2-data` junction into `C:\pd2r-cache\extract`
  and once emptied it. Use only `tools/worktree_remove.ps1`, and say so in every brief.
- **Reviewers need `isolation: worktree`.** One reviewer without it moved the orchestrator checkout's HEAD.
- **Don't run two full merges of one commit at once** (#821). Reuse one worktree's `generated/`.
- **Measure speed in CPU time** (`time.process_time`), not wall time under load.
- **Runner `pd2r-6` is offline.** The owner manages the runners: report it, never start or stop it.
- **Worktrees left in `.claude/worktrees/`:**
  - `agent-a1626d6c099b06d6a` and `agent-a41926f9bd3af0c21` are still locked by finished agents' processes;
  - finished auditor, reviewer and research worktrees (`agent-*`);
  - `autocompact-300k-c604f1`, `pd2-resurrected-issue-89-9b011b`, `pd2-resurrected-pr-queue-b28b28` (which has the local
    `handoff/orchestrator-2026-11-10` branch checked out) and `orchestrator-handoff-review-7b0ddd` belong to other
    sessions.
  Remove any of them only with `tools/worktree_remove.ps1`, and only when no process holds them.
- **Branches never to delete:** `research/*` (including `research/hd-parity-strategy` and `research/pd2-merge-speed`),
  `issue-assets`, `docs/warp-locations` (owner reference), and this branch.
