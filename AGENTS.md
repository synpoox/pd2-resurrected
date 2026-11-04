## Agent skills

### Issue tracker

Issues live in GitHub Issues (`gh` CLI). See `docs/agents/issue-tracker.md`.

Every PR links the issue it solves: the body opens with `Closes #<n>`, or `Part of #<n>` when it does not finish the issue. If no issue exists yet, open one first.

### Triage labels

A bug that exists in PD2 itself, and that we fix, also gets `pd2-origin` and a row in `docs/pd2-origin-bugs.md`.

Default five-label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Testing in-game

- The in-game console can toggle god mode, teleport you out of town, and spawn monsters. Use it to set up a test quickly instead of playing to the spot.
- Spawn items from the console after `debug_mode` turns cheats on. These are the lines the debug menu's Item Spawner sends; its window needs double-clicks, which the test harness cannot send. `cheat item` does nothing.
  - A base item: `cheat spawn <item code>,inv 0` (e.g. `cheat spawn wnd,inv 0`) puts it in the inventory. Without `inv 0`, or with no room, it drops on the ground ("I am overburdened"). `typ=<letter>` sets the quality, but the letters don't match their names (`typ=n` rolled magic, `typ=u` rare), so read the tooltip.
  - A unique: `cheat uniques <row>`, where the row is zero-based in the merged `uniqueitems.txt` (`out/data/global/excel/`): Gull 39, Pelta Lunata 94, Goblin Toe 110. It drops on the ground.
  - `cheat idall` identifies every item you carry.
- Spawned items have normal durability: a fresh Goblin Toe came at 17 of 18 and kept it through `cheat idall` and 55 attacks. A 0-durability item has its stats switched off, so hover the item before a test and spawn a fresh one if the tooltip says `Durability: 0`.
- `cheat gold <n>` does not add gold.
- To pick up a ground item, hold Alt so the item labels show, then click the label.
- Always test in Hell difficulty, where monsters have enough HP to survive long enough to show what a skill does.
- Always test on a character of level 90 or higher.

## Reverse engineering

Use Ghidra, as `d2rAI-toolkit/knowledge/engine-re.md` describes. Use capstone only for quick byte checks in scripts (a hook's entry bytes).

- **D2R:** project `C:\ghidra-work\project\D2R.gpr` (the 93847 dump). Query it with the toolkit's `tools/ghidra_query.ps1 <Script.java> <args>`.
- **PD2:** project `C:\ghidra-work\pd2-project\PD2.gpr`, holding `ProjectDiablo.dll` and the 1.13c `D2Game.dll`, `D2Client.dll` and `D2Common.dll` from the PD2 install. `ghidra_query.ps1` only opens the D2R project, so run `analyzeHeadless` directly: `analyzeHeadless C:\ghidra-work\pd2-project PD2 -process <dll> -noanalysis -readOnly -scriptPath <toolkit>\tools\ghidra_scripts -postScript <Script.java> <args>`.
