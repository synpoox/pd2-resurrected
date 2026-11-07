# Monster kits ([W36](https://github.com/synpoox/pd2-resurrected/issues/244))

Answers work item [W36](https://github.com/synpoox/pd2-resurrected/issues/244) ([#244](https://github.com/synpoox/pd2-resurrected/issues/244)). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and PD2's 1.13c `D2Game.dll` /
`D2Client.dll` (RVAs named so) in the PD2 Ghidra project; D2R side from the 93847 dump (RVAs). Read statically; D2R's
table slots read from the runtime dump the way `skill-function-tables/probe_tables.py` reads them.

PD2's table fill `0x1036ba20` writes srvdo 173 = `0x103b3520`, srvdo 181 = `0x103b4cf0` (D2Game do table `[0x105de8fc]`),
cltdo 109 = `0x103abba0` (`[0x105de71c]`) and srvst 67 = `0x103b6520` (start table `[0x105de8ec]`). Its server missile
install `0x103a9170` writes srvhit 6 = `0x103a7750`. Its client missile installs (`0x103a6330`, `0x103a6c50`) leave cdo 12
and chit 11 alone. 1.13c's own tables, through PD2's per-version offsets (index 0 is 1.13c): D2Client missile cltdo at
RVA `0xdd8a0` (version array `0x104a910c`), clthit at `0xdda10` (`0x104a9934`); D2Game srvst at `0x107338`
(`0x104a9c4c`).

## srvdo 173, cltdo 109 (PoisonEggSpawn)

`0x103b3520(game, unit, skill, level)` calls the do table's slot 22 with the same arguments, then slot 84 with skill
`0x9f` (159, MaggotEgg) and the level, and returns slot 84's result. The client `0x103abba0(unit, skill, level)` does
the same with cltdo 25 and 46. D2R's slots hold the same: srvdo 22 `0x553cf0` and cltdo 25 `0x220140` are Nova's
(Nova, Frost Nova, Poison Nova), srvdo 84 `0x57b880` and cltdo 46 `0x223cb0` MaggotEgg's. PD2's row: srvmissilea and
cltmissilea `poisonnova`, the pois EType damage. Only torajanBossPoisonEgg (Sk1lvl 5) casts it.

**Port.** core-rules' srvdo 173 (free in D2R) and cltdo 126 (D2R fills 109) call D2R's slots the same way. MaggotEgg is
159 in the mod too.

## srvdo 181 (TorajanBossNest)

`0x103b4cf0(game, unit, skill, level)`:

1. The skill record (`0x1037c110`) and the unit's current skill (`0x1030dd00`); none: 0.
2. `0x1037b990(game, unit, 1)`, D2R's `RemoveUnitEvent` (`0x48b890`) as D2R's Nest calls it.
3. A monster with 1.13c AI data (`unit+0x14 -> +0x28`) only. Its counter (`+0x1c` there) below Param1 (`+0x148`),
   else 0.
4. `0x103104e0(unit, 0)` (D2R `0x48e820`), then the current skill's spawn (D2Common getters `0x10309230` monster,
   `0x10309260` x, `0x10309290` y, `0x103092c0` mode, which srvst 49, Nest's start, set from the monstats spawn
   columns), the room at x/y (`0x103a16c0`), the spot cleared (`0x1030bed0(room, x, y, 1, 0x100)`), and **one**
   spawn (`0x103130a0`, 1.13c's spawn monster, with 1, 0).
5. On a spawn: counter + 1, unit flags (1.13c `+0xc4`) `|= 0x4020000`, and the skill's `+0xe6` overlay when not
   negative. Returns 1.

D2R's Nest (srvdo 91, `0x57c590`) does steps 2 and 4 with the same calls (`0x48e820`, `RoomAtXY`, `0x3636d0`,
`SpawnMonster` `0x492cf0`), but spawns the start's count (`+0x34`), has no cap, sets the same two flags through
`0x34e8d0` (`+0x124 |= 0x4000000`) and `0x34f010` (`|= 0x20000`), puts the skill's `+0x13c` overlay on, and links the
spawn to the nest's minion list and callback. D2R's unit flags at `+0x124` are 1.13c's `+0xc4` bit for bit: 0x20000 no
treasure class, 0x4000000 no experience.

**Port.** The router (srvdo 190, skill 631) runs PD2's steps with D2R's calls. The counter is kept by core-rules, by
game and unit id: D2R's AI data was not mapped (approximated row, on the deviation list; the count is the same).

## srvst 67 (Corpse/Vine Cycler start)

1.13c's srvst 67 is empty: PD2 wrote it. Vanilla's CorpseCycler and VineCycler used srvst 63 (1.13c `0xd170`, D2R
`0x587b50`): with a monster caster and a target corpse not in state 118, it sets 118 on the corpse, refreshes it
(Ordinal 10158, D2R `0x38ebf0`), creates srvmissilea (`recycler delay`) whose hit heals, and sends packet 0xa3 (D2R
`0x5396d0`, `SendChargePacket`).

PD2's `0x103b6520(game, unit, skill, level)`:

1. A monster, skill id in range and not 0.
2. The target (`0x1036d440`: the refreshed path target, not the caster), not in state 118, and not owned by a player
   (1.13c owner type `+0x94` / id `+0x98`: the `OwnedByPlayer` test).
3. State 118 on, the refresh, then aurarangecalc (`+0x64`), calc1-3.
4. calc3 must be 6 (life) or 8 (mana), else 0 (the corpse stays taken). heal = calc1 + rand(calc2 - calc1).
5. The area search `0x103a1450` at the caster, radius aurarangecalc, mask aurafilter (`+0x50`), callback `0x1036d5a0`:
   stat = min(stat + heal * 256, max) with max 7 (maxhp) or 9 (maxmana). No alignment test beyond the mask.
6. Packet 0xa3 (`0x103a1c40(corpse, unit, skill, 1, 0, 0, 0)`), as vanilla's 63. Returns 1.

PD2's rows: aurarangecalc 40, aurafilter 73731, calc3 6 (CorpseCycler, Cycle of Life's vine) and 8 (VineCycler,
Vines'). D2R's 67 is Frenzy's start, so ours takes 83 (71-90 free; PD2 uses 72-79).

**Port.** srvst 83 does PD2's steps with D2R's calls (UnitHasState, SetUnitState, `0x38ebf0`, AreaUnits, UnitStat,
SetUnitStat, the caster's seed, SendChargePacket). The corpse is D2R's skill target (`0x48fe20`, what D2R's 63 reads),
where PD2 refreshes and reads the path target, as for Holy Light (approximated row).

## shit 6 (summon-on-hit missiles)

PD2's `0x103a7750(game, missile)`: with the missile's skill in range and its row: monstats `sHitPar1` (`+0x4c`), unless
-1, spawned at the missile's x/y in its room (`0x103a1420`) in mode `sHitPar2` (`+0x50`, 1 when 16 or more), then
`sHitPar3` (`+0x54`, unless negative) or-ed into its unit flags. Returns 1.

D2R's srvhit 6 (`0x45ce90`) spawns the same (`0x50a050(game, room, monster, x, y, mode, 0)`, which returns the monster),
but never reads `sHitPar3`. PD2's five rows set 0x4020000 (no treasure class, no experience; sharptooth's adds 0x1e), so
D2R's 6 would let these spawns drop loot and give experience. Ours takes slot 67 (61-70 free; PD2 uses 61-65).

`sHitPar1` is a monstats id, and the mod's monstats has D2R's rows first (17 more than 1.13c's) and PD2's after them, so
PD2's 999 (leoricGuards) is the mod's 1016. Before [W36](https://github.com/synpoox/pd2-resurrected/issues/244) the merge kept 999, which is a different monster in the mod.
`pd2_ids.HIT_PARAMS` now names `sHitPar1` of srvhit 67 a monstats id, and `pd2_ids.monstats_ids` maps PD2's ids the way
`merge/tables.json` merges monstats (by name, D2R's rows keep their ids).

## chit 11 (desertfireball), cdo 12 (siege beast stomp)

1.13c's clthit 11 is empty, and so is D2R's; D2R's dispatcher (`0x1b8e60`) skips an empty slot. Data.

1.13c's cdo 12 (D2Client `0xb2d00`): with CltParam1 and CltParam2 at least 1, and unless D2Common #10985 says no, a
screen shake of strength CltParam1 with (CltParam2 / 3) * 1000 twice; then the default missile do (cdo 1). D2R's 12
(`0x1b6b30`) is the same with a three-part shake, (CltParam2 / 3) * 1000, (CltParam2 - 2 * (CltParam2 / 3)) * 1000,
(CltParam2 / 3) * 1000, then cdo 1 (`0x1b5380`). PD2's row has CltParam2 1, so 1.13c passes 0, 0 and D2R a middle part
of 1000: the shake's length may differ (approximated row; the siege beast is a map monster, not seen in game).
