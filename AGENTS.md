## Agent skills

### Issue tracker

Issues live in GitHub Issues (`gh` CLI). See `docs/agents/issue-tracker.md`.

Every PR links the issue it solves: the body opens with `Closes #<n>`, or `Part of #<n>` when it does not finish the issue. If no issue exists yet, open one first.

Rerun the table-diff gate (`tools/pd2_diff.py`) after resolving a merge from main, and paste the report for the PR's
final commit (its first line names the commit). Merge only on a report for the final commit. Untracked files count as uncommitted changes; the gate CI job's report names GitHub's merge commit, not the PR head.

Each CHANGELOG entry is one short line (one sentence, under ~100 chars), unless it is a huge feature.
Never edit `CHANGELOG.md` in a PR: add `changelog.d/<issue>-<slug>.md` holding `#### <Category>` and then the `- ` line;
`tools/release.py` folds the fragments in at release. Likewise add map-generator passes and Kit.add hooks as new modules
under `tools/presets/passes/` and `tools/presets/hooks/` (each declares `ORDER`), never to a shared list.

After a PR merges, delete its branch, on the remote and locally.

`research/*` branches hold `/wayfinder` research findings. They are never merged, and never deleted: design docs and
issue comments link to them.

### Triage labels

A bug that exists in PD2 itself, and that we fix, also gets `pd2-origin` and a row in `docs/pd2-origin-bugs.md`.

Default five-label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Testing in-game

- Game slots (separate game copies for a test, Slot4-8 here) and how to set one up: `docs/agents/game-slots.md`.
- `pd2_extract.py` and `pd2_merge.py` reuse the machine-wide cache in `C:\pd2r-cache` (game-slots.md, "Shared cache"):
  run them as usual, and don't copy `pd2-data/`, `generated/` or `out/` between worktrees.
- Before removing a worktree, unlink its `pd2-data`, `generated` and `out` junctions (`cmd /c rmdir <worktree>\<name>`):
  `git worktree remove --force` follows them and empties the shared cache (`C:\pd2r-cache`, `F:\pd2r-cache`).
- **Read `d2rAI-toolkit/knowledge/cheat-commands.md` before any in-game test.** It lists every D2RLoader console cheat command (spawn items, monsters and objects, set stats and skills, go to any level, unlock waypoints, god mode, and more) and what the item spawn lines did in game: spawn syntax and quality, `idall`, durability reading 0 after a reload, `gold`, cursor and pickup hints. Use them instead of playing to the spot.
- **Agents may run any in-game check, console-driven or not**, but prefer the console (clicking and movement are hard for agents). If a check runs past 10 minutes with no significant progress on what it checks, the agent stops, unlocks its slot, and a new `ready-for-human` issue hands the game check to the owner, with the steps left and what the agent already verified.
- PD2's uniques for `cheat uniques <row>` (zero-based rows in the merged `uniqueitems.txt`, `out/data/global/excel/`): Gull 39, Pelta Lunata 94, Goblin Toe 110.
- Always test in Hell difficulty, where monsters have enough HP to survive long enough to show what a skill does.
- Always test on a character of level 90 or higher.

## HD data

- **IMPORTANT: every JSON key D2R reads is case-sensitive.** Key `data/hd/character/monsters.json` by the exact
  monstats `Id`: a PD2 monster keyed `hellbovinemap` instead of `hellbovineMap` was invisible in game. Never lower-case
  a key built from a PD2 name unless D2R's own file shows it lower-cases that kind of name (uniques and sets: it does).

## Plugin code

- **Always use D2RLoader's PluginSDK (`third_party/PluginSDK`) before a native hook.** Read its `README.md` and
  `include/D2RLPlugin/` for a service that does the job (items: `createItem`, `executeTransaction`,
  `executeExistingItemTransaction`, `splitStack`; inventory; game rules; and others). Hook D2R code only when no service
  covers it, and say in the code why. A byte patch goes through the SDK's MutationService.

## Reverse engineering

Use Ghidra, as `d2rAI-toolkit/knowledge/engine-re.md` describes. Use capstone only for quick byte checks in scripts (a hook's entry bytes).

- **D2R:** project `C:\ghidra-work\project\D2R.gpr` (the 93847 dump). Query it with the toolkit's `tools/ghidra_query.ps1 <Script.java> <args>`.
- **PD2:** project `C:\ghidra-work\pd2-project\PD2.gpr`, holding `ProjectDiablo.dll` and the 1.13c `D2Game.dll`, `D2Client.dll` and `D2Common.dll` from the PD2 install. Query it with `tools/ghidra_query.ps1 <Script.java> -Project PD2 [-Program D2Game.dll] <args>` (toolkit [PR 3](https://github.com/synpoox/d2rAI-toolkit/pull/3) or later).
