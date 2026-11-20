# Target Reign of the Warlock for PD2 gameplay

PD2 is a Lord of Destruction mod. The port targets D2R's Reign of the Warlock (RotW) mode for gameplay and
keeps the Warlock as a class designed new in PD2's style. Classic and Lord of Destruction use their native
class and act rules. Their shared table bank (`data/global/excel/base/`) ships the mod's inventory geometry to
match the shared UI. The remaining base tables stay vanilla. The mod uses its existing extended save format
and save folder for all three modes. Players start with fresh characters. Character upgrades and imports
remain unsupported.

## Considered Options

- LoD only: closest to PD2, but loses the Warlock.
- Both modes with identical banks: protects players who pick the wrong mode, but doubles what must be kept in step and tested.

## Consequences

- Keep the inventory geometry in both banks in sync with the shared UI.
- PD2 rows are merged around D2R's RotW rows; D2R-only rows (e.g. itemtypes `warl` at 67) stay at D2R's numbers.
- D2R-only content stays on unless turned off item by item by approval in `docs/features/d2r-only-content.md`.
