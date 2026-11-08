# Game slots

A **game slot** is a separate copy of the D2R install for one test run, with its own plugins, mod data and saves.
"Set up a slot" means: lock one, install a build into it, and hand it over. d2rAI-toolkit's `testing/` folder holds the
tools (`game_slots.py`, `game_ctl.py`, `game_checks.py`; its `testing/README.md`).

## This machine

- Slots live in `C:\D2RTestSlots\Slot<N>`. PD2 Resurrected owns **Slot4-Slot8**; Slot2 is D2RToolsTest and Slot3 is
  ProjectForever. Always name the slot: `lock any` and a bare `setup` reach Slot2 and Slot3.
- The config is `C:\D2RTestSlots\modkit-testing-pd2.toml`. Set `MODKIT_TESTING_CONFIG` to it on every command, or the
  tools read the toolkit's default config and report the wrong saves.
- Test characters must be Reign of the Warlock characters (character creation's Game Version, the default). A Classic or
  LoD character plays D2R's vanilla `excel/base/` tables, which the mod never writes (ADR 0001): vanilla skill tree,
  no PD2 skills, and `cheat sklvl` of a PD2 skill does nothing (#442). On character select an LoD character's banner
  shows an X badge. `cheat expansion 2` does not convert one: it crashed the game.
- A slot's saves are in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected-Slot<N>`. Test characters follow
  AGENTS.md's testing rules.

## Steps

1. `python <toolkit>/testing/game_slots.py status` and pick a free slot from 4-8.
2. `python <toolkit>/testing/game_slots.py lock <N> <owner>`. For a slot the owner tests by hand, use an owner name
   such as `owner-<work item>`: a manual lock lasts until `unlock`.
3. Build and install: `plugin\build.ps1`, `python tools/pd2_extract.py`, `python tools/pd2_merge.py`, then
   `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate` (main still has known validation errors).
   The shared cache below makes the extract and merge near-instant when another worktree already ran them.
   `install.ps1` leaves only our plugins in `<slot>\d2rloader\plugins` (`core-rules.dll`, `core-maps.dll`, `core-ui.dll`,
   `test-harness.dll`, `d2r-testharness-console.txt`): it moves anything else to
   `C:\D2RTestSlots\backups\stray-plugins-<date>\Slot<N>\` and lists it. Pass `-KeepExtras` to keep a slot's extras.
4. Check the installed data holds the change (the merged table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. `python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.

## Shared cache

`C:\pd2r-cache` (or `PD2R_CACHE`) holds what every worktree would otherwise rebuild (`tools/pd2_cache.py`, #440):

- `pd2_extract.py` (no `--out`) extracts once per extract stamp into `extract\<key>\` and makes the worktree's
  `pd2-data` a junction to it. A lock lets one extract run at a time; the others wait, then skip. A worktree whose
  `pd2-data` is still a real folder keeps using it: delete that folder to share the cache.
- `pd2_merge.py` copies `merge\<key>\` into `generated\` when merge/, tools/, the stamps and modkit match a finished
  merge, else merges and stores the result. Don't skip the merge in a worktree: a hit takes seconds.
- `install.ps1` (modkit install) writes only the `out\data` files that differ from the slot's and touches nothing outside
  `<slot>\mods\PD2Resurrected`, so moving a slot between builds is quick.
- Before removing a worktree, unlink its junctions, and never delete through them: `git worktree remove --force`
  follows a junction and empties the shared cache (`C:\pd2r-cache`, or `F:\pd2r-cache` for slots on F:):
  `cmd /c rmdir <worktree>\pd2-data`, `cmd /c rmdir <worktree>\generated`, `cmd /c rmdir <worktree>\out`.
- Never delete a cache folder another worktree's `pd2-data` points at. CI (`GITHUB_ACTIONS`) does not use these.
- Nothing prunes the cache. Each `merge\<key>` holds about 2 GB, so the owner deletes old ones by hand while no
  merge is running.
