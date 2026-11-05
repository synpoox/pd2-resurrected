# PD2 Resurrected

Project Diablo II rebuilt as a Diablo II: Resurrected mod: PD2's items, skills, monsters and rules, in D2R's
graphics. Offline single-player for now; private multiplayer comes later.

**Status: alpha 0.1.0** ([roadmap](docs/roadmap.md), [changelog](CHANGELOG.md), [what is not ported yet](docs/unported.md)).

## Requirements

- Windows and Diablo II: Resurrected 3.3.0 (build 93847) with Reign of the Warlock.
- [D2RLoader](https://d2rloader.net/) 1.3.1-beta. Characters load only through it.

## Install

1. Close the game.
2. Unpack `PD2Resurrected-<version>.7z` into `<D2R folder>\mods\`, so that
   `mods\PD2Resurrected\PD2Resurrected.mpq\modinfo.json` exists.
3. Make `PD2Resurrected` D2RLoader's default mod (once): in D2RLoader's settings, or with
   `default_mod = "PD2Resurrected"` in `<D2R folder>\d2rloader\config\d2rloader.toml`.
4. Start `D2RLoader.exe`. Characters are saved in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected\`.

To update, delete `mods\PD2Resurrected\PD2Resurrected.mpq\data` first, then unpack the new release the same way.

## Loot Filters

The mod comes with the [Pyre Loot Filter](https://github.com/Pyre-Flies/D2R-Mods/blob/main/plugins/pyre-lootfilter/README.md)
plugin and a default filter, `filter.json`. The filter hides, renames and recolors ground items, plays drop sounds
and draws minimap icons. Hidden items can still be picked up by holding Ctrl.

To make or edit a filter, use the [D2R Reimagined loot filter editor](https://www.d2r-reimagined.com/loot-filter):
start from a preset or a blank filter, or browse the public filters, then save the result as `filter.json`.

To install a filter by hand:

1. Get a `filter.json`: one from the editor, or the default from
   [here](https://raw.githubusercontent.com/synpoox/pd2-resurrected/main/companions/filter.json) (right-click, Save
   as).
2. Put it at `<D2R folder>\mods\PD2Resurrected\d2rloader\config\filter.json`, replacing the one there.
3. The plugin reloads it as soon as the file changes, even in game. To reload it yourself, press Ctrl+Shift+F9 or
   type `/filter reload`.

Unpacking a new release puts the default filter back, so keep a copy of your own.

## Bug reports

Found a bug? [Open an issue](https://github.com/synpoox/pd2-resurrected/issues/new) and say:

- what happened, and what you expected;
- how to make it happen again (class, level, difficulty, area, skill or item);
- the mod version (in the release's file name, e.g. `0.1.0-alpha`).

First check the [unported list](docs/unported.md): what is on it is known and not a bug yet.

## For developers

- Plan and open decisions: [map issue #1](https://github.com/synpoox/pd2-resurrected/issues/1)
- Decisions: [`docs/adr/`](docs/adr/)
- Terms: [`GLOSSARY.md`](GLOSSARY.md)
- Research notes: [`docs/research/`](docs/research/)

No Blizzard files, PD2 archives or extracted PD2 data are committed here ([ADR 0002](docs/adr/0002-pd2-data-generated-not-committed.md)).

## Contributing

Pull requests are welcome. Every PR links its issue: the body opens with `Closes #<n>` (or `Part of #<n>`); open an
issue first if there is none. Run the tool tests before you push, and add a line to `CHANGELOG.md` for anything a
player would notice. Changes that alter gameplay need an in-game check (Hell, a level 90+ character). How agents and
humans work here: [AGENTS.md](AGENTS.md).

## Build

Needs the PD2 install named in `pd2-baseline.json` and the sibling `..\d2rAI-toolkit` at commit `ec17a9a` or later
(modkit's `generated/data/` layer).

```bash
python tools/pd2_extract.py
```

```bash
python -m modkit fetch --prefix data/global/tiles/ --prefix data/hd/env/preset/
```

```bash
python tools/pd2_merge.py
```

```bash
python tools/companions.py fetch
```

```bash
python -m modkit build .
```

```bash
python tools/companions.py install --game DIR
```

Run the `modkit` fetch and the last two with `..\d2rAI-toolkit` on `PYTHONPATH`. The fetch (once per D2R build)
caches D2R's DS1, DT1 and HD presets, which the merge harvests for the HD presets of PD2-only areas
(`tools/pd2_presets.py`). `companions.py fetch` downloads the pinned companion plugins in
`companions/manifest.json` (checked by SHA-256, never committed) and must run after `pd2_merge.py`, which wipes
`generated/`. `install` is `modkit install` plus the companions, our `companions/filter.json`, and the vanilla
item tables the loot filter needs when the build doesn't ship them. The merge and the tool tests find modkit in
`..\d2rAI-toolkit` (or on `PYTHONPATH`). Tool tests (no PD2 data needed):

```bash
python -m unittest discover -s tools/tests
```

The table-diff gate compares the merged tables with PD2's, scoped to a work item (P2, P3, P4) or milestone (M1,
alpha). It fails on any difference that [docs/unported.md](docs/unported.md) does not list; paste its output into
the PR:

```bash
python tools/pd2_diff.py --scope P2
```

## Release

`tools/release.py` packages the built mod (the whole build above, plus the plugins below) with d2rAI-toolkit's
`release/package_release.py` and `release.toml`: `dist/PD2Resurrected-<version>.7z`, release notes with the changelog
section and the unported list, and `SHA256SUMS`. It needs 7-Zip, and a `### <version> (<date>)` section in
`CHANGELOG.md` for mod.json's version: rename `### Unreleased` on release day.

```bash
python tools/release.py --game DIR
```

The `release` workflow (Actions > release > Run workflow) runs the whole build and this script and keeps the 7z as
a workflow artifact; nothing is published. It needs the PD2 install and D2R, so it runs only on a self-hosted
Windows runner with the labels `self-hosted, windows, pd2r` (Settings > Actions > Runners > New self-hosted runner;
add the `pd2r` label when `config.cmd` asks). That machine needs the PD2 install named in `pd2-baseline.json`, Python
3.13 with `py`, CMake and Visual Studio, and 7-Zip. Set two repository variables (Settings > Secrets and variables >
Actions > Variables): `D2R_TOOLKIT`, the d2rAI-toolkit checkout (its `d2r-data/` cache is reused), and `D2R_GAME`, the
D2R folder. The repository is private, so only its collaborators can run workflows on this runner; if it ever goes
public, first set Settings > Actions > General > "Fork pull request workflows from outside collaborators" to "Require
approval for all external contributors", or a fork's PR could edit a workflow to run on it.

## Plugins

`plugin/` builds the D2RLoader plugins (ADR 0004); `core-rules` holds PD2's engine rules, `core-maps` PD2's map system. Engine addresses live in one header per D2R build (`plugin/common/rvas_93847.h`).

```powershell
git submodule update --init
powershell -ExecutionPolicy Bypass -File plugin\build.ps1
powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game <D2R folder> [-Data <modkit project>]
```

Game checks for d2rAI-toolkit's `game_checks.py` are in `tests/game/` (setup and run in `core_rules_checks.py`).
