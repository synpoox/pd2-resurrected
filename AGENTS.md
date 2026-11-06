## Agent skills

### Issue tracker

Issues live in GitHub Issues (`gh` CLI). See `docs/agents/issue-tracker.md`.

Every PR links the issue it solves: the body opens with `Closes #<n>`, or `Part of #<n>` when it does not finish the issue. If no issue exists yet, open one first.

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
- The in-game console can toggle god mode, teleport you out of town, and spawn monsters. Use it to set up a test quickly instead of playing to the spot.
- Spawn items from the console after `debug_mode` turns cheats on. These are the lines the debug menu's Item Spawner sends; its window needs double-clicks, which the test harness cannot send. `cheat item` does nothing.
  - A base item: `cheat spawn <item code>,inv 0` (e.g. `cheat spawn wnd,inv 0`) puts it in the inventory. Without `inv 0`, or with no room, it drops on the ground ("I am overburdened"). `typ <letter>` with a space sets the quality: `cheat spawn t11,typ m,inv 0` gives a magic map and `typ r` a rare one (D2R's own test lines use `typ m`). `typ=<letter>` did not change it in [MS5](https://github.com/synpoox/pd2-resurrected/issues/220)'s run, so read the tooltip.
  - A unique: `cheat uniques <row>`, where the row is zero-based in the merged `uniqueitems.txt` (`out/data/global/excel/`): Gull 39, Pelta Lunata 94, Goblin Toe 110. It drops on the ground.
  - `cheat idall` identifies every item you carry.
- Spawned items have normal durability: a fresh Goblin Toe came at 17 of 18 and kept it through `cheat idall` and 55 attacks. But every item reads `Durability: 0` after the game is saved and loaded again, and a 0-durability item has its stats switched off. So spawn fresh items after each load, and hover them before a test.
- An unidentified item can't be equipped ("I can't use this yet"): run `cheat idall` first.
- A red cell behind every item in the inventory means the cursor holds an item, and tooltips don't show until you put it down.
- Escape twice saves and exits to the character screen.
- `cheat gold <n>` does not add gold.
- To pick up a ground item, hold Alt so the item labels show, then click the label.
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
- **PD2:** project `C:\ghidra-work\pd2-project\PD2.gpr`, holding `ProjectDiablo.dll` and the 1.13c `D2Game.dll`, `D2Client.dll` and `D2Common.dll` from the PD2 install. `ghidra_query.ps1` only opens the D2R project, so run `analyzeHeadless` directly: `analyzeHeadless C:\ghidra-work\pd2-project PD2 -process <dll> -noanalysis -readOnly -scriptPath <toolkit>\tools\ghidra_scripts -postScript <Script.java> <args>`.
