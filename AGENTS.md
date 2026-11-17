## Agent skills

### Issue tracker

Issues live in GitHub Issues (`gh` CLI). See `docs/agents/issue-tracker.md`.

Every PR links the issue it solves: the body opens with `Closes #<n>`, or `Part of #<n>` when it does not finish the issue. If no issue exists yet, open one first.

**Gate rule (the one home).** Docs-only PRs (only `.md` files, docs and `changelog.d/` fragments, except the gate input `docs/deviations.md`) merge without waiting
for CI. Any other PR merges only when CI's `table-diff` and `unit` are green on the PR's final head and the PR
is MERGEABLE (rerun after resolving a merge from main). Only when the runners are offline, run `tools/pd2_diff.py`
locally and paste the report for the final commit (its first line names the commit; untracked files count as
uncommitted changes). The CI table-diff report's first line names GitHub's merge commit, not the PR head.

**PR order.** Push, open the PR, then run the game check while CI runs and post its results to the PR in words and log lines. Never commit or
push a screenshot, to any branch (`issue-assets` included): it stays in the slot's run folder. Never merge an
unchecked PR.

Each CHANGELOG entry is one short line (one sentence, under ~100 chars), unless it is a huge feature.
Never edit `CHANGELOG.md` in a PR: add `changelog.d/<issue>-<slug>.md` (format and rules: the toolkit's
`templates/changelog.d/README.md` and its `d2r-release` skill); the toolkit's `release/stage.py` folds the fragments in at release.

After a PR merges, delete its branch, on the remote and locally.

`research/*` branches hold `/wayfinder` research findings. They are never merged, and never deleted: design docs and
issue comments link to them. `pd2-import` is an orphan branch of raw PD2 imports (ADR 0007): also never merged, never deleted.

### Triage labels

A bug that exists in PD2 itself, and that we fix, also gets `pd2-origin` and a row in `docs/pd2-origin-bugs.md`.

Default five-label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Testing in-game

- Game slots (separate game copies for a test) and how to set one up: `docs/agents/game-slots.md`. Which slots this repo may lock is the toolkit's slot registry (`~/.d2r-modkit/slots.toml`, machine config, enforced by `game_slots lock`; Slot2-5 on this machine). Name the slot every time; never `lock any`.
- The game-check recipe (lock, install, cheats, screenshots, unlock, worktree hygiene): `docs/agents/game-check.md`. Remove a worktree only with the toolkit's `tools/worktree_remove.ps1` (never `git worktree remove` or `rm -r`: they follow the `pd2-data` junction).
- The mod data is committed in `files/data/` and its tables are the source (ADR 0007): a data change edits the table
  or JSON there directly; never add a JSON patch. `install.ps1 -Data .` installs it with no extract or merge. Don't
  keep a `generated/` folder in a worktree (install refuses it); `pd2-data` is the only junction.
- **Read [`knowledge/cheat-commands.md`](https://github.com/synpoox/d2rAI-toolkit/blob/ce0a3174ec1ca7c08a1d27d54e63ab1aa9e844d3/knowledge/cheat-commands.md) before any in-game test.** It lists every D2RLoader console cheat command (spawn items, monsters and objects, set stats and skills, go to any level, unlock waypoints, god mode, and more) and what the item spawn lines did in game: spawn syntax and quality, `idall`, durability reading 0 after a reload, `gold`, cursor and pickup hints. Use them instead of playing to the spot.
- **Agents may run any in-game check, console-driven or not**, but prefer the console (clicking and movement are hard for agents). If a check runs past 10 minutes with no significant progress on what it checks, the agent stops, unlocks its slot, and a new `ready-for-human` issue hands the game check to the owner, with the steps left and what the agent already verified.
- PD2's uniques for `cheat uniques <row>` (zero-based rows in the merged `uniqueitems.txt`, `out/data/global/excel/`): Gull 39, Pelta Lunata 94, Goblin Toe 110.
- Always test in Hell difficulty, where monsters have enough HP to survive long enough to show what a skill does.
- Always test on a character of level 90 or higher.

## HD data

- **IMPORTANT: every JSON key D2R reads is case-sensitive.** Key `data/hd/character/monsters.json` by the exact
  monstats `Id`: a PD2 monster keyed `hellbovinemap` instead of `hellbovineMap` was invisible in game. Never lower-case
  a key built from a PD2 name unless D2R's own file shows it lower-cases that kind of name (uniques and sets: it does).

## Plugin code

- **Always use D2RLoader's PluginSDK (the toolkit's `third_party/PluginSDK`; `toolkit_path.py` prints the toolkit) before a native hook.** Read its `README.md` and
  `include/D2RLPlugin/` for a service that does the job (items: `createItem`, `executeTransaction`,
  `executeExistingItemTransaction`, `splitStack`; inventory; game rules; and others). Hook D2R code only when no service
  covers it, and say in the code why. A byte patch goes through the SDK's MutationService.

## Reverse engineering

Use Ghidra, as [`knowledge/engine-re.md`](https://github.com/synpoox/d2rAI-toolkit/blob/ce0a3174ec1ca7c08a1d27d54e63ab1aa9e844d3/knowledge/engine-re.md) describes. Use capstone only for quick byte checks in scripts (a hook's entry bytes).

- **D2R:** project `C:\ghidra-work\project\D2R.gpr` (the 93847 dump). Query it with the toolkit's `tools/ghidra_query.ps1 <Script.java> <args>`.
- **PD2:** project `C:\ghidra-work\pd2-project\PD2.gpr`, holding `ProjectDiablo.dll` and the 1.13c `D2Game.dll`, `D2Client.dll` and `D2Common.dll` from the PD2 install. Query it with `tools/ghidra_query.ps1 <Script.java> -Project PD2 [-Program D2Game.dll] <args>` (toolkit [PR 3](https://github.com/synpoox/d2rAI-toolkit/pull/3) or later).
