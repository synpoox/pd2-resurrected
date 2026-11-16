# Release rehearsal: install, upgrade, roll back (#966)

A local, unpublished release candidate built from `cartographer-migration`, rehearsed in one game slot (Slot5, Hell,
level 95+ characters). Nothing was tagged, uploaded or released.

## The candidate

`python tools/release.py --game <D2R> --version 0.3.0-rc.1 --out <dir> --cartographer <cartographer.dll>` stages the mod
folder and hands it to the toolkit's `package_release.py`. Cartographer has no release to download yet, so
`companions/cartographer/pin.json` pins the build by SHA-256 (the d2r-tools commit is in its `source`); `release.py`
ships the dll only when the file matches the pin, and fails when it is missing or differs. Without a pin file nothing
changes.

| | main (0.2.0-alpha) | candidate (0.3.0-rc.1) |
|---|---|---|
| manifest files | 1608 | 1613 |
| core-rules.dll | 5017fda9... | 158adeda... |
| core-maps.dll | 01fa3924... | 464380e2... |
| core-ui.dll | 7d839ef1... | ab729a71... |
| cartographer.dll | none | 7b2763d4... (pin) |

The 7z is not committed. `release.py` also writes a temporary `release.toml` that adds `loader_files` (the toolkit's
schema 3 refuses a catalog without it); the same failure was already in `main`.

## Install and rollback by manifest

The launcher's Update has no headless mode, so `tools/release_rehearsal.py` does what its manifest rules say:
a `managed` file is replaced when its hash differs, a `shipped_default` file only when missing or still the old
default (a player's edit stays), a file the old release owned and the new one does not is deleted, and a plugin either
release shipped is removed from the other plugin scope (`<game>/d2rloader/plugins`). Files in neither manifest, and the
saves, are never touched. `--savepath` keeps the slot's own save folder in both `modinfo.json`. Rolling back is the same
command with the releases swapped. `tools/tests/test_release_rehearsal.py` covers it.

## Results

Each step: the game starts, a character loads, a map item spawns (`cheat spawn m01,typ m` on the candidate, the PD2
`t11` Kurast Map on main).

| Step | Result |
|---|---|
| (a) fresh install of the candidate | pass: Cartographer set `Area Level 85` on the new map item |
| (b) main, then a player edit and a note file, then upgrade to the candidate | pass: verify 0 problems; edited `d2rloader.toml` line, `user-notes-966.txt` and `user-unrelated-966.txt` kept; saves and savepath intact; `cartographer.dll` active; map item spawned |
| (c) roll back to main with the game closed | pass: verify 0 problems; `cartographer.dll` and four candidate-only files removed; edit and user files kept; the saves written in (b) kept byte for byte; savepath unchanged; main's map item spawned, and the scroll from (b) was still in the inventory |

The upgrade deleted nothing in (b): the two releases ship the same plugin names, so the stale-owned cleanup is covered
by the unit test, not by this run.

Screenshots: `issue-assets/966/b-main-map.png`, `c-cand-upgrade-map.png`, `d-rollback-map.png`.

## Seen, not caused here

- `core-maps` logs hook failures at `0x494A60` and `0x50ACF0` when it loads next to Cartographer (the Cartographer log
  says the two cannot be used together). The retained events still install (`mapevents 1/1`).
- Item Durability and Mass Identify fail to load, on main and the candidate alike.

## Deferred

- #965: retiring the converter. Dclone, Rathma, Lucion/Temple and Bitter Depths still use converted levels and refuse
  safely until then.
- #967: publishing (tag, upload, public version).
- #1028 (Horazon's second map) and Desecration.
