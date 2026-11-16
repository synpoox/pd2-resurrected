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

The per-check recipe (cheats, shots, images, unlock) is `docs/agents/game-check.md`; these are the slot basics.

1. `python <toolkit>/testing/game_slots.py status` and pick a free slot from 4-8.
2. `python <toolkit>/testing/game_slots.py lock <N> <owner>`. For a slot the owner tests by hand, use an owner name
   such as `owner-<work item>`: a manual lock lasts until `unlock`.
3. Build and install: `plugin\build.ps1`, `python tools/pd2_extract.py`, `python tools/pd2_merge.py`, then, with
   `MODKIT_RUN` set to the lock's owner, `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate` (main
   still has known validation errors). modkit install refuses a slot, so `install.ps1` installs a slot's data through
   the toolkit's `testing/slot_install.py`, which keeps the slot's own savepath and needs the lock held by `MODKIT_RUN`.
   The shared cache below makes the extract and merge near-instant when another worktree already ran them.
   `install.ps1` leaves only our plugins in `<slot>\d2rloader\plugins` (`core-rules.dll`, `core-events.dll`, `core-ui.dll`, `cartographer.dll`,
   `test-harness.dll`, `d2r-testharness-console.txt`): it moves anything else to
   `C:\D2RTestSlots\backups\stray-plugins-<date>\Slot<N>\` and lists it. `cartographer.dll` (required here, built in d2r-tools) is kept if the slot holds it; install does not copy it. Pass `-KeepExtras` to keep other extras.
   It also moves our own `core-*.dll` and `cartographer.dll` found in `<slot>\mods\PD2Resurrected\d2rloader\plugins` (they shadow the game-wide install) to
   `C:\D2RTestSlots\backups\stale-mod-plugins-<date-time>\Slot<N>\`; companion DLLs there stay. With `-Data` it also copies
   `companions/cartographer/signatures.txt` into the mod's `excel/d2rloader/cartographer/` (core-rules' cube hook needs it).
4. Check the installed data holds the change (the merged table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. With `MODKIT_RUN=<owner>` set, `python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.

## Shared cache

`C:\pd2r-cache` (or `PD2R_CACHE`) holds what every worktree would otherwise rebuild (`tools/pd2_cache.py`, #440):

- `pd2_extract.py` (no `--out`) extracts once per extract stamp into `extract\<key>\` and makes the worktree's
  `pd2-data` a junction to it. A lock lets one extract run at a time; the others wait, then skip. A worktree whose
  `pd2-data` is still a real folder keeps using it: delete that folder to share the cache.
- `pd2_merge.py` caches each rule on its own (#558) in `rules\<key>\`: a rule whose code, stamps and modkit match, and
  whose inputs (every file it read, path it checked and folder it listed, under the worktree and D2R's tables) are
  unchanged, copies what it wrote last time instead of running. So a PR that edits one rule reruns that rule and the
  rules that read its output. Don't skip the merge in a worktree.
  Merges in different worktrees run at once (only one per worktree, `pd2_merge.lock` in it): the cache is safe to
  share, as entries are kept and deleted by rename, so a replay that loses a race just runs the rule. The output is
  checked (required tables, table count) before use: a bad tree is rebuilt once without the cache, then the run fails
  (#745). A rule's cache key covers its module, what it imports, and every file of an imported package.
- `install.ps1` (modkit install) writes only the `out\data` files that differ from the slot's and touches nothing outside
  `<slot>\mods\PD2Resurrected`, so moving a slot between builds is quick.
- Merge slots (#760, #801): at most `PD2R_MERGE_SLOTS` (default 4) merges run at once on the machine, agents' and the CI
  gate's together (the gate's uncached merge takes a slot too, under the same cache root). A merge that waits prints
  `merge queued: N/N slots busy`; the gate's says `CI merge queued`. Pick the count from RAM (`pd2_cache.merge_slots`).
  The last `PD2R_CI_RESERVED` (default 2, #872) slots are the gate's: an agent merge holds at most
  `PD2R_MERGE_SLOTS - PD2R_CI_RESERVED` (at least one), CI (`GITHUB_ACTIONS`) may take any. The queued line says which
  pool it waits on. Set both variables in the runners' `.env` too.
- CI's gate shares the merge slots, the extract lock and `pd2-data` with the agents; it does not share the rule
  cache (#820). With 4 agents merging, expect CI to queue; a merge is one per commit (#820, #821), so reuse a
  worktree's or CI's output, and time merges by CPU time (`time.process_time`), not wall time under load.
- Removing a worktree: `tools/worktree_remove.ps1` only (`docs/agents/game-check.md`, "Worktree hygiene").
- Each merge prunes the least recently used `rules\` entries until they hold `PD2R_CACHE_GB` (default 20) GB. A rule
  run that wrote outside `generated\` (new `merge\art` PNGs) is not kept. The whole-merge `merge\<key>` folders older
  tools kept are pruned only with `PD2R_PRUNE_LEGACY=1`: set it once no worktree runs the old tools. `extract\` is
  never pruned.
