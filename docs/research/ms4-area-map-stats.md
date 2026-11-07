# [MS4](https://github.com/synpoox/pd2-resurrected/issues/219): area map stats in D2R

Part of [MS4](https://github.com/synpoox/pd2-resurrected/issues/219) ([#219](https://github.com/synpoox/pd2-resurrected/issues/219)); the drop chances are
[#286](https://github.com/synpoox/pd2-resurrected/issues/286) and the extra boss [#289](https://github.com/synpoox/pd2-resurrected/issues/289). PD2's side is
[pd2-map-system-dll.md](pd2-map-system-dll.md), section 2 (the area slots). D2R addresses are RVAs in the 93847 dump,
read in Ghidra (`D2R.gpr`). PD2's are VAs in `ProjectDiablo.dll` (`PD2.gpr`).

## Routing

The 2000-2999 rows of [MS3](https://github.com/synpoox/pd2-resurrected/issues/218)'s `Routes` table ([ms3-map-stats.md](ms3-map-stats.md)) are area slots (`Divide - 2000`), in
the port's stat ids. A scratch script checked all 28 against PD2's `itemstatcost.txt`. On opening, `RouteMapStats`
keeps each slot's first value in the per-game counters (`AreaSlot(game, slot)`); PD2's getter `0x1038a9e0` also returns
the first entry.

`map_glob_add_mon_*` values are monstats ids. `tools/pd2_ids.py` now remaps them like the other id columns (PD2 690 is
port 690, 1169 is 1186, 1111 is 1128...), so the merged map affixes name the right monster.

## Who reads each slot in PD2

`0x1038a9e0` has five callers: `0x102fc400` (area level), three in the drop hook `0x10372180` (ethereal, corrupted,
sockets) and `0x1038bfc0` (extra boss). The region rewrite `0x1038b4d0` reads the slots itself. Nothing reads slots 7-9
and 15 (the boss drop stats), and slot 10 is only matched in `0x1038aa00` for Horazon's entry ([MS9](https://github.com/synpoox/pd2-resurrected/issues/223)). So leaving those
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

Only the entry level is rewritten; PD2 also rewrites a multi-level map's other levels, which come with [MS7](https://github.com/synpoox/pd2-resurrected/issues/221).

## Game check

Slot 7, Hell, the level 95 Paladin, rare maps from `cheat spawn t11,typ r,inv 0`:

- Density: 2062 -> 3422 at +66, 3464 at +68, 2639 at +28, 3361 at +63, each the exact truncation.
- Rarity: +48 took the boss packs from 40-60 to 59-88, +39 to 55-83.
- Add monster: Ghastly (1128) and Rampant (1186) each took the groups from 11 to 12.
- The area level hook installed; with no area level stat the monster level stayed 87.

Not seen: a monster from the added group. About 60 packs in Ruins of Viz-Jun with 1186 added (1 in 12 by weight) showed
none, and the spawner (`0x50acf0`, `0x50b7e0`, `0x509cc0`) has no check that would turn it away; not explained. Also not
run: the area level (it rolls only through corruption), fortify (`fort` spawns since [#310](https://github.com/synpoox/pd2-resurrected/issues/310), but no fortified map was opened yet), drop bonus and
treacherous.

**The added group, read again.** The affix values are PD2 `hcIdx` ids (PD2's `monstats.txt` has an `Expansion` row,
so row and id part after 469): Ghastly adds `wraithMapMod` (port 1128), Rampant `baalminion3MapMod` (port 1186), both
enabled and `isSpawn` with `MinGrp`/`MaxGrp` set and no `spawn` swap. In D2R's spawn path nothing turns the group away:

- The region builder `0x509940` adds a group only when its monstats flag byte `+0x3c` passes, and raises `+0x10`,
  `+0x11` and `+0x12` together, so `+0x12` equals the group count; the port's group lands at index `+0x10`.
- `0x419d20` picks the pack kind: a unique pack while `+0x2c8` (packs so far) is below `+0x2bc`/`+0x2bd`, else a
  champion (rand % 100 > 35) or a normal pack. Both kinds take their monster from `0x419e80`.
- `0x419e80` rolls `rand % (+0x11)` over every group's `+2` rarity. It swaps the pick for its monstats `spawn`
  (`+0x54`) only when that is set (the `isSpawn` flag, `+0x3e`), and a debug override (`0x14238d5b4`) can force one id.
- `0x50acf0` then needs only `MinGrp`/`MaxGrp` (`+0x65`/`+0x66`) and `sparsePopulate` (`+0x67`, blank here) to spawn.
- Both getters of the room's region (`0x2efc10`, `0x2efd00`) read the same level id, so the spawner uses the region
  `ApplyArea` rewrote.

So in a Sewers map (8 groups, rarity sum 9) one pack in 10 should be wraiths of the added group. `ApplyArea` now logs
`map area: level L adds monster M as group G`, and the `added-group-spawn` check in `tests/game/core_maps_checks.py`
looks for that monster among the spawned kinds.

## [MS4b](https://github.com/synpoox/pd2-resurrected/issues/286): the drop chances ([#286](https://github.com/synpoox/pd2-resurrected/issues/286))

PD2's drop hook `0x10372180` replaces 1.13c's TC drop (D2R's `0x444680`, which keeps PD2's monster 0x187 check). For a
unit in a map area it reads three slots, in this order (`edx` before each `0x1038a9e0` call):

| Slot | PD2 | D2R |
|---|---|---|
| 13 ethereal | `rand() % 100 < v` sets flag 0x4 in the drop request's flags (1.13c `+0x80`) before the item is made | the same flag at `+0xc0`: D2R's quality roll `0x442d60` then makes an ethereal-capable item ethereal, as for its own 5% roll |
| 6 corrupted | `rand() % 100 < v`, then `0x103649d0(item, 0)` | `CorruptDrop`, below |
| 11 socketed | the item is socketable (D2Common `#10360`), `rand() % 100 < v`, then `0x103816c0` | D2R's socketable test `0x36b410`, then `0x36e880(item, n)` (`#10599`): n clamped to the type's max |

The port hooks `0x43cd50(game, drop, 0)`, which makes the item from the request, and acts only when it returns to the
TC drop (`0x44486b`), so vendors, gambling, the cube and gold pass through.

**Sockets** (`0x103816c0`): not item types 38 `tpot`, 48 `thro` or 56 `misl` (`0x10364cb0` reads the item's own type);
1-2 sockets, 2-4 when the items row is `2handed` (1.13c `+0x11c`, D2R `+0x12b`).

**Corruption** (`0x103649d0`, second argument 0): nothing for an item that has `corrupted`. It walks cubemain: enabled
rows of op 16 or 18 on `corruptor` with a value, whose second input is the Worldstone Shard (or none) and whose first
matches the item by PD2's own test `0x103647d0` (class, type, quality, socketed, ethereal; no tier or runeword flags).
The group's "destroyed (rare)" row is the roll row: rare `usetype` output (`0xff`; `0xfe` is `useitem`, read from PD2's
`cubemain.bin`) and no third mod. It sets the roll to `value + rand() % (1000 - value)`, and the first row after it whose
value beats the roll puts its mods on the item through 1.13c `#10914` (mod type 7), D2R's `0x3d3420`. So a drop is never
destroyed. D2R's record is packed: inputs at `+0x14`, 10 bytes each; output A at `+0x5e`, quality `+0x64`, type
`+0x67`, mods from `+0x77` (`0x529d50`, `0x5269c0`; the field table `0x3e3ff0` gives offsets 0xe lower).

PD2 resolves its 1.13c targets through version tables (`0x103a5f20(dll, table[version])`, negative entries are
ordinals); column 0 is 1.13c.

**Game check** (Hell, level 95): the log read `stats 4/4 installed: ... drops`. In Na-Krul's Abyss (`cheat uniques 519`,
slot 13 at 100), with `cheat mspawn` monsters killed, it logged `map drops: item 178 ethereal 1`. Warlord of Blood
(`cheat uniques 470`, corrupted) and Ureh City (`cheat uniques 509`, socketed) passed in the user's own run.

## Slot 5, the extra boss ([MS4c](https://github.com/synpoox/pd2-resurrected/issues/289))

`0x1038bfc0` wraps 1.13c D2Game's preset spawn (`0x6fd013b0`, game, room, preset). After the normal spawn, in a map area
with slot 5 > 0, it spawns a monster preset (type 1) a second time when its id is in a PD2 set (`0x10371290`) or is
1044.

**The set.** `0x10371290` tests the id (in `ecx`) against six std containers, `0x105de938`, `0x105de7d4`, `0x105de9c8`,
`0x105de9d8`, `0x105de9b0` and `0x105de818`, each filled at start from an int array by `0x10190e30`, `0x10190f10`,
`0x10190ff0`, `0x101910b0`, `0x10191110` and `0x101911f0` (constants at `0x104506f0`-`0x10450870` and immediates). In
PD2 monstats ids, then the port's (`tools/pd2_ids.py` `monstats_ids`):

| Set | PD2 ids | Monsters |
|---|---|---|
| `0x105de938` | 1026, 800, 1025, 894, 923, 924, 926, 927, 1242, 997 | lernaeanhydra2, ArcaneBoss, AshenBoss, archerBoss, the four NihlMinions, PoisonedWellBoss, doomknight3LibraryBoss |
| `0x105de7d4` | 746, 996, 750, 915, 1166, 1164, 1175, 1174, 1240, 1243 | megademonboss, westmarchMapBoss, unravelerboss, spiderboss, ImperialPalaceBoss and its minion, CityofUrehMiniBoss, CityofUrehBoss, DarkTempleBoss, DescentBoss |
| `0x105de9c8` | 247, 248, 249, 824, 970-973 | frogdemon1-3, frogdemonThrone, voidKnightCorridor, overseerRat, unraveler3Rat, skeleton4Rat |
| `0x105de9d8` | 747, 1000, 1103 | fingermageboss, siegebeastMapBoss, KanemithBoss |
| `0x105de9b0` | 743, 1104, 1105 | willowispboss, RadamentBoss, SharpToothBoss |
| `0x105de818` | 968, 969, 1106, 1107, 884, 963, 882, 1149, 1183-1186 | the four voidKnights, SewerBoss, CanyonBoss, ThroneBoss, TortureHallsBoss, the four Invaders |
| (immediate) | 1044 | lernaeanhydra1 |

Port ids: 247-249, 760, 763, 764, 767, 817, 841, 899, 901, 911, 932, 940, 941, 943, 944, 980, 985-990, 1013, 1014, 1017,
1042, 1043, 1061, 1120-1124, 1166, 1181, 1183, 1191, 1192, 1200-1203, 1257, 1259, 1260 (checked against the merged
`monstats.txt` by name).

**D2R's preset spawn** is `0x4926b0(game, room, preset)` (`D2Game\src\Unit\SUnit.cpp`), the only callee of the room's
preset loop `0x48f190` (1.13c `0x6fd01430`: the same 0x85-0x88 uber level cases, preset ids 0x18d, 0x192, 0x10c, 0x1a,
0x10d). It calls `0x48dc50` (1.13c `0x6fd012d0`), which sends type 1 to `0x50a050` (1.13c `0x6fc67cf0`; "Preset monster
could not be added"). The preset record moved: id `+4`, x `+8`, unit type `+0x20`, y `+0x24` (1.13c `+0x14`, `+0x18`).

`OnPresetSpawn` (`plugin/core-maps/map_stats.cpp`) hooks `0x4926b0`: after the spawn, a type 1 preset in a map area
(levels 138-201, from the room) whose id is in the set gets a second spawn when slot 5 > 0. No PluginSDK service spawns
presets. PD2's wrapper also rewrites or adds presets in about 20 of its levels (PD2 ids 152-199: the dungeons, the unique maps,
the Void); that is the dungeons' and unique maps' work ([MS7](https://github.com/synpoox/pd2-resurrected/issues/221)), not slot 5.

**Game check** (Slot5, Hell, the level 95 Amazon): the log read `stats 5/5 installed: ... extra boss`. A Sewers of
Harrogath map with the Duplicated prefix (`cheat spawn t25,typ m,pre 843,inv 0`) opened with `stat 551 ... -> area slot
as 5` (value 1). Not seen yet: the Sewer Boss twice. `cheat goto 142,<x>,<y>` did not move the character, so the boss
room was not reached in the time box.
