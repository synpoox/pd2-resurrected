# Orchestrator handoff, 2026-11-11 late: Cartographer moved to d2r-tools

This branch holds only this note and is never merged. It is pushed to both synpoox/d2r-tools (where Cartographer now lives) and synpoox/pd2-resurrected. It supersedes `handoff/orchestrator-2026-11-11-pm` for the
**Cartographer** workstream only. The HD-parity lanes (P7–P10, PRs #876 #878 #912 #914 #915 #922 #923 #607) were not
touched; their state is still in `handoff/orchestrator-2026-11-11-day2`. Keep both branches.

## 1. What happened this session

1. Filed the Cartographer epic and five wave-1 sub-issues, and launched five Sonnet implementers (A–E).
2. E (phase 0 docs) finished; PR #941 was reviewed by Opus, one fix cycle applied.
3. The owner asked to build Cartographer in **synpoox/d2r-tools** instead, since it is a standalone plugin. All
   workers were paused at a safe point (no game started, no slot locked, all work pushed).
4. Migrated: issues transferred, docs PR opened in d2r-tools, WIP code copied to d2r-tools branches, #941 closed.
5. **Owner's instruction: no further Cartographer work started in this session.** Nothing is running.

## 2. Where Cartographer lives now: synpoox/d2r-tools

Read d2r-tools' `AGENTS.md` and `CONTRIBUTING.md` before dispatching. They differ from pd2-resurrected:

- One folder per plugin, `plugins/cartographer/`, with its own `CMakeLists.txt`; plugins share no code.
- CI (`ci.yml`) builds **every** folder under `plugins/`, and fails on one without a `CMakeLists.txt`. So the docs sit
  in `docs/cartographer/` until phase 1 creates the plugin folder.
- Addresses: byte signatures with a per-part guard (fail open), no version gates; record every one in
  `docs/reverse-engineering/cartographer.md`.
- Build and test: `tools\build.ps1 -Plugin cartographer -Test` (VS 2022, CMake 3.29+). Worktrees are sibling folders
  `..\d2r-tools-<topic>`, branches `<plugin>/<topic>`; no `pd2-data` junction, plain `git worktree remove` is fine.
- Changelog: the plugin's own `CHANGELOG.md` under `## Unreleased` (no `changelog.d/`). No table-diff gate.
- Outward-facing steps (push, PRs, issues, comments) need the owner's ask; **the owner granted standing permission
  for Cartographer workers** (§4.3).
- Game checks: vanilla D2R + D2RLoader with a test save (no PD2 build, no core-maps to switch off).

| Item | Where |
|---|---|
| Epic | [d2r-tools#19](https://github.com/synpoox/d2r-tools/issues/19) (was pd2-resurrected#935) |
| Phase 1 skeleton | [d2r-tools#20](https://github.com/synpoox/d2r-tools/issues/20) (was #936): no code yet |
| Phase 2 layout graph | [d2r-tools#21](https://github.com/synpoox/d2r-tools/issues/21) (was #937): WIP `cartographer/layout-wip` 937ee81 |
| Outdoor S0/S1 spike | [d2r-tools#22](https://github.com/synpoox/d2r-tools/issues/22) (was #938): WIP stays at pd2-resurrected `spike/cartographer-outdoor-s1` b78cfe2 |
| Catalogue tool | [d2r-tools#23](https://github.com/synpoox/d2r-tools/issues/23) (was #939): WIP `cartographer/catalogue-wip` cc35536 |
| Phase 0 docs | [d2r-tools#24](https://github.com/synpoox/d2r-tools/issues/24) (was #940): [PR d2r-tools#25](https://github.com/synpoox/d2r-tools/pull/25), head 62b7274 |

Issue bodies were rewritten for d2r-tools paths and rules; each opens with a "Moved from pd2-resurrected" note.
The `ready-for-agent` label does not exist in d2r-tools, so the moved issues have none.

## 3. State of each piece

- **PR d2r-tools#25 (docs only):** plans (copied from `docs/map-kit-plan` at 785935e), README draft, table
  reference, ADR (renumbered `adr-0001`), glossary, and the RE stub with 18 addresses (all "needs a byte signature,
  not validated"). Carries the #941 review fixes. CI builds were still running at handoff. Next: merge (docs only).
  The RE stub omits `0x3e77a0` (maze add-room) and `0x32c6a0` (LvlMaze lookup) from the steer prototype; add them.
- **Layout graph WIP (#21):** `rng.h`, `layout.h`, `layout.cpp` and doctest 2.4.11 (downloaded from doctest's GitHub
  by the worker; confirm the owner is fine with that vendored file). Never compiled. Left: `shapes.txt` parser
  (declared, unimplemented, so it does not link), CMake, the 10k-seed tests, compile and tune. Ring maps put the boss
  opposite the entrance (n/2), the event room at about n/4.
- **Catalogue WIP (#23):** `catalogue.py`, never run. It imports pd2-resurrected's `tools/presets` DS1/DT1 readers,
  which must be brought into the plugin. Finding from scratch scripts: jungle masks 530–544 match the DS1 walk flags
  exactly; each exit is a ~12-tile walkable corridor with a 2-tile river inside, so the mask means **walkable exits**
  (path), pending the in-game answer in #22.
- **Outdoor spike (#22):** probe and slot-build plugin written, never compiled, no game run. Key finding:
  `0x3ee320` reaches `0x3fe2e0` only when D2RCore's act lookup `0x3e2b5c8` returns 4 for the slot id; if not, the
  fallback (re-implement the head of `0x3ee320`) is needed. Slot id 202 was the next free id in PD2's levels.txt;
  recheck against the vanilla table in d2r-tools.
- **Skeleton (#20):** research only. D2RLoader has no row injection for vanilla tables, so the plan's installer
  stands (adds the slot level, a LvlPrest row, the map item, its string). The SDK's custom tables reject a whole table
  on one bad cell, so the plugin parses its own tables (plan §3.0: skip bad rows).

## 4. Owner decisions (2026-11-11 late)

1. **Mod files: exception granted.** Cartographer may read the mod's `maps.txt` (and its other tables) and its
   installer may add rows to the mod's `levels.txt`/`misc.txt`, despite d2r-tools' "global plugin" rule.
2. **Byte signatures: yes**, as d2r-tools best practice, in place of fixed 93847 RVAs (more work in phases 1, 3, 4).
3. **Push/PR/issue permission granted** to Cartographer workers in d2r-tools.
4. **`Boss` takes monstats `Id`s and superunique names** (so `Bishibosh` in the §3.5a example is valid). Update
   plan §3.1 and `docs/cartographer/tables.md` accordingly.
5. **The vendored doctest 2.4.11** (downloaded by the layout worker) is approved.
6. **Do not merge pd2-resurrected #929 and #930.** They stay open as reference.

Still open, for the plan owner: `shapes.txt` column names, `tilesets.txt` `First` being a LvlPrest id, option C
editing LvlPrest despite "no vanilla table edits", the Difficulty column, mixed name kinds in `mapevents.txt` `Spawn`.

## 5. Left in pd2-resurrected (reference only, never merged)

- PRs #929 (DRLG steering) and #930 (level regeneration), open, docs only; **do not merge** (§4.6). Each has a comment
  pointing at the move.
- Spike branches `spike/cartographer-steer-proto`, `spike/cartographer-level-regen`, `spike/cartographer-outdoor-s1`.
- Owner reference branch `docs/map-kit-plan` (the plans' source).
- WIP branches `cartographer-layout-937` and `cartographer-catalogue-939` (copied byte-identical to d2r-tools;
  safe to delete once the owner agrees).
- `docs/cartographer-phase0` (#941, closed).
- PR #941 closed with a pointer to d2r-tools#25. Issues #927 and #928 stay here, linked as sub-issues of d2r-tools#19.

## 6. Leftover worktrees

- `.claude/worktrees/agent-a0728273882562cd3` (catalogue WIP, c76c924, clean, pushed): still **locked** by its
  agent harness, so `tools/worktree_remove.ps1` refuses it. Remove it once the lock clears.
- From earlier sessions, unchanged: `handoff-orchestrator-dispatch-aa0df4` (`docs/drlg-steering-spike`),
  `pd2-resurrected-pr-queue-b28b28` (`claude/level-regen-spike-117061`), `orchestrator-handoff-review-7b0ddd`
  (`docs/map-kit-plan`).
- d2r-tools: the local branch `cartographer/docs` in the main checkout stays until PR #25 merges.

## 7. Next steps (when the owner restarts Cartographer)

1. Merge d2r-tools#25 (docs only), after folding in the §4.4 `Boss` change.
2. Add `0x3e77a0` and `0x32c6a0` to the RE stub.
3. Restart wave 1 in d2r-tools: A (#20) first creates `plugins/cartographer/` with a CMake skeleton, so B (#21) and
   D (#23) can land under it; C (#22) continues from the pd2-resurrected spike branch, ported to d2r-tools' build.
4. Wave 2 and 3 order is unchanged from the plan's §5.

Game slots: this session never locked one. No agents are running.
