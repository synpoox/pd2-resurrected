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
4. Check the installed data holds the change (the merged table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. `python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.

## Shared cache

`C:\pd2r-cache` (or `PD2R_CACHE`) holds what every worktree would otherwise rebuild (`tools/pd2_cache.py`, #440):

- `pd2_extract.py` (no `--out`) extracts once per extract stamp into `extract\<key>\` and makes the worktree's
  `pd2-data` a junction to it. A lock lets one extract run at a time; the others wait, then skip. A worktree whose
  `pd2-data` is still a real folder keeps using it: delete that folder to share the cache.
- `pd2_merge.py` caches each rule on its own (#558) in `rules\<key>\`: a rule whose code, stamps and modkit match, and
  whose inputs under the worktree (every file it read, every path it checked, every folder it listed) are unchanged,
  copies what it wrote last time instead of running. So a PR that edits one rule reruns that rule and the rules that
  read its output. Don't skip the merge in a worktree.
- `install.ps1` writes only the `out\data` files that differ from the slot's and touches nothing outside
  `<slot>\mods\PD2Resurrected`, so moving a slot between builds is quick.
- Never delete a cache folder another worktree's `pd2-data` points at. CI (`GITHUB_ACTIONS`) does not use these.
- Each merge prunes the least recently used `rules\` entries, and the whole-merge `merge\<key>` folders older tools
  kept, until the cache holds `PD2R_CACHE_GB` (default 20) GB. `extract\` is never pruned.
