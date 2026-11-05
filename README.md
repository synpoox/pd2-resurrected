# pd2-resurrected

A near-1:1 recreation of the latest Project Diablo II as a Diablo II: Resurrected mod (Reign of the Warlock mode only), with D2RLoader plugins for engine mechanics and a Warlock redesigned in PD2's style. Offline single-player first, then private multiplayer.

**Status: building toward the pre-alpha** ([roadmap](docs/roadmap.md)).

- Plan and open decisions: [map issue #1](https://github.com/synpoox/pd2-resurrected/issues/1)
- Decisions: [`docs/adr/`](docs/adr/)
- Terms: [`GLOSSARY.md`](GLOSSARY.md)
- Research notes: [`docs/research/`](docs/research/)

No Blizzard files, PD2 archives or extracted PD2 data are committed here ([ADR 0002](docs/adr/0002-pd2-data-generated-not-committed.md)).

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
pre-alpha). It fails on any difference that [docs/unported.md](docs/unported.md) does not list; paste its output into
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
D2R folder. The repository is public, so set Settings > Actions > General > "Fork pull request workflows from
outside collaborators" to "Require approval for all external contributors": a fork's PR could otherwise edit a
workflow to run on this runner.

## Plugins

`plugin/` builds the D2RLoader plugins (ADR 0004); `core-rules` holds PD2's engine rules, `core-maps` PD2's map system. Engine addresses live in one header per D2R build (`plugin/common/rvas_93847.h`).

```powershell
git submodule update --init
powershell -ExecutionPolicy Bypass -File plugin\build.ps1
powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game <D2R folder> [-Data <modkit project>]
```

Game checks for d2rAI-toolkit's `game_checks.py` are in `tests/game/` (setup and run in `core_rules_checks.py`).
