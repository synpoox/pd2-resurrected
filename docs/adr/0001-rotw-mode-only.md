# Target Reign of the Warlock mode only

PD2 is a Lord of Destruction mod, but the port targets D2R's Reign of the Warlock (RotW) game mode only. The mod writes only the RotW table bank (`data/global/excel/`); the Classic/LoD bank (`excel/base/`) stays vanilla, so LoD or Classic characters play plain D2R and are unsupported. RotW is the mode with the Warlock, which the port keeps as a class designed new in PD2's style. There are no conversions: no LoD-to-RotW upgrade path and no import of PD2 `.d2s` characters; everyone starts fresh.

## Considered Options

- LoD only: closest to PD2, but loses the Warlock.
- Both modes with identical banks: protects players who pick the wrong mode, but doubles what must be kept in step and tested.

## Consequences

- PD2 rows are merged around D2R's RotW rows; D2R-only rows (e.g. itemtypes `warl` at 67) stay at D2R's numbers.
- D2R-only content stays on unless turned off item by item by approval in `docs/d2r-only-content.md`.
