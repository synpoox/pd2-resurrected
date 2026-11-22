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
- Use Reign of the Warlock characters for PD2 gameplay checks. For a native-mode inventory check, create a
  fresh character with **Classic** or **Lord of Destruction** selected under Game Version. LoD characters
  have an X badge on character select. Both modes read `excel/base/`, which the build fills with the mod's tables (#1853).
  Use a fresh character for each mode. Changing a character's mode with the console has crashed the game (#442).
- A slot's saves are in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected-Slot<N>`. Test characters follow
  AGENTS.md's testing rules.

## Game version (3.4 and 3.3)

One build of the mod runs on D2R 3.4 (build 95032) and 3.3 (93847): D2RLoader 1.3.1 runs its own engine image
(`0x6ab3782c`, 93847's code) with the installed game's data, so every plugin signature and address is the same on both
(#1509). The tables and strings the mod ships replace the game's; of the files 3.4 changed, only `propertygroups.txt`
(one new row) and the `ui-controller` and `vo` strings reach the game from 3.4.

- Slot 1, the config's `install_dir`, is the owner's 3.4 game, `C:\Games\Diablo II Resurrected`: read only, never
  written, launched or locked. Slots 2-5 are made from it and run 3.4.
- After a D2R patch, sync each slot, one at a time, with the lock held: `game_slots.py lock <N> <owner>`,
  `game_slots.py setup <N>`, `unlock`. Under `Data` the sync mirrors slot 1 and removes the CASC files the patch dropped
  (a stale `.idx` fails with "needs repair"). It hard-links on the same drive and copies (about 30 GB) to a slot on F:.
- A 3.3 slot: `C:\Games\Diablo II Resurrected Infernal Edition` is a 3.3 copy (also read only). Copy the config, set
  its `install_dir` to that folder, and run `setup <N>` with `MODKIT_TESTING_CONFIG` pointing at the copy; run `setup`
  with the normal config to bring the slot back to 3.4.
- Which version a slot runs: `Version` in `<slot>\.build.info`, or the loader log's `[BUILD] D2R version` line. Our
  plugins log it too, with the image check: `core-rules: 0.2.0 loaded (mod PD2Resurrected, D2R 3.4.0, image 0x6ab3782c
  (expects 0x6ab3782c, match), ...)`. The loader's `mod build 93847` comes from the mod's dataversion, not the game: it is no mismatch on 3.4.

## Steps

The per-check recipe (cheats, shots, unlock) is `docs/agents/game-check.md`; these are the slot basics.

1. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml python <toolkit>/testing/game_slots.py status` and pick a free slot your registry entry allows (here 2-5).
2. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> python <toolkit>/testing/game_slots.py lock <N> <owner>`. For a slot the owner tests by hand, use an owner name
   such as `owner-<work item>`: a manual lock lasts until `unlock`.
3. Build and install: `plugin\build.ps1`, then, with `MODKIT_RUN` set to the lock's owner,
   `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate` (it copies the committed `files/data`; no extract
   or merge; main still has known validation errors). modkit install refuses a slot, so `install.ps1` installs a slot's data through
   the toolkit's `testing/slot_install.py`, which keeps the slot's own savepath and needs the lock held by `MODKIT_RUN`. `install.ps1` checks the lock first (`game_slots.py check <N> $MODKIT_RUN`) and writes nothing, failing, when `MODKIT_RUN` is unset or does not hold it (#1241).
   `install.ps1` installs our plugins (`core-rules.dll`, `core-events.dll`, `core-ui.dll`, `cartographer.dll`) mod-scoped, in
   `<slot>\mods\PD2Resurrected\d2rloader\plugins`, beside the companions (#1180). It leaves only the test harness
   (`test-harness.dll`, `d2r-testharness-console.txt`) in the game-wide `<slot>\d2rloader\plugins`: it moves anything else there,
   our old game-wide copies included, to `C:\D2RTestSlots\backups\stray-plugins-<date>\Slot<N>\` and lists it. Install always overwrites the slot's `cartographer.dll` with d2r-tools' `dist\cartographer` build, so testing a cartographer branch build needs `-Cartographer <path>`. It copies the latest `cartographer.dll` (`-Cartographer`, `CARTOGRAPHER_DLL`, else d2r-tools' `dist\cartographer`; it refuses one older than the d2r-tools commit pinned in `tools/cartographer.pin` (#1300), checking before touching the folder), and outside a test slot fetches and installs the companions. Pass `-KeepExtras` to keep third-party plugins in the game-wide folder (our old copies still go).
   Our plugin logs and `<plugin>-off.txt` files are now in the mod's `d2rloader\logs` and `d2rloader\config`. With `-Data` it also copies
   `companions/cartographer/signatures.txt` into the mod's `excel/d2rloader/cartographer/` (core-rules' cube hook needs it).
4. Check the installed data holds the change (the table row in `out/data`).
5. For a hand test, write the slot, its lock owner and the unlock command into the game-check issue.
6. `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> python <toolkit>/testing/game_slots.py unlock <N> <owner>` once the test is done.

## Shared cache

`C:\pd2r-cache` (or `PD2R_CACHE`) holds what every worktree would otherwise rebuild (d2rAI-toolkit's `shared/classic_import/cache.py`, #440; the toolkit is found as `toolkit_path.py` prints it):

- `pd2_extract.py` (no `--out`; only the gate and `pd2_import.py` run it) extracts once per extract stamp into `extract\<key>\` and makes the worktree's
  `pd2-data` a junction to it. A lock (`extract.lock`) lets one extract run at a time; the others wait, then skip. A worktree whose
  `pd2-data` is still a real folder keeps using it: delete that folder to share the cache.
- `install.ps1` (modkit install) writes only the `out\data` files that differ from the slot's and touches nothing outside
  `<slot>\mods\PD2Resurrected`, so moving a slot between builds is quick.
- Only `pd2_import.py` (through `pd2_merge.py`) uses the rule cache (`rules\<key>\`, pruned to `PD2R_CACHE_GB`, default 20) and
  the merge slots (`PD2R_MERGE_SLOTS`, default 4); a normal build or install runs no merge.
- Removing a worktree: the toolkit's `tools/worktree_remove.ps1` only (`docs/agents/game-check.md`, "Worktree hygiene").
