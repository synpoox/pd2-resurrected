# Project Diablo II: Resurrected

Project Diablo II rebuilt as a Diablo II: Resurrected mod.

[Discord](https://discord.com/invite/KWGqVxmpDR) | [Launcher](https://github.com/synpoox/resurrected-launcher)

**Status: alpha 0.2.1** ([roadmap](docs/roadmap.md), [changelog](CHANGELOG.md), [how it differs from PD2](docs/deviations.md)).

## Requirements

- Windows and Diablo II: Resurrected 3.4.0 (build 95032) or 3.3.0 (build 93847) with Reign of the Warlock. One
  release runs on both: D2RLoader runs its own 3.3 engine image with the installed game's data.
- [D2RLoader](https://d2rloader.net/) 1.3.1-beta.

## Install

### Launcher
1. Download and install the latest launcher: https://github.com/synpoox/resurrected-launcher
2. Go to `Settings` and select your D2R directory.
3. Click `Install` from the main screen. 

### Manual
1. Unpack `PD2Resurrected-<version>.7z` into `<D2R folder>\mods\`, so that
   `mods\PD2Resurrected\PD2Resurrected.mpq\modinfo.json` exists.
2. Make `PD2Resurrected` D2RLoader's default mod (once): in D2RLoader's settings, or with
   `default_mod = "PD2Resurrected"` in `<D2R folder>\d2rloader\config\d2rloader.toml`.
3. Start `D2RLoader.exe`. Characters are saved in `Saved Games\Diablo II Resurrected\mods\PD2Resurrected\`.

To update, delete `mods\PD2Resurrected\PD2Resurrected.mpq\data` first, also delete `mods\PD2Resurrected\d2rloader\plugins\core-maps.dll` if it is there (the old name of `core-events.dll`; both loaded together would hook the game twice), then unpack the new release the same way.

### Linux

Runs under Wine/Proton with the manual install: [docs/linux.md](docs/linux.md).

## Loot Filters

The mod ships the [Pyre Loot Filter](https://github.com/Pyre-Flies/D2R-Mods/blob/main/plugins/pyre-lootfilter/README.md)
plugin with a default `filter.json`.

- Edit or make a filter: [D2R Reimagined loot filter editor](https://www.d2r-reimagined.com/loot-filter).
- Default filter: [filter.json](https://raw.githubusercontent.com/synpoox/pd2-resurrected/main/companions/filter.json).
- Install: save it as `<D2R folder>\mods\PD2Resurrected\d2rloader\config\filter.json`. It reloads on save.

A new release overwrites `filter.json`; keep a copy of yours.

## Controller

Every controller control the mod ships, with its source and status: [docs/controller.md](docs/controller.md).

The mod ships the Controller QOL Updates plugin (PyreFly), which changes some of D2R's controller buttons:

- RT changes the main menu page and RB changes the skill tab. This is the reverse of normal D2R. The config file can't switch it back; the console command `qol menu on/off` does.
- Hold LB to label the items on the ground with X, A and Y. Then LB+X, LB+A or LB+Y picks that item up. Potions go to the belt.
- The right stick moves the aim cursor. It snaps to monsters near the cursor.

## Cut features

These PD2 features are left out on purpose. Every deviation from PD2, cut or not, is listed in
[docs/deviations.md](docs/deviations.md).

- PD2's stash pages: D2R's stash stays for now.
- PvP: the PvP maps, the BR Arena and every PvP rule, including the arenas' life regen cap.
- Corrupted zones: D2R's Terror Zones stand in.
- PD2's Uber Ancients: D2R's own Uber Ancients stays.
- Chat commands: `.kills`, `.deaths`, `.played`, `.age`, `.allstats`, `.accountstats` and `.noexp`.

## Bug reports

Found a bug? [Open an issue](https://github.com/synpoox/pd2-resurrected/issues/new) and say:

- what happened, and what you expected;
- how to make it happen again (class, level, difficulty, area, skill or item);
- the mod version (in the release's file name, e.g. `0.1.0-alpha`).

First check the [deviation list](docs/deviations.md): what is on it is known and not a bug yet.

## Contributing

Want to help? See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT for our own work: see [LICENSE](LICENSE). It does not cover Blizzard's game data, Project Diablo 2's content or the bundled companion plugins; see [NOTICE.md](NOTICE.md).
