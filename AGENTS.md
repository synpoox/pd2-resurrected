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
- Always test in Hell difficulty, where monsters have enough HP to survive long enough to show what a skill does.
- Always test on a character of level 90 or higher.

## Reverse engineering

Use Ghidra, as `d2rAI-toolkit/knowledge/engine-re.md` describes. Use capstone only for quick byte checks in scripts (a hook's entry bytes).

- **D2R:** project `C:\ghidra-work\project\D2R.gpr` (the 93847 dump). Query it with the toolkit's `tools/ghidra_query.ps1 <Script.java> <args>`.
- **PD2:** project `C:\ghidra-work\pd2-project\PD2.gpr`, holding `ProjectDiablo.dll` and the 1.13c `D2Game.dll`, `D2Client.dll` and `D2Common.dll` from the PD2 install. `ghidra_query.ps1` only opens the D2R project, so run `analyzeHeadless` directly: `analyzeHeadless C:\ghidra-work\pd2-project PD2 -process <dll> -noanalysis -readOnly -scriptPath <toolkit>\tools\ghidra_scripts -postScript <Script.java> <args>`.
