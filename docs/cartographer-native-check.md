# Native catalogue check (#952, lane A)

Game check of the five native Cartographer maps (m01-m05), 2026-11-15, Slot4, Hell, TESTPAL (level 99), build of
`cartographer-migration` (d929925, then b6aec9b for the second pass). Images: `issue-assets` branch, `952/`. Image names (s4, m3-in, b1, ...) are screenshot labels, not item codes.

| Check | Result |
|---|---|
| Item reads (name, area level, native property text) | Pass: "Hastened Bone Vaults Map / Area Level: 85 / +16% Monster Speed" |
| Acquisition by `cheat spawn m01..m05` and opening in Harrogath (Hell) | Pass for all five (red portal, "Entering <name>") |
| Tunneling Depths (Cave, slot 202; now 138), Bone Vaults (Crypt, 203; now 139), Frozen Hollows (Ice, 205; now 141), Overgrown Reach (Jungle, 206; now 142), Frostbitten Fields (Snow, 207; now 143) | Pass: each log says "built as planned" (Tunneling Depths 12 rooms; Bone Vaults 13 rooms; Frozen Hollows 12 rooms; Overgrown Reach 15 stamps, 336 void of 576 cells, 240 rooms; Frostbitten Fields 48 stamps, 0 void, 192 cells); arrival and area text seen |
| Entrance to boss room | Bone Vaults, Tunneling Depths (`cheat goto 202,17802,422`, now level 138, walked) and Overgrown Reach (plan cell (0,0), Berserker Slayer boss present, walked): loaded and populated. Frozen Hollows, Frostbitten Fields: skipped: owner scope cut 2026-11-15 |
| Return path | Pass for Frozen Hollows: the arrival portal leads back to Harrogath |
| HD / legacy (`g`) | Pass on Bone Vaults (name, monster level and property text show in both) |
| Panel / name text | Pass: map name and "Monster level 85 / Monsters have ..." text on the automap panel |
| Teardown (Save and Exit, stop) | Pass: no new crash file, no plugin error line |
| Ordinary areas after a map | Pass after Tunneling Depths: Blood Moor and Den of Evil show their own name, Hell and no map text on the automap |
| Terror Zone restoration | Skipped: owner scope cut 2026-11-15. Seen once: with `cheat desecrate 109` and `cheat seed 1` set, the Frozen Hollows portal answered "I can't" (not reproduced without them; cause unknown) |
| Deterministic rebuild | Skipped: owner scope cut 2026-11-15. `cheat seed 7` did not pin the map: Tunneling Depths opened twice got maze seeds 8439465493737898331 and 13976776300227222905 |
| Max size | Only Medium is enabled in `maps.txt` |
| One map per game | A second map item answers "a map has already been opened in this game" (by design) |
| Frame time / memory / load / teardown | Skipped: tracked in #1022 |

Deviations seen (not caused by this PR, not fixed here):

- `cartographer-report.txt` warns "core-maps is loaded next to Cartographer ... cannot be used together". Both load in the slot; map opening still worked. Belongs to the shared checkpoint (#959).
- Item tooltip says "+16% Monster Speed", the in-map text says "+16% Movement Speed": synpoox/d2r-tools#483.
- `EventChance` 0 with the forced core-maps event is by design (#999): the log says "event ... chance 0%: none" while core-maps rolls its own event.
- Frostbitten Fields (Snow): the arrival put the portal and player against a wall (`m05-arr-a`); another seed arrived on open ground (`m05-arr-b`). Shape is ignored for Snow, so the blank Shape cell is not the cause; it is Cartographer's Snow planner or arrival placement: synpoox/d2r-tools#484.
- `plugin/common/pd2_engine.h` redefined RVAs the d2rAI-toolkit (`rvas_93847/tables.h`) holds, so this check was built against an older toolkit; `plugin/build.ps1` now builds against the commit `toolkit.version` pins.
