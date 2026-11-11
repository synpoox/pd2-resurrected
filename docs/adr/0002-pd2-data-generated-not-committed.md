# PD2 data is generated at build time, never committed

The mod's PD2 tables are not stored in this repo. `tools/pd2_extract.py` reads the local PD2 install into git-ignored `pd2-data/`, refusing any install whose MPQ hashes differ from the committed `pd2-baseline.json`; `tools/pd2_merge.py` applies the committed rules in `merge/` (ID remaps, event names, function-number rewrites, D2R-only rows kept or off) and writes git-ignored `generated/data/`, which `modkit build` layers after `files/` and before `patches/`. So every build needs the PD2 install on the machine, and CI never builds the mod: it runs the privacy scan and tool unit tests on synthetic fixtures only.

## Considered Options

- Commit generated patches: builds anywhere and CI could build, but puts PD2 data in git, which the project forbids.
- Write merge output into git-ignored parts of `files/`: no toolkit change, but mixes generated and hand-made files and lets a stale merge win silently.

## Consequences

- The table-diff gate and game checks run locally; a PR carries the pasted `pd2_diff` result.
- Re-baselining PD2 is a commit that changes `pd2-baseline.json` and whatever `merge/` rules the new data breaks.
- `modkit build` gains an optional `generated/data/` layer in d2rAI-toolkit.

## Amendments

- 2026-11-04 ([#151](https://github.com/synpoox/pd2-resurrected/issues/151)): the rule covers PD2's level files too. `pd2_extract` reads the DS1 and DT1 that PD2-only areas use from `pd2maps.mpq` and `pd2assets.mpq`, under the same baseline hash check; the merge rewrites them and writes the HD presets generated from them (ADR 0005) to `generated/`. None of them is committed.
- 2026-11-10 ([#833](https://github.com/synpoox/pd2-resurrected/issues/833)): the table-diff gate now builds on a self-hosted runner that has the PD2 install; the local `pd2_diff` paste is the fallback when runners are offline.
