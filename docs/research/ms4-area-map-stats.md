# MS4: area map stats in D2R

Part of MS4 ([#219](https://github.com/synpoox/pd2-resurrected/issues/219)); the extra boss and the drop chances are
[#286](https://github.com/synpoox/pd2-resurrected/issues/286). PD2's side is
[pd2-map-system-dll.md](pd2-map-system-dll.md), section 2 (the area slots). D2R addresses are RVAs in the 93847 dump,
read in Ghidra (`D2R.gpr`). PD2's are VAs in `ProjectDiablo.dll` (`PD2.gpr`).

## Routing

The 2000-2999 rows of MS3's `Routes` table ([ms3-map-stats.md](ms3-map-stats.md)) are area slots (`Divide - 2000`), in
the port's stat ids. A scratch script checked all 28 against PD2's `itemstatcost.txt`. On opening, `RouteMapStats`
keeps each slot's first value in the per-game counters (`AreaSlot(game, slot)`); PD2's getter `0x1038a9e0` also returns
the first entry.

`map_glob_add_mon_*` values are monstats ids. `tools/pd2_ids.py` now remaps them like the other id columns (PD2 690 is
port 690, 1169 is 1186, 1111 is 1128...), so the merged map affixes name the right monster.

## Who reads each slot in PD2

`0x1038a9e0` has five callers: `0x102fc400` (area level), three in the drop hook `0x10372180` (ethereal, corrupted,
sockets) and `0x1038bfc0` (extra boss). The region rewrite `0x1038b4d0` reads the slots itself. Nothing reads slots 7-9
and 15 (the boss drop stats), and slot 10 is only matched in `0x1038aa00` for Horazon's entry (MS9). So leaving those
unported keeps parity.

## D2R's monster region

- `*(game+0x1d0)` is an array of region pointers by level id (`0x508740` reads it). `0x5098b0` builds every region once
  per game (`0x508750`), from the levels row: groups chosen by `0x509940`, each group's component picks by `0x4195e0`.
- The layout is 1.13c's: `+0x10` group count, `+0x11` rarity sum, `+0x12` spawn count, `+0x14` groups of 0x34 bytes
  (`{monstats id u16, rarity u8, picks u8, ..., components at +0x10}`), `+0x2b8` `MonDen` (u32), `+0x2bc`/`+0x2bd`
  `MonUMin`/`MonUMax` (u8). From `+0x2e8` on are D2R's terror zone fields.
- `0x50acf0` spawns a room's packs: it caps `MonDen` at 10000 and spawns when `rand % 100000 <= MonDen`. The pack
  picker `0x419e80` weights the groups by their `+2` byte against the `+0x11` sum.
- D2R never reads the region's monster level (`+0x2dc`, `+0x2e0`), which PD2 rewrites for the area level. Monster, unique
  and item code all get it from `0x508f80(game, level)` (levels `MonLvl`/`MonLvlEx` through `0x32c700`, plus the terror
  zone bump `0x35b340`).

## What the port does

`ApplyArea` (`plugin/core-maps/map_stats.cpp`) rewrites the opened map's region the way `0x1038b4d0` does:

| Slot | Effect |
|---|---|
| 0 density | `MonDen` x (1 + v/100) |
| 1 area level | a hook on `0x508f80` adds v in map areas (138-201) |
| 2 rarity | boss pack min and max x (1 + v/100), truncated to a byte |
| 3 add monster | group `{id, rarity 1, picks 1}`, count and rarity sum + 1, 13 groups at most; an id that is not a monstats row is skipped |
| 4 fortify | density and pack counts x v/100; the monster list gets +100 `item_maxhp_percent`, +40 `damagepercent`, +40 to 329-332, +20 `passive_mag_mastery` (357) and `map_glob_skirmish_mode` (546) = v |
| 12 drop bonus | the monster list gets `map_glob_dropbonus` (399) = v |
| 14 treacherous/phasing | the monster list gets +100 `item_maxhp_percent` and `treacherous` (402) = v |

Only the entry level is rewritten; PD2 also rewrites a multi-level map's other levels, which come with MS7.

## Game check

Slot 7, Hell, the level 95 Paladin, rare maps from `cheat spawn t11,typ r,inv 0`:

- Density: 2062 -> 3422 at +66, 3464 at +68, 2639 at +28, 3361 at +63, each the exact truncation.
- Rarity: +48 took the boss packs from 40-60 to 59-88, +39 to 55-83.
- Add monster: Ghastly (1128) and Rampant (1186) each took the groups from 11 to 12.
- The area level hook installed; with no area level stat the monster level stayed 87.

Not seen: a monster from the added group. About 60 packs in Ruins of Viz-Jun with 1186 added (1 in 12 by weight) showed
none, and the spawner (`0x50acf0`, `0x50b7e0`, `0x509cc0`) has no check that would turn it away; not explained. Also not
run: the area level (it rolls only through corruption), fortify (`fort` does not spawn, #260), drop bonus and
treacherous.
