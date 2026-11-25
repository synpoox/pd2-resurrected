# pd2-resurrected: agent rules

Stable rules only. Details live in the linked files: open one only when the task needs it.
Toolkit paths below are relative to the toolkit checkout (`py -3 tools/toolkit_path.py` prints it).

## Issues and PRs

- Work tracker: GitHub Issues via `gh`. Labels and conventions: `docs/agents/issue-tracker.md`, `docs/agents/triage-labels.md`.
- `ready-for-agent` means approved: run it, even if the text still says "waiting for review".
- Issues are short: goal + "Done when". Scope goes in the lane prompt, not the issue.
- Next lane = oldest open issue.
- A PR body opens with `Closes #<n>` (or `Part of #<n>`). No issue yet: open one first.
- A PD2 bug we fix also gets `pd2-origin` and a row in `docs/pd2/origin-bugs.md`.
- Finishing an issue updates its row in `docs/roadmap.md` (Issue column = merged PR).
- Controller PRs update `docs/controller.md`; binds mirror PD2 or D2R native binds, never invented.
- Default five-label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

## Merge gate

- Docs-only PRs (`.md`, docs, `changelog.d/`, but not `docs/deviations.md`) merge without CI.
- Every other PR: CI `table-diff` and `unit` green on the final head, and MERGEABLE.
  Runners offline: run `tools/pd2_diff.py` locally and paste the report for the final commit.
- Order: push, open the PR, then review (Opus) and game check in parallel, then fixes, then squash-merge.
- Data-only edits (tables/JSON in `files/data/`) merge on review + CI. No game check, even if a reviewer asks.
- Any other code change gets one brief game check before merge.
- After merge, delete the branch on the remote and locally.

## Changelog

- Never edit `CHANGELOG.md` in a PR. Add `changelog.d/<issue>-<slug>.md`: one plain sentence, under ~100 chars.
- Format: toolkit `templates/changelog.d/README.md`.

## Branches

- `research/*`: findings fold into `docs/research` on main, then the branch is deleted
  (tag `archive/research/<name>` first if non-doc files stay off main).
- `pd2-import`: orphan branch of raw PD2 imports (ADR 0007). Never merge, never delete.
- Remove a worktree only with toolkit `tools/worktree_remove.ps1`
  (`git worktree remove` / `rm -r` follow the `pd2-data` junction and delete it).

## Mod data

- `files/data/` tables are the source (ADR 0007). Edit the table or JSON there; never add a JSON patch.
- `install.ps1 -Data .` installs it. No `generated/` folder in a worktree; `pd2-data` is the only junction.
- **D2R JSON keys are case-sensitive.** Key `data/hd/character/monsters.json` by the exact monstats `Id`
  (`hellbovineMap`, not `hellbovinemap`). Lower-case only where D2R's own file does (uniques, sets).
- Glossary: `GLOSSARY.md`. Decisions: `docs/adr/`.

## Plugin code

- New core-rules rule = one file `plugin/core-rules/skill/<rule>.cpp`: define `Install<Rule>(context, report, reportSize)`,
  end with `PD2R_RULE(Install<Rule>, <next free multiple of 10>)`. No other file changes.
- Do not add to `plugin/core-rules/skill_functions.cpp` (360 KB). Never read it whole:
  `grep -n` for the function, then read only that range.
- Use the PluginSDK (toolkit `third_party/PluginSDK`, `include/D2RLPlugin/`) before any native hook.
  Hook D2R code only when no service covers it, and say why in the code. Byte patches go through MutationService.
- New hooks resolve by unique byte signature (toolkit `shared/plugin-common` scanner), never a fixed RVA.

## Reverse engineering

- Ghidra per toolkit `knowledge/engine-re.md`; capstone only for quick byte checks.
- D2R: `C:\ghidra-work\project\D2R.gpr`. PD2: `C:\ghidra-work\pd2-project\PD2.gpr`
  (`ProjectDiablo.dll`, 1.13c `D2Game/D2Client/D2Common.dll`).
- Query: toolkit `tools/ghidra_query.ps1 <Script.java> [-Project PD2 -Program D2Game.dll] <args>`.

## Game checks

Only when a check is due (see Merge gate). Then read, in this order and only these:
1. `docs/agents/game-check.md` (recipe, slots, screenshots, cleanup)
2. `docs/console-commands.md` for our plugin commands; toolkit `knowledge/cheat-commands.md` for cheats: grep the section, don't read it whole.

Fixed rules:
- Slots: this repo locks Slot2-5 only. Name the slot every time; never `lock any`.
- Character: level 99, Hell difficulty. Prefer console commands over clicking and walking.
- Stop only after 5 minutes with no progress; then unlock and open a `ready-for-human` issue with the steps left.
- Tooltips and monster name plates are the owner's: prove spawns and drops by log line, hand the visual read over.
- Report in words and log lines. Never commit, push or upload a screenshot, to any branch.

## Lanes (orchestrator)

- Implementers are Opus for tasks involving heavy reverse-engineering, Sonnet otherwise.
- Lane prompts are self-contained: goal, files, done-when, these rules by reference. No shared "common" file to re-read.
- Lanes message main with the PR URL before their game check.
- Lane reports: PR URL, CI state, one line per check result. No logs, diffs or file dumps.
