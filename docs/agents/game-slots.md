# Game slots

A **game slot** is a separate copy of the D2R install for one test run, with its own plugins, mod data and saves.
"Set up a slot" means: lock one, install a build into it, and hand it over. d2rAI-toolkit's `testing/` folder holds the
tools (`game_slots.py`, `game_ctl.py`, `game_checks.py`; its [`testing/README.md`](https://github.com/synpoox/d2rAI-toolkit/blob/732a7146e71402a48d8fd379887b75977a971ad1/testing/README.md), which also describes the fixture pattern our
`tests/game/fixtures/*` follow: one tiny mod per check).

## This machine

- Slots live in `C:\D2RTestSlots\Slot<N>`. Which slots PD2 Resurrected may lock is not written here: it is the toolkit's slot
  registry, `~/.d2r-modkit/slots.toml` (machine config; `game_slots lock` refuses a slot another project owns, and an owned
  slot when no project is given). On this machine the registry gives it Slot2-Slot5. Always name the slot: a bare `lock`
  or `setup` must not pick one for you, and never `lock any`.
- Every toolkit command here sets `MODKIT_PROJECT=pd2-resurrected` (who is asking the registry) and
  `MODKIT_TESTING_CONFIG=C:\D2RTestSlots\modkit-testing-pd2.toml` (or `--config`). With no config the toolkit refuses to
  touch a slot instead of using its default config.
- Test characters must be Reign of the Warlock characters (character creation's Game Version, the default). A Classic or
  LoD character plays D2R's vanilla `excel/base/` tables, which the mod never writes (ADR 0001): vanilla skill tree,
  no PD2 skills, and `cheat sklvl` of a PD2 skill does nothing (#442). On character select an LoD character's banner
  shows an X badge. `cheat expansion 2` does not convert one: it crashed the game.
- A slot's saves are in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected-Slot<N>`. Test characters follow
  AGENTS.md's testing rules.

## Steps

The per-check recipe (cheats, shots, images, unlock) is `docs/agents/game-check.md`; these are the slot basics.

1. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml python <toolkit>/testing/game_slots.py status` and pick a free slot your registry entry allows (here 2-5).
2. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> python <toolkit>/testing/game_slots.py lock <N> <owner>`. For a slot the owner tests by hand, use an owner name
   such as `owner-<work item>`: a manual lock lasts until `unlock`.
3. Build and install: `plugin\build.ps1`, then, with `MODKIT_RUN` set to the lock's owner,
   `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate` (it copies the committed `files/data`; no extract
   or merge; main still has known validation errors). modkit install refuses a slot, so `install.ps1` installs a slot's data through
   the toolkit's `testing/slot_install.py`, which keeps the slot's own savepath and needs the lock held by `MODKIT_RUN`.
   `install.ps1` leaves only our plugins in `<slot>\d2rloader\plugins` (`core-rules.dll`, `core-events.dll`, `core-ui.dll`, `cartographer.dll`,
   `test-harness.dll`, `d2r-testharness-console.txt`): it moves anything else to
   `C:\D2RTestSlots\backups\stray-plugins-<date>\Slot<N>\` and lists it. `cartographer.dll` (required here, built in d2r-tools) is kept if the slot holds it; install does not copy it. Pass `-KeepExtras` to keep other extras.
   It also moves our own `core-*.dll` and `cartographer.dll` found in `<slot>\mods\PD2Resurrected\d2rloader\plugins` (they shadow the game-wide install) to
   `C:\D2RTestSlots\backups\stale-mod-plugins-<date-time>\Slot<N>\`; companion DLLs there stay. With `-Data` it also copies
   `companions/cartographer/signatures.txt` into the mod's `excel/d2rloader/cartographer/` (core-rules' cube hook needs it).
4. Check the installed data holds the change (the table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.

## Shared cache

`C:\pd2r-cache` (or `PD2R_CACHE`) holds what every worktree would otherwise rebuild (`tools/pd2_cache.py`, #440):

- `pd2_extract.py` (no `--out`; only the gate and `pd2_import.py` run it) extracts once per extract stamp into `extract\<key>\` and makes the worktree's
  `pd2-data` a junction to it. A lock lets one extract run at a time; the others wait, then skip. A worktree whose
  `pd2-data` is still a real folder keeps using it: delete that folder to share the cache.
- `install.ps1` (modkit install) writes only the `out\data` files that differ from the slot's and touches nothing outside
  `<slot>\mods\PD2Resurrected`, so moving a slot between builds is quick.
- Only `pd2_import.py` (through `pd2_merge.py`) uses the rule cache (`rules\<key>\`, pruned to `PD2R_CACHE_GB`, default 20) and
  the merge slots (`PD2R_MERGE_SLOTS`, default 4); a normal build or install runs no merge.
- Removing a worktree: `tools/worktree_remove.ps1` only (`docs/agents/game-check.md`, "Worktree hygiene").
