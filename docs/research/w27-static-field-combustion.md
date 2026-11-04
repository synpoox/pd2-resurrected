# Sorceress: Static Field, Combustion and the boss novas (W27)

Answers work item W27 (#102). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) in the Ghidra project `PD2.gpr`,
D2R side from the 93847 dump. Addresses are D2R RVAs unless they start with `0x10`. The install routine `0x1036ba20`
writes srvdo 159 = `0x103b1d00`, srvdo 160 = `0x103b1dc0` and cltdo 100 = `0x103ab580`. D2R fills all three slots
(srvdo 159 Consume, 160 Blood Boil, cltdo 100 Flame Wave), and the dispatcher refuses srvdo values over 190, so the
merge sends srvdo 159 and 160 to the router (srvdo 190, by skill id) and cltdo 100 to the free slot 120.

## srvdo 159 and cltdo 100: Combustion and the boss novas

Rows: Combustion, FingermageBossNova, RathmaTeethNova, RathmaOuterNova, RathmaPacman, LeoricSummonNova,
ZharMiniBossCombustion (merged 448, 472, 561, 567, 571, 595, 617).

1. Set the unit's skill flag (`+0xc4 |= 0x40`; D2R's `setUnitSkillFlag(unit, 1)`).
2. Return 0 unless srvmissilea (the client: cltmissilea) is a missiles.txt row.
3. PD2's ring (`0x1036d250` server, `0x1036d370` client, the one Teleport and Poison Dagger use) of calc2 missiles at
   the caster's position, flags 3: one missile at every (64 / calc2)-th of the 64 directions. calc2 is Param1 in
   every row (Combustion 32).
4. Return 1.

The ring's radius is the missile row's own travel (Vel, Range): the merged `combustion` row has PD2's values.
D2R's Nova (srvdo 22, the stand-in until now) counts its missiles differently (`0x3c4860` plus calc2).

## srvdo 160: Static Field

Rows: Static Field, Merc Static Field, Static Field Proc (merged 42, 528, 679). PD2's function has D2R's srvdo 20
(`0x5546b0`) shape: the same per-unit argument block {calc2, calc1, difficultylevels StaticFieldMin, an elemental
length, EType}, the same area search over aurarangecalc with aurafilter as the unit mask. It differs in the per-unit
damage and adds a second pass.

### Damage pass (`0x10302830`, per unit)

The area context is {game, caster, ?, &block}. Skips monsters of class 789, 933-936, 1112 (PD2's monhydra3,
dopplezonnew, rathmaBone, rathmaPoison, rathmaBoneClone, wraithMapMod; merged 806, 950-953, 1129), a unit with
life (stat 6, in 1/256) under 256, and a unit whose life percent (D2Common #11027: `(life >> 8) * 100 / (max >> 8)`)
is not over StaticFieldMin (PD2: 55, 70, 85 by difficulty). `MulDiv` is `0x1037c4c0`: a * b / max(c, 1) in doubles,
truncated, `INT32_MAX` past it.

    cap      = MulDiv(maxlife, (unsigned)(100 - StaticFieldMin), 100)
    damage   = MulDiv(life, calc1, 100)
    resisted = MulDiv(damage, 100 - lightresist, 100)
    limit    = min(MulDiv(cap, 100 - lightresist, 100), cap)
    out      = max(damage, calc2)          (unsigned compare)
    if resisted > limit: out = cap

The hit is lightning (`out`) with hit class 0x4d, applied with area events. D2R's own callback (`0x5556a0`) differs:
it stops at `life <= maxlife * StaticFieldMin / 100` instead of the percent test, takes at most life - 1, and divides
the damage by (100 - resist) / 100 when the resist is negative, so a negative resist never raises it. The port builds
the hit the way D2R's callback does (`AddElementDamage 0x437980` with etype 2, hit class `|= 0xd`, `+0x148 = 1`,
`HitEvents` with area 1, `DealDamage`) with PD2's amount.

### Debuff pass (`0x1036c920`, per unit, area flag 1)

The block is {skill, level, auralencalc, auratargetstate, aurastat1-6, their aurastatcalc values}; a stat that is
not an itemstatcost row is -1, except that an invalid aurastat1 makes srvdo 160 return 0 (after the damage). Per unit:
each value goes through `0x1036df10` (a resist reduction, stats 36/37/39/41/43/45, on a unit that is not a player,
not a hireling (D2Common #11104) and already resists 100 or more, is halved), then the timed-state helper `0x1036d7d0`
puts the list on the unit with the caster as owner; stats are set only on a new list. AreaUnits' flag 1 (D2R reads
it the same way) skips monsters by a monstats flag.

staticfield_debuff (merged 240) is not a curse, so the helper's curse rules do not apply. PD2's aurastatcalc1
`-min((par5+(lvl<22)?lvl:(22+((lvl-22)/2))), 100)` parses as `(par5 + (lvl<22)) ? lvl : ...`, so it is
`-min(lvl, 100)`; auralencalc is `125 + 5 * Lightning Mastery`.

## core-rules

`plugin/core-rules/skill_functions.cpp`: `SrvDoNova` and `CltDoNova` (cltdo 120) share `Ring` with Teleport
(`ServerRing`); `SrvDoStaticField` runs both passes through `AreaUnits`. `StaticFieldDamage` and `ImmuneHalved` have
compile-time checks; `TimedStatList` takes the list's owner.

Deviation (approximated, on the unported list): the state helper's monster refusals (monstats flags, `0x1036d7d0`)
are D2R's NewStateList's; a player who gets the debuff is not sent the `0x83` timer packet; an aurastat past
itemstatcost's end is not refused. D2R's online cast limit (`0x432c40`) before its Static Field is left out, as PD2 has none.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w27`: `static-field-cast` (radius, calcs, StaticFieldMin, the first
unit's damage against PD2's formula, the debuff's state, stat, value and length), `combustion-cast` (32 of 32 missiles
on server and client, the missile's Vel and Range as PD2's), and the W21 `channel-cast` / `channel-client` for
Inferno. Not run in game yet.
