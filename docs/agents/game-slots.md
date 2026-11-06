# Game slots

A **game slot** is a separate copy of the D2R install for one test run, with its own plugins, mod data and saves.
"Set up a slot" means: lock one, install a build into it, and hand it over. d2rAI-toolkit's `testing/` folder holds the
tools (`game_slots.py`, `game_ctl.py`, `game_checks.py`; its `testing/README.md`).

## This machine

- Slots live in `C:\D2RTestSlots\Slot<N>`. PD2 Resurrected owns **Slot4-Slot8**; Slot2 is D2RToolsTest and Slot3 is
  ProjectForever. Always name the slot: `lock any` and a bare `setup` reach Slot2 and Slot3.
- The config is `C:\D2RTestSlots\modkit-testing-pd2.toml`. Set `MODKIT_TESTING_CONFIG` to it on every command, or the
  tools read the toolkit's default config and report the wrong saves.
- A slot's saves are in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected-Slot<N>`. Test characters follow
  AGENTS.md's testing rules.

## Steps

1. `python <toolkit>/testing/game_slots.py status` and pick a free slot from 4-8.
2. `python <toolkit>/testing/game_slots.py lock <N> <owner>`. For a slot the owner tests by hand, use an owner name
   such as `owner-<work item>`: a manual lock lasts until `unlock`.
3. Build and install: `plugin\build.ps1`, `python tools/pd2_merge.py`, then
   `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate` (main still has known validation errors).
4. Check the installed data holds the change (the merged table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. `python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.
