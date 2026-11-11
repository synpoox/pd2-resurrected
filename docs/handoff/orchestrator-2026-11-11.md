# Orchestrator handoff and plan, 2026-11-11: end of day 1 (gate bottleneck work)

This branch holds only this note and is never merged.

The goal and map tiers are unchanged. See `handoff/orchestrator-2026-11-10-night` and the plan doc on
`research/hd-parity-strategy` (`docs/research/hd-parity-strategy.md`).

## 1. Start here

1. Run the repo skill **`/orchestrator`** (`.claude/skills/orchestrator/SKILL.md`, on main).
2. Finish the gate bottleneck queue (section 3) **before** you open day-2 lanes. Every lane rebases onto main, and each
   generator change still costs a ~10 min presets-hd rebuild per gate run until #807 and #861 land.
3. Then start day 2 (section 4).

## 2. State at handoff (2026-11-11 ~08:30 UTC)

- `main` is **efb4042**.
- **Agents:** none running. **Slots:** 2-11 all free.
- **Merged today:** #837 (docs, one gate rule), #848 (gate skips docs-only, cancels merged/closed runs), #845 (CI replays
  unchanged rules from the shared cache, #820), #846 (single-flight per rule key, #821), #853 (never
  `--delete-branch`), #842 (parity sweep harness, P4), #841 (override layer, P3), #851 (stock-vote typing, P1 #822),
  #857 (sweep: abandon a slot whose lock was lost, #850), #858 (nightly cache-verify, #849), #864 (override add op
  docs, #856).
- `PD2R_MERGE_SLOTS` = 4 machine-wide (set by the owner).
- **Gate state:** cached rules replay in ~4 s. The one heavy step left is the `presets-hd` rule (all 135 DS1, ~10 min),
  which rebuilds whenever the generator or its inputs change. Docs-only PRs (except `docs/deviations.md` and
  `docs/coverage-baseline.tsv`) merge at once without CI (AGENTS.md "Gate rule").

## 3. Open PRs, in merge order

| PR | Head | State | Next step |
|---|---|---|---|
| #859 (#844) | ce6d6e6 | table-diff + unit **green**, reviewed | **First retarget #865 to `main`** (`gh pr edit 865 --base main`): deleting #859's branch closes any PR based on it. Then squash-merge #859 and delete its remote branch. Its merge forces one cold rebuild of every rule (new CACHE_FORMAT); expect one slow main run. |
| #865 (#808) | 284cd25 | reviewed, stacked on #859 | After #859: rebase onto main, wait for green, merge. Toolkit commits then rebuild ~1.5 rules on average instead of ~35. Issue #808 is "Part of": close it if the review agrees nothing remains. |
| #860 (#826) | 91b5d6b | unit green, table-diff was still running | Reviewed (fix cycle done: a `walls:` line fills wall, cliff and pillar cells). Retarget #863 to `main` first, then merge. |
| #863 (#862) | 5ae35a1 | stacked on #860 | After #860: rebase, gate, merge. Open question before #827/#828/#829: `wall_px` uses the file median (MIXED below 25%), so Lucion asks low on 1 of 98 cells. Decide whether that is acceptable. |
| #843 (#838) | 417171b | fixes pushed, gate running | Needs a cycle-2 review (Opus, `isolation: worktree`). When it merges, retire the `coverage-drop-ok` label (the owner approved its one use on #851). |
| #868 (#855) | 0ad5868 | needs review | Generator imports `pd2_tiles`, not the scorers, so scorer edits no longer rebuild presets-hd. `generated/` is byte-identical (SHA-256 b621cf7a… before and after). Conflicts only with `edges_walls` import lines from #860: rebase after #860. |
| #866 (#807) | cc233f4 | **draft** | Presets rule in parallel worker processes. Needs CPU-time and wall timings plus an identical-output SHA on a quiet machine (no other merges running) before it leaves draft. Its last gate run was cancelled to free a runner. |
| #867 (#854) | cf9d396 | **draft** | Coverage scores cached per map. Needs verification (fresh vs cached report identical). The parity half of #854 remains. |
| #607 (#533) | 3a83547 | draft, parked | Leave it. |

## 4. Day-2 plan (unchanged order, after section 3)

1. **Bottleneck finish:** #861 (presets rule cached per DS1, so a one-area change rebuilds one area). The design is
   posted at https://github.com/synpoox/pd2-resurrected/issues/861#issuecomment-6790721142. Implement it after #866,
   since it shares the worker split.
2. **P5 #783** void: draw empty space where legacy draws none.
3. **P6** floating pieces: no issue yet. Open one, folding in #722 and #721.
4. **P7 walls:** #829 (Lucion Arena, uber tier) first, then #827 (Hellcaves) and #828 (Hellcave Fortress). Each needs
   #860 and #863 on main.
5. **Sweep 1** of all pinned variants on main with the #842 harness (`pd2_edge_tour sweep`) on Slot4-8, ~1 h. Post
   contact sheets. #840 stays open until that sweep runs.
6. **P10 calibration:** the owner grades 12 contact sheets 1-5, and the parity scorer (#843) is calibrated against them.

## 5. Other open tickets

- **#849:** close after the first green nightly `cache-verify` run (04:17 UTC; it has not run yet).
- **#856:** the override add op is too small to show at its shipped scale (jungle1 rock). Needs a bigger proof object.
- **#834, #815, #819:** cleanup follow-ups, low priority.

## 6. Hazards

- **Never `git worktree remove`, never `gh pr merge --delete-branch`.** Both follow the `pd2-data` junction into
  `C:\pd2r-cache\extract` and empty the shared cache. This happened today (restored with `py -3 tools/pd2_extract.py`).
  Merge with `gh pr merge --squash`, then `git push origin --delete <branch>`. Remove worktrees only with
  `tools/worktree_remove.ps1`.
- **Stacked PRs:** retarget the child to `main` before you delete the parent's branch, or GitHub closes the child.
- **One cold rebuild after #859.** Don't read the slow main run as a regression.
- **Runner `pd2r-6` is offline.** The owner manages the runners: report it, never start or stop one.
- **Measure speed in CPU time** (`time.process_time`), not wall time under load.
- **Worktrees left in `.claude/worktrees/`:** 24 `agent-*` worktrees from today. Those with open PRs:
  `perf/808-per-rule-toolkit-keys`, `perf/presets-parallel-807`, `walls-class-862`, `fix/844-cache-package-imports`,
  `perf/854-coverage-cache`, `refactor/pd2-tiles-855`, `parity-scorer-838` and `walls-rule-826`. The rest are finished,
  so prune them with `tools/worktree_remove.ps1`, keeping C: above 30 GB free. `agent-afe20471ea04c141c` holds
  `research/hd-parity-strategy`: never delete that branch.
- **Branches never to delete:** `research/*`, `issue-assets`, `docs/warp-locations` and `handoff/*`.

## 7. Owner items

- Delete `C:\pd2r-p14-cache` (a leftover from the #820 prototype).
- Runner `pd2r-6` is offline.
- Grade the 12 P10 contact sheets once sweep 1 posts them.
