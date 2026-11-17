# Contributing

PD2 Resurrected is a Diablo II: Resurrected mod; players: see the [README](README.md).

## For developers

- Plan and open decisions: [map issue #1](https://github.com/synpoox/pd2-resurrected/issues/1)
- Decisions: [`docs/adr/`](docs/adr/)
- Terms: [`GLOSSARY.md`](GLOSSARY.md)
- Research notes: [`docs/research/`](docs/research/)

No Blizzard files or PD2 archives are committed here; the mod's data is, in `files/data/` ([ADR 0007](docs/adr/0007-mod-data-committed-tables-are-the-source.md)).

## Pull requests

Pull requests are welcome. Every PR links its issue: the body opens with `Closes #<n>` (or `Part of #<n>`); open an
issue first if there is none. Run the tool tests before you push, and add a `changelog.d/<issue>-<slug>.md` fragment (see AGENTS.md) for anything a
player would notice. Changes that alter gameplay need an in-game check (Hell, a level 90+ character). How agents and
humans work here: [AGENTS.md](AGENTS.md).
Contributions are under the MIT license ([LICENSE](LICENSE)).

## Build

Needs D2R and the d2rAI-toolkit at the commit in `toolkit.version`. The tools, `plugin\build.ps1` and `tools\install.ps1` find the toolkit with its own finder (`D2R_TOOLKIT`, else the nearest `d2rAI-toolkit` folder up the tree, so agent worktrees find it) and warn when its HEAD differs from the pin (`D2R_TOOLKIT_STRICT=1` makes that an error). Bump the pin on purpose, in a commit named `chore: pin d2rAI-toolkit to <sha>`. The mod's data is committed in `files/data/`:
no extract or merge runs per build.

```bash
python <toolkit>/release/companions.py fetch
```

```bash
python -m modkit build .
```

```bash
python <toolkit>/release/companions.py install --game DIR
```

`<toolkit>` is the d2rAI-toolkit checkout the finder returns. Run `modkit` and the last command with it on `PYTHONPATH`.
`companions.py fetch` downloads the pinned companion plugins in
`companions/manifest.json` (checked by SHA-256, never committed; an entry pins a GitHub release asset (`repo`, `tag`),
a file at a repo commit (`commit`), or a direct download link (`url`, e.g. a D2RLoader Hub release)). `install` is
`modkit install` plus the companions, our `companions/filter.json`, plugin settings
(`companions/config/`), Cartographer's `signatures.txt` (manifest `project_files`), and the vanilla
item tables the loot filter needs when the build doesn't ship them (manifest `filter_tables`). The tool tests find modkit through the same finder (or on `PYTHONPATH`).

### Importing a new PD2 version

Only when PD2 releases a version; the PD2 install in `pd2-baseline.json` is needed. A PD2 frame with no HD art in
`merge/art/` needs [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN/releases) (`realesrgan-ncnn-vulkan`) in the
sibling `..\realesrgan-ncnn-vulkan` or named by `REALESRGAN`, and numpy and Pillow for skill icons.

1. Update `pd2-baseline.json`.
2. From the ticket-branch checkout (the `pd2-import` branch has no `tools/`), run `python tools/pd2_import.py --out <a worktree of the kept pd2-import branch>`, and commit it there. New `merge/art/` PNGs the run writes are committed on the ticket branch.
3. On a ticket branch from main, run `git diff --binary pd2-import~1 pd2-import | git apply --3way`.
4. Resolve the conflicts and run the gate. The PR diff is the import.

Tool tests (no PD2 data needed):

```bash
python -m unittest discover -s tools/tests
```

The table-diff gate compares the merged tables with PD2's, scoped to a work item (P2, [P3](https://github.com/synpoox/pd2-resurrected/issues/47), [P4](https://github.com/synpoox/pd2-resurrected/issues/48)) or milestone (M1,
alpha). It fails on any difference that [docs/deviations.md](docs/deviations.md) does not list. The `gate` workflow
runs it at scope alpha on every PR from this repository's branches, on the self-hosted runner described under Release. Docs-only changes (`*.md`, `docs/`, `changelog.d/`, except the gate input `docs/deviations.md`) start no run, and closing or merging a PR cancels its running gate.
For a work item's own scope, paste its output into the PR:

```bash
python tools/pd2_diff.py --scope P2
```

## Release

The toolkit's `release/stage.py` packages the built mod (the whole build above, plus the plugins below) with
`release/package_release.py` and `release.toml`: `dist/PD2Resurrected-<version>.7z`, release notes with the changelog
section and the deviation list, and `SHA256SUMS`. It needs 7-Zip, and a `### <version> (<date>)` section in
`CHANGELOG.md` for mod.json's version: rename `### Unreleased` on release day.

```bash
python tools/release_precheck.py   # refuses an out/data with the retired converter's output
python <toolkit>/release/stage.py --game DIR --extra-plugin "<cartographer.dll>@d2r-tools <commit>"
```

The `release` workflow (Actions > release > Run workflow) runs the whole build and this script, then publishes a GitHub
release `v<version>` with the 7z, the notes and `SHA256SUMS`, marked Latest (a pre-release would not show on the
repository page). It fails if that version already has a release. It needs the PD2 install and D2R, so it runs
only on a self-hosted Windows runner with the labels `self-hosted, windows, pd2r` (Settings > Actions > Runners > New self-hosted runner;
add the `pd2r` label when `config.cmd` asks). That machine needs the PD2 install named in `pd2-baseline.json`, Python
3.13 with `py`, CMake and Visual Studio, and 7-Zip. Set two repository variables (Settings > Secrets and variables >
Actions > Variables): `D2R_TOOLKIT`, the d2rAI-toolkit checkout (its `d2r-data/` cache is reused; each job runs against the commit `toolkit.version` pins, in its own worktree under `C:\pd2r-cache\toolkit`, made by `tools/ci_toolkit.ps1`, so the checkout itself is only fetched, never moved; the toolkit tests and the gate, `tests` and `release` workflows all run on this runner), and `D2R_GAME`, the
D2R folder. The repository is public, so Settings > Actions > General > "Fork pull request workflows from outside
collaborators" must stay on "Require approval for all external contributors": a fork's PR could otherwise edit a
workflow to run on this runner. Read a fork PR's workflow changes before approving its run.

## Plugins

`plugin/` builds the D2RLoader plugins (ADR 0004); `core-rules` holds PD2's engine rules, `core-events` PD2's map events and uber fights (maps are Cartographer's). Shared code (log, guards, the `d2r::api` binding and engine addresses other projects use) comes from d2rAI-toolkit's `shared/plugin-common`, found by the toolkit finder (`D2R_TOOLKIT` or `-DD2R_TOOLKIT=<checkout>`, else a walk up), together with the PluginSDK in its `third_party/PluginSDK` (this repo has no PluginSDK copy of its own); the addresses only PD2 uses live in `plugin/common/pd2_engine.h`.

```powershell
powershell -ExecutionPolicy Bypass -File plugin\build.ps1
powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game <D2R folder> [-Data <modkit project>]
```

Game checks for d2rAI-toolkit's `game_checks.py` are in `tests/game/` (setup and run in `core_rules_checks.py`).
