# Release rehearsal: install, upgrade, roll back (#966)

A local, unpublished release candidate built from `cartographer-migration`, rehearsed in one game slot (Slot5, Hell,
level 95+ characters). Nothing was tagged, uploaded or released.

## The candidate

`python tools/release.py --game <D2R> --version 0.3.0-rc.1 --out <dir> --cartographer <cartographer.dll>
[--cartographer-commit <sha>]` stages the mod folder and hands it to the toolkit's `package_release.py`. Cartographer is
not pinned: a release ships the latest build, passed with `--cartographer` (or `CARTOGRAPHER_DLL`). `release.py` fails
only when no dll is given or the file is missing, and records `Cartographer: d2r-tools <commit>, sha256 <hash>` at the
end of the release notes (the commit comes from `--cartographer-commit` or `CARTOGRAPHER_COMMIT`, and is left out when
not given).

| | main (0.2.0-alpha) | candidate (0.3.0-rc.1) |
|---|---|---|
| manifest files | 1608 | 1613 |
| core-rules.dll | 5017fda9... | 158adeda... |
| core-maps.dll | 01fa3924... | 464380e2... |
| core-ui.dll | 7d839ef1... | ab729a71... |
| cartographer.dll | none | built from d2r-tools d78952d |

The candidate ships `core-rules`, `core-maps` and `core-ui` plus Cartographer; there is no separate core-events (the
retained map events live in core-maps). The 7z is not committed; the hashes above are from the rehearsal run, whose dll was
a build of a stale local merge commit (the load check on d2r-tools d78952d passed afterwards).

Update (#967): this table is the 0.3.0-rc.1 rehearsal as run. Since #967 the plugin is `core-events` (`core-maps` was renamed
and its map code removed), so a current candidate ships `core-rules`, `core-events`, `core-ui` and Cartographer, and
the `core-maps` rows and log lines below are historical. The DLL was renamed, but the plugin id stayed `core-maps` (#1044), so current logs may still say `core-maps`.

`--game` must hold the official `D2RLoader.exe`: `release.py` writes a temporary `release.toml` that adds
`catalog.loader_files` for it, pinned at `[d2rloader] version` (the toolkit's schema 3 refuses a catalog without it;
the same failure was already in `main`). It fails when `[catalog]` or that version is missing, or when `release.toml`
sets `loader_files` itself.

## Build latest Cartographer from d2r-tools main and pass it to release.py

1. Build from a d2r-tools `origin/main` commit in a scratch clone (never the owner's worktrees):
   `git clone https://github.com/synpoox/d2r-tools <dir>`, `git submodule update --init third_party/PluginSDK`, then
   `cmake -S <dir>/plugins/cartographer -B <build> -A x64` and `cmake --build <build> --config Release`
   (the dll is `<build>/Release/cartographer.dll`; build outside the system temp folder if MSBuild warns).
2. `python tools/release.py ... --cartographer <build>/Release/cartographer.dll --cartographer-commit <full sha>`.
3. Start the game once in a slot with the dll and check `cartographer.log` loads it.

## Arena status

Vanilla mini-ubers and D2R Tristram (136) work on native maps. Dclone, Rathma, Lucion/Temple and Bitter Depths are
refused safely (#965 retired the converted levels; new arenas come later).

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

- `core-maps` (now `core-events`, #967) logged a hook failure at `0x50ACF0` during this rehearsal (the `0x494A60` one is fixed, #1032) when it loaded next to Cartographer. Current code no longer hooks `0x50ACF0`; Cartographer owns it (see `docs/deviations.md`) (the Cartographer log
  says the two cannot be used together). The retained events still install (`mapevents 1/1`).
- Item Durability and Mass Identify fail to load, on main and the candidate alike.

## Deferred

- #967: publishing (tag, upload, public version).
- #1028 (Horazon's second map) and Desecration.
