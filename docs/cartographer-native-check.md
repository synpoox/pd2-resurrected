# Native catalogue check (#952, lane A)

Game check of the five native Cartographer maps (m01-m05), 2026-11-15, Slot4, Hell, TESTPAL (level 99), build of
`cartographer-migration` at d929925 plus this PR. Images: `issue-assets` branch, `952/`.

| Check | Result |
|---|---|
| Item reads (name, area level, native property text) | Pass: "Hastened Bone Vaults Map / Area Level: 85 / +16% Monster Speed" |
| Acquisition by `cheat spawn m01..m05` and opening in Harrogath (Hell) | Pass for all five (red portal, "Entering <name>") |
| Tunneling Depths (Cave, slot 202), Bone Vaults (Crypt, 203), Frozen Hollows (Ice, 205), Overgrown Reach (Jungle, 206), Frostbitten Fields (Snow, 207) | Pass: each log line "built as planned"; arrival and area text seen |
| Entrance to boss room | Bone Vaults only: warped to the boss room, loaded and populated. Others: skipped (log reports boss and event rooms known) |
| Return path | Pass for Frozen Hollows: the arrival portal leads back to Harrogath |
| HD / legacy (`g`) | Pass on Bone Vaults (name, monster level and property text show in both) |
| Panel / name text | Pass: map name and "Monster level 85 / Monsters have ..." text on the automap panel |
| Teardown (Save and Exit, stop) | Pass: no new crash file, no plugin error line |
| Terror Zone restoration, ordinary-area regressions | Not checked: no ordinary area visited after a map except Harrogath (normal) |
| Frame time / memory | Skipped: no matched before/after build was cheap to get (old toolkit copy needed to build, below) |

Deviations seen (not caused by this PR, not fixed here):

- `cartographer-report.txt` warns "core-maps is loaded next to Cartographer ... cannot be used together". Both load in the slot; map opening still worked. Belongs to the shared checkpoint (#959).
- Item tooltip says "+16% Monster Speed", the in-map text says "+16% Movement Speed".
- Overgrown Reach's plan is 11 stamps with 144 void cells of 320.
- Frostbitten Fields: the arrival spot sits on a wall segment.
- `plugin/common/pd2_engine.h` redefines RVAs the current d2rAI-toolkit (`rvas_93847/tables.h`) now holds, so `plugin/build.ps1` fails against a current toolkit. The check built with `D2R_TOOLKIT=d2rAI-toolkit-669f42a`. Needs a shared-file sync.
