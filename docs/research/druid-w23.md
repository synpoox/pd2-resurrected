# Gust and Hunger (W23): srvdo 156 and 165

Answers work item W23 (#98). PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll` /
`D2Common.dll` of the PD2 install in Ghidra, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`), RVAs.

PD2's table fill (`0x1036ba20`) writes srvdo 156 = `0x103b1980` (`+0x270`) and srvdo 165 = `0x103b27c0` (`+0x294`).
D2R fills both slots (the free range starts at 172) and its srvdo table ends at 190, so both join the srvdo 190
router (`tools/pd2_ids.py` FUNCS), by merged skill id: Gust 442 (PD2 370), Hunger 242 (D2R's own row).

ProjectDiablo.dll reaches 1.13c code through lazy thunks: `0x103a5f20(dll index, entry)` with the entry from a
per-version table (index 0 is 1.13c; a positive value is an offset in the DLL, a negative one an ordinal; DLL indexes
in [nova-orb-hits](nova-orb-hits.md)).

## srvdo 156, Gust (`0x103b1980`)

Row: srvmissilea `gust`, calc1 `ln12` (chill length, Param1 12, Param2 0), calc3 `par3` (radius 4), ResultFlags 5,
HitFlags 1, HitClass 2, EType cold, no damage columns.

1. The teleport core `0x1036cd20(game, unit, 1)` (Teleport's, [sorceress-w26](sorceress-w26.md)); return 0 if it fails.
2. D2Game `0xa28f0(game, srvmissilea, unit, skill, level, 0, 0, 0, 0, 1)`: the missile from the unit thrown at the
   skill's target, the call 1.13c's do dispatcher makes for `srvmissile`. D2R's is `0x4333f0` (`ThrowSkillMissile`,
   one more argument, the unit's state key), as Double Throw calls it. With its last argument 1 the call makes a
   missile for a player only when `0xa1790` passes (a throwing weapon's quantity), so a Druid without one throws
   nothing; D2R's does the same.
3. A zeroed 0x70-byte damage block: hit flags = record `+0x130` (HitFlags), result flags = `+0x12e` (ResultFlags),
   `+0x30` cold length = calc1, `+0x64` hit class = `+0x134` (HitClass).
4. D2Game's area search (`0x103a1450`) around the unit with radius calc3, unit mask `0x8583` and callback D2Game
   `0xa1210`, which copies the block and calls `0xa10b0`: avoid rolls, the hit's effects, events and damage. Returns 1.

D2R: `0xa10b0` is D2R's area apply `0x436e90` step for step, and `0xa1210` is the callback `0x43a740` that D2R's
`AreaKnockback` (`0x437820`, Leap's landing) runs with a copy of its block and PD2's unit mask. D2R's block has the
cold length at `+0x118` and the hit class at `+0x144` (ElementalDamage `0x437d00`, D2R's Hunger `0x576630`); the
record's ResultFlags, HitFlags and HitClass are `+0x186`, `+0x188`, `+0x18c` (1.13c + 0x58, as calc1).

core-rules: `TeleportCore`, then `ThrowSkillMissile`, then `AreaKnockback` with that block. The teleport core's
deviations are Teleport's (on the unported list).

## srvdo 165, Hunger (`0x103b27c0`)

Row: aurastate `hunger`, auralencalc `475+(lvl*25)`, aurastat1-4 `lifedrainmindam` (`par3 + blvl / 4`),
`velocitypercent` (`par4 + lvl`), `item_openwounds` (`dm12 + 5`), `deep_wounds` (`edmx`); TargetCorpse, bear only.

1. The record (`0x1037c110`), the path's target unit (D2Common 10392); none, or the unit itself, returns 0.
2. The target gets state 104 `corpse_nodraw` (D2Common 10945, the state toggle; it marks the unit for an update,
   D2Common 10158, and PD2 calls 10158 once more).
3. For aurastat1-6 (record `+0x54`, calcs `+0x68`): a stat out of range (below 0 or past itemstatcost) is skipped,
   except aurastat1, which gives no list. core-rules checks below 0 only (an empty column); the merged rows name no
   stat past the table.
4. PD2's state helper `0x1036c920` with aurastate (`+0x80`) for auralencalc (`+0x60`) frames and those stats: the
   timed-state helper `0x1036d7d0` (the recast rule of [w12-blood-warp](w12-blood-warp.md)), each stat set when the
   list doesn't have it yet, then the state, and packet `0x83` with the timer for a player. The resist halving and the
   special states (16, 87, 188) of [necromancer-w11](necromancer-w11.md) don't touch Hunger's stats or state.
5. Unit `+0xc4 |= 0x40` (D2R's `setUnitSkillFlag(unit, 1)`, `0x34e790`) on both paths past the corpse; returns 1.

D2R's own Hunger (srvdo 122, `0x576630`) takes the corpse from the skill's target (`0x48fe20`); core-rules does the
same, then the state through `TimedStatList`. Its recast rule keeps a running list's values where PD2 adds missing
stats; Hunger always sets the same stats, so the result is the same. A recast that extends the list doesn't send the
client its timer (packet `0x83`), as for Teleport and Blood Warp: an approximated row on the unported list.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w23`: `gust-cast` (radius calc3 4, cold length calc1 12, the row's
flags; the missile thrown or not, step 2) and `hunger-hit` (state hunger for 475 + lvl * 25 frames, lifedrainmindam
3 + lvl / 4, velocitypercent 19 + lvl).

Run on Slot8 (2026-11-04), a level 95 Druid with Gust 5, Werebear 1 and Hunger 5: all four checks pass. Gust
teleported and logged radius 4, cold length 12, flags 1/5/2, missile 763 not thrown (no throwing weapon). Hunger in
bear form ate a zombie's corpse: state 246 for 600 frames, stats 60=4, 67=24, 135=37, 554=80, the buff icon shown.
