# Orchestrator handoff, 2026-11-11 evening: end of day 2 into day 3

This branch holds only this note. It is never merged.

The goal, map tiers and plan are unchanged. Read the plan doc on `research/hd-parity-strategy`
(`docs/research/hd-parity-strategy.md`) and the previous note, `handoff/orchestrator-2026-11-11`.

## 1. Start here

1. Run the repo skill **`/orchestrator`** (`.claude/skills/orchestrator/SKILL.md` on main).
2. Read section 3 (open PRs) and section 5 (owner items) before you start any worker. Most of the remaining day-3 lanes
   wait on an owner decision.
3. Model rules (strict, owner-enforced): implementers use **Sonnet, `effort: medium`**; reviewers use **Opus,
   `effort: medium`**. Never swap models, even when one hits a usage limit: report it and wait.
4. Every implementer brief must say:
   - Wait on your own `pd2_merge.py`. Use the Monitor tool (an until-loop on its output) or foreground polls at timeout
     600000. Never hand back mid-run. Only CI waiting is out.
   - Prefix merges with `PD2R_MERGE_SLOTS=6`. The app's own environment still says 4, which leaves agents 2 slots.
   - Lock the slot before any `game_ctl` or install call, and unlock it after.
   - Upload only your own images to `issue-assets`.

## 2. State at handoff

- `main` is **97489f6**. No agents are running. Slot locks were not verified at handoff: check them per
  `docs/agents/game-check.md` before starting a game check.
- **Merged since the last note:**
  - **Gate and infrastructure:** #866, #873, #879, #880, #881, #883, #884, #887, #888, #892, #894, #895, #898, #900
    (hotfix for a main broken by #880 + #883), #904, #908, #918.
  - **Plan:** #875 + #911 (P5 void, #783/#902), #877 (P6 floating, #871), #889 (#856), #905 (P8 low walls, #824),
    #907 (P9 liquid surfaces, #823).
  - **Scorer:** #910 (#906), #917 (#913).
- **Gate:**
  - The ruleset "table diff" (id 27018002) requires `table-diff` and `unit`, with strict OFF and admin bypass.
  - Auto-merge is enabled. `gh pr merge --squash --auto` works.
  - A skipped check no longer passes (#898/#908), and fork PRs now fail `table-diff`.
  - Main push runs cancel the older ones (#904).
- **Merge slots:** the owner set `PD2R_MERGE_SLOTS` = 6, with `PD2R_CI_RESERVED` = 2.
- **Merge time is the bottleneck.** A full merge takes 12-28 minutes under load. CI `table-diff` jobs took 5-36
  minutes today. The rest is the wait for a merge slot: agents reported waits of 40-45 minutes, and agents saw only 2
  slots because the app's environment is stale. With 4 merges at once the CPU is at about 87% across all 16 cores,
  so more slots won't help. The fix is #920: a partial build of only the maps you name. Its
  design is in the status comment on #920, and no code exists yet.

## 3. Open PRs

| PR | Head | State | Next step |
|---|---|---|---|
| #922 (#919) | 04d2727 | CI green, **not reviewed** | Opus review, then merge. Key guard escapes and `run()` re-score write-back. |
| #912 (#830) | 4221630 | Re-review not blocking, CI green | Before merging: merge main, then fix `changelog.d/830-cliff-pass.md`, which says "D2R cliff pieces" but the pieces are Act 3 stone. Owner call: does the low HD rim read as legacy's terrace edge? Images are under `issue-assets` `830/fix1/`. |
| #876 (#829) | 970bd37 | Fix cycles done | **Owner decision:** mesa `chain_walls` (legacy shape, character visible; cliff 62.5/26.2/27.1%) or siege `rockcliff` (100/76.9/76.6%, but tall towers hide the floor and the character). It's a one-line swap in `merge/presets-hd.d/hellcaves-walls.tsv`. Merge main first: #905 landed, and #876's builder says `Option.foot` already covers #905's full-footprint guard. Verify that. |
| #878 (#827) | cbe9f00 | Waits on #876 | After #876: drop its own `hellcaves_base` walls line, since #876's line is global per DT1. Then re-score and run the game check. |
| #914 (#738) | 322a165 | **Blocked** (review https://github.com/synpoox/pd2-resurrected/pull/914#issuecomment-6801379181) | Fix cycle 1: <br>• curves are turned in pseudo-random 30° steps, not toward the corner <br>• rims spill 1-2 tiles onto walkable ground <br>• the lava1 shots show no shore rims <br>• gate river rocks off until they get a game check <br>• merge main; `table-diff` reports 46 drops, partly because the base is stale |
| #915 (#909) | f75f65c | **Blocked** (review https://github.com/synpoox/pd2-resurrected/pull/915#issuecomment-6801385297) | Fix cycle 1: <br>• the hold rule needs an x/z overlap check: 15 Westmarch pieces per map float, 6 with nothing under them <br>• the body should say `Part of #909`, because the fortress roof boards are still dropped by `tall.drop` over known ground <br>• fix the changelog line <br>• merge main and squash out the experiment/revert pair |
| #923 (#916) | fd55789 | **Game check not done** | Don't merge before the game check. The steps are in the status comment on #916: Slot8, `cheat goto 201,<25000+5*tx+2>,<9000+5*ty+2>`, rapids near tiles (78-89,88), (83,72-74) and (108-113,66). Then run an Opus review. Re-check Descend after #914 lands, because its rims may run along the rapids. |
| #607 (#533) | 3a83547 | Draft, parked | Leave it. |

Not reviewed or merged yet: none besides #922.

## 4. Day-3 work left

- **P7 walls:** #876 → #878 → #828 (Hellcave Fortress), all waiting on the owner's kit decision. Then #780 slices.
- **P8 cliffs:**
  - #830 is in #912 (see above).
  - #831 (cliffs at the wrong height on frozenforest, siege, realmoftorment, kyovoshad, pandemonium): WIP on branch
    `cliff-rise-831` (366d205), no PR yet. The new pass `tools/presets/passes/cliff_rise.py` (ORDER 950) lifts D2R cliff
    models by how far they hang, so their base sits on our flat ground. Wrong-height tiles drop from 300-800 per map
    to 0-7. Still to do:
    - fix the new floating on pandemonium (lifted `cliff_support*` pieces)
    - fix the small wrong-floor rise on frozenforestv2/v3
    - run `pd2_parity --check`
    - run the game check (are the 78-unit Siege rocks too tall?)
    - open the PR

    Status is in issuecomment-6801623719 on #831.
  - #832 (Baal lair underwall) is **parked** until #876 lands. Data-only `walls:` lines lower parity. It needs cliff
     cells in `pick` to be allowed to touch walkable tiles (full footprint for blocking, core for coverage). Findings
     are on #832.
- **P9 liquids:** #914 (blocked), #923 (game check).
- **P6 follow-up:** #915 (blocked).
- **P10 calibration:** sweep 1 is done (33 variants of the 12 CP2 maps on main ec6d88e). The sheets and parity numbers
  are on **#921** (`ready-for-human`). Once the owner grades them 1-5, fit the parity weights (Spearman ≥ 0.8).
- **Sweep 2:** after P5/P6/P7 land, per the plan's day 3. The other non-Palace variants have not been swept yet.
- **Gate work:** #920 (partial merge build, design ready, no code). #922 is open.
- **Follow-ups filed today:**
  - #885 (done via #918)
  - #896 (folder-types check compares the recomputed vote)
  - #901 (done via #908)
  - #903 (done via #904)
  - #906, #909, #913 (done or in flight)
  - #919 (in #922)
  - #920
  - #921
  - #905's review follow-ups F1-F4 (in its review comment): new tall pieces in ruinedcistern_01, poisonedwell_01 and
    djinnsdomain_00, which may come from the tall fallback. Not filed as an issue yet.
  - #910's swamp-mound note (water-typed mounds hold trims, as on main). Not filed yet.

## 5. Owner items

1. **#876 kit:** mesa or rockcliff (section 3).
2. **#912 look:** accept the low Act 3 stone rim on Travincal and Kurast, or not.
3. **#921:** grade the 12 CP2 sheets 1-5.
4. **Semantic merge conflicts:** with strict OFF, #880 + #883 broke main (fixed by #900). Options: a merge queue, or a
   strict unit check in a separate workflow. Your call.
5. **#895:** delete the empty reviewer comment (issuecomment-6795527418).
6. **Disk:** delete `C:\pd2r-p14-cache` and `C:\pd2r-iso861` / `iso861b` with `cmd /c rmdir /s /q` (never
   `Remove-Item -Recurse`).
7. **Stray process:** the #914 reviewer's cancelled merge (PID 33436) may still be running. Its kill was blocked, so it
   exits on its own, or you can run `taskkill /PID 33436 /T /F`.
8. **`issue-assets` history:** about 36 stray PNGs were pushed to `902/` in commit f45e43d, then removed. They remain
   in that branch's history. Leave them, or rewrite the branch if you want them gone.
9. **App environment:** restart the Claude app so agents inherit `PD2R_MERGE_SLOTS=6`.

## 6. Hazards

- **Never use `git worktree remove`, `rm -r` or `gh pr merge --delete-branch`.** They follow the `pd2-data` junction into
  `C:\pd2r-cache`. Remove worktrees only with `tools/worktree_remove.ps1`. Merge with `--squash` (or `--auto`), then
  `git push origin --delete <branch>`.
- **Stacked PRs:** retarget the child to main before deleting the parent's branch. #914 lost its base this way and was
  retargeted.
- **Semantic conflicts:** with strict OFF, two green PRs can break main together. After any main breakage, merge main
  into the open PRs instead of rerunning failed jobs: a rerun reuses the old merge commit.
- **Stale parity baselines:** PRs based before #910/#917 show false parity drops (43 maps) until they merge main.
- **Never start or stop the runners.**
- **Branches never to delete:** `research/*`, `issue-assets`, `docs/warp-locations`, `handoff/*`.
- **Worktrees:** `.claude/worktrees/` holds many finished `agent-*` worktrees from today. Prune the ones without open
  PRs with `tools/worktree_remove.ps1`, keeping C: above 30 GB free (203 GB free at the sweep). The branches of the open
  PRs are `fix-919`, `cliffs-830`, `walls/829-lucion`, `walls/827-hellcaves`, `liquids/738-edges`,
  `issue-909-held-decals` and `descend-rapids-water-916`. Keep `cliff-rise-831` (WIP) and `agent/920-partial-build`
  as well.
- The stopped #830 worker left `agent-a31f3aadd8ee48eb2`, a partial folder that is not a git worktree. Check it for a
  `pd2-data` junction before you delete it.
