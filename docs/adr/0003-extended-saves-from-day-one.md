# Extended save format from day one

`mod.json` sets `"extendedSaves": true` from the first milestone, so characters load only with D2RLoader. Retail saves cannot hold the port: every PD2 class has 33 class skills (retail stores 30), and PD2's stats appended after D2R RotW's 368 pass the 511 that retail's 9-bit stat ids allow. Turning it on later would invalidate every character made before.

Every id the port adds (stats, skills, states, item types, anything a save stores by number) is assigned once in `merge/` and is append-only, so rebuilds and PD2 re-baselines never renumber a saved id. Until the first private-multiplayer release a build may break characters, noted in `CHANGELOG.md`; after it, an unavoidable break changes `savepath` so old characters stay with the old build. PD2's stash lives in D2R's own personal and shared stashes (softcore and hardcore apart), extended with D2RLoader inventory pages; search, dropdown tabs and other stash features are plugins, never a stash file of our own.

## Considered Options

- Retail saves, cutting the 31st-33rd skills and squeezing stats: breaks the "gameplay data exact" tier for every class.
- A plugin-owned stash file: full control, but our own format to version and keep uncorrupted.

## Consequences

- Test items come from D2RLoader's Item Spawner; `modkit save-set` runs with `--extended`.
- If D2RLoader inventory pages cannot hold PD2's stash, the fallback is D2R's stock stash with the gap on the deviation list, not a custom file.
