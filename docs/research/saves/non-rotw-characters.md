# Classic and LoD characters (#1825)

## The save failure

A Classic or Lord of Destruction (LoD) character reads D2R's base table bank, where `itemstatcost.txt` has 368 rows
(ids 0-367). core-rules kept PD2's op bases `equipped_eth` (368) and `missing_hp` (369) as base stats on every player
from the stat callback (`RefreshOpBase`, `plugin/core-rules/item_rules.cpp`). A LoD player whose max life changed while
hurt (a level-up, an item worn) got stat 369. That id is past the bank, so D2RLoader's extended save rejected the
character, and every save after it, until the game closed:

```
Character save failed before the disk write: character="lodsave" mod="PD2Resurrected" format=extended native-result=1 encoding-error=yes ...
Save batch cancelled. Existing files were not replaced. ...
```

The `.ctl`, `.ma0` and shared stash files still change, so a player sees files being written. The `.d2s` keeps its
last good state, and the character loses everything since then. Owning RotW makes no difference: the repro used a LoD
character made with Game Version "Expansion" on a RotW account.

Fix: `InBank` reads the unit's bank (`d2r::api.unitDataFlag`, the same value as the SDK's `DataTables::Bank`:
1 Classic, 2 LoD, 3 RotW). It counts that bank's ItemStatCost rows through the SDK's DataTableService and skips a
stat past them. core-rules logs `op bases: data flag 2 reads 368 itemstatcost rows: no equipped_eth or missing_hp
there` for LoD, and `data flag 3 reads 653 itemstatcost rows` for RotW, where the op bases still run.
LoD players lose nothing from that gate today: equipped_eth and missing_hp only feed PD2 items' op stats.

## One set of tables for every bank (#1853)

Owner decision: Classic and LoD characters play the full mod, the same as RotW; existing Classic/LoD saves may break.
D2R reads `data/global/excel/` for RotW and `data/global/excel/base/` for both Classic (data flag 1) and LoD (2). The
loader has no way to point a bank at another folder (the SDK's DataTableService is read only), and forcing a unit's
data flag to 3 would also switch on RotW-only engine rules (stash, Warlock, save layout). So the build ships our
tables twice: `tools/mirror_base_bank.py` runs after `modkit build` (in `tools/install.ps1` and the release workflow)
and copies every `out/data/global/excel/*.txt` into `excel/base/`. A table committed in `files/data/global/excel/base/`
wins: only `inventory.txt` stays there, the Classic/LoD panel geometry (#1548). `armor.txt` and `weapons.txt` left
`base/`, so both banks number items the same way.

With that, every id a plugin writes (stats past 367, states past 231, PD2 skills, monsters, levels) is in every bank,
and #1855's write guards are not needed; `InBank` stays as a safety net.

Save break, seen on Slot5: the Classic/LoD shared stash (`SharedStashSoftCoreV2.d2i`; Hardcore has its own) written
under the old tables made every new LoD character fail its first save (`Game error screen: reason=32
(D2RExtraSaveFailure)`, a 403-byte `.d2s`). With that file moved aside, a fresh LoD character saved and reloaded. Old
Classic/LoD characters log `Offline character disk refresh failed ... result=record-add-failed` at character select.
Players must start Classic/LoD characters fresh and move that stash file out of the save folder.

What a Classic or LoD character still can't have:

| RotW-only | Why | Handling |
|---|---|---|
| The Warlock | character creation offers it in RotW only | none; ADR 0001 |
| RotW's shared stash pages | Classic/LoD have 3 stock shared pages | #1854 |
| Act 5, the Druid and Assassin, and rows with `version` 100 (Classic only) | the engine filters expansion content in Classic games | none: play LoD for the expansion |

## Gaps between the banks (before #1853)

Before #1853 the mod shipped only `armor.txt`, `weapons.txt` and `inventory.txt` in `excel/base/` (#1548). For every
other table, a Classic or LoD character played D2R's own base rows. Row counts (data rows; D2R 95032):

| Table | D2R base | D2R RotW | Mod (RotW) | In `base/` |
|---|---|---|---|---|
| itemstatcost | 368 | 368 | 653 | no |
| states | 232 | 232 | 291 | no |
| skills / skilldesc | 427 / 232 | 429 / 263 | 683 / 312 | no |
| missiles | 736 | 742 | 1125 | no |
| misc / itemtypes | 169 / 109 | 170 / 110 | 309 / 248 | no |
| properties | 285 | 285 | 473 | no |
| magicprefix / magicsuffix / automagic | 722 / 786 / 44 | same | 1051 / 1084 / 66 | no |
| uniqueitems / setitems / runes | 427 / 141 / 181 | 439 / 141 / 181 | 522 / 142 / 213 | no |
| cubemain | 155 | 227 | 2328 | no |
| treasureclassex | 1138 | 1358 | 1652 | no |
| monstats / monstats2 / superuniques | 752 / 624 / 67 | same | 1323 / 758 / 89 | no |
| levels / lvlmaze / lvlprest / lvlwarp | 138 / 82 / 1093 / 89 | 139 / 82 / 1093 / 89 | 171 / 100 / 1096 / 91 | no |
| objects / objtype / objgroup | 588 / 574 / 133 | 588 / 588 / 133 | 632 / 632 / 135 | no |
| hireling / monequip / monprop / pettype | 135 / 45 / 13 / 22 | same | 171 / 53 / 66 / 31 | no |
| overlay / monseq / monpreset / monsounds | 334 / 1030 / 230 / 150 | 335 / 1030 / 230 / 150 | 368 / 1269 / 330 / 158 | no |
| armor / weapons / inventory | 218 / 307 / 31 | same | 220 / 320 / 31 | yes |

The other shipped tables have the same row count in both banks (charstats, experience, gems, sets, monlvl and the
rest), but their content is D2R's, not PD2's. `itemstatcost` also differs in 148 Save Bits / Save Add cells for ids
0-367, so copying it to `base/` changes how existing LoD items decode.

## Plugin gaps seen in a LoD game

| Gap | Evidence | Issue |
|---|---|---|
| Base bank is vanilla D2R: no PD2 content | `cheat spawn t11` made nothing; the table above | #1853 (fixed: every bank gets our tables) |
| Stash tabs assume 5 stock shared pages; Classic/LoD have 3 (`Page 1 / 18` with `add_shared_tabs = 15`), so pages 18 and 19 don't exist | Bossing tab shown, `shared page 20 on show (all 79 arrows sent)` | #1854 |
| Other PD2-only stat and state writes on Classic/LoD units | `BC_ASSERT: nShift >= 0 && nShift < DataTablesGetNumStates(UnitGetGameVersion(hUnit))` | #1855 (gone with #1853: every bank holds the ids) |
| core-events looks up PD2 treasure classes | `key drops: no TC "lucdN" (100 so far)` | #1856 |

Not seen as a problem: inventory, personal stash, shared stash, gold, level, quests and identified rare items all
saved and reloaded on a LoD character once the op bases were gated.
