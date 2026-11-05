# Holy Light and the Fist of the Heavens hit ([W17](https://github.com/synpoox/pd2-resurrected/issues/92))

Answers work item [W17](https://github.com/synpoox/pd2-resurrected/issues/92) ([#92](https://github.com/synpoox/pd2-resurrected/issues/92)). PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll`,
`D2Client.dll`, `D2Common.dll` of the PD2 install in Ghidra; D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`),
RVAs.

PD2 reaches vanilla code through version-indexed entries (column 0 is 1.13c's offset, negative for a D2Common
ordinal). The ones read here:

| PD2 call | Entry | 1.13c | What it is | D2R |
|---|---|---|---|---|
| `0x103a1930` | `0x104a9f5c` | D2Game `0xe0a60` | hostile? (below) | alignment `0x2f4190` + the rule |
| `0x103a1b40` | `0x104aa7ac` | D2Game `0xe07a0` | refresh the path target | (skill target `0x48fe20`) |
| `0x1030aef0` / `0x1030af20` | `0x1049b1c8` / `0x1049b598` | D2Common [#10121](https://github.com/synpoox/pd2-resurrected/issues/10121) / [#11091](https://github.com/synpoox/pd2-resurrected/issues/11091) | skill elemental min / max | ElementalDamage `0x437d00` |
| `0x1030bfc0` | `0x1049b748` | D2Common [#10239](https://github.com/synpoox/pd2-resurrected/issues/10239) | monster is undead | monstats `+0x3c` & `0x1800` |
| `0x103a1660` | `0x104aa91c` | D2Game `0x11d0` | roll below n on a seed | `d2r::RandomBelow` |
| `0x103a1860` | `0x104a994c` | D2Game `0x4df20` | add life, at most max life | UnitStat / SetUnitStat |
| `0x10305760` | (PD2's own) | | 1.13c's area apply | AreaApply `0x436e90` |
| `0x1030dcd0` | `0x104a8bdc` | D2Common [#10689](https://github.com/synpoox/pd2-resurrected/issues/10689) | set overlay (no layer) | `0x349020(unit, overlay, 0)` |
| `0x103a2ad0` | `0x104a98b4` | D2Client `0xa60d0` | client missile owner | `0x9a7b0` |
| `0x103309e0` | `0x1049a7f8` | D2Client `0xa1860` | client area search | `0x2142d0` |
| `0x10328ee0` | `0x1049b310` | D2Client `0xb30f0` | client missile create | `0x1b7760` |

## srvdo 176 (Holy Light)

The install routine `0x1036ba20` writes srvdo 176 = `0x103b3cc0` (D2Game do table `+0x2c0`). D2R's 176 is empty.

1. The skill record (null: 0). The caster's path target, after `0xe07a0` refreshes it; none: return 1.
2. Heal min/max = cltcalc2 / cltcalc3 `<< 8` (1.13c skill `+0x118`, `+0x11c`; D2R `+0x170`, `+0x174`). Damage
   min/max = the skill's elemental min/max with synergies.
3. When the target is hostile to the caster the heal is halved, otherwise the damage.
4. D2Game's area search around the target with radius aurarangecalc (`+0x64`, D2R `+0x84`), unit mask aurafilter
   (`+0x50`, D2R `+0x70`; 0 is the search's default `0x583`) and callback `0x103043b0`.
5. srvoverlay (`+0x4e`, D2R `+0x6e`) on the target. Returns 1.

Callback `0x103043b0`, per unit: the caster is skipped. An ally heals min + rand(max - min) on the caster's seed and
gets overlay 26. Anything else takes a 0x70-byte damage block: result flags 1, or 9 (knockback) for an undead monster
that is not the target; magic (`+0x20`, D2R `+0x30`) = min + rand(max - min); PD2's apply `0x10305760`.

`0x10305760` is 1.13c's area apply: avoid roll, the same result-flag edits, state 0x36, damage step, events (area 1),
deal. D2R's area apply `0x436e90` does the same steps, so core-rules calls it.

Hostile (`0xe0a60`): pets and missiles are swapped for their owners; the same unit is not hostile; two players ask the
PvP state; otherwise D2Common [#10666](https://github.com/synpoox/pd2-resurrected/issues/10666), which reads [#10830](https://github.com/synpoox/pd2-resurrected/issues/10830)'s alignment (0 hostile, 1 neutral, 2 ally, other types 2) and
calls two units allied when both are 0 or both 2. D2R keeps [#10830](https://github.com/synpoox/pd2-resurrected/issues/10830) as `0x2f4190`; pets carry alignment 2.

Holy Light's row: cltcalc2 `ln12 + Prayer blvl * par6`, cltcalc3 `ln34 + Prayer blvl * par7`, aurarangecalc 6,
EType mag, srvoverlay valkyriestart, cltdofunc 36 (D2R's own, the same as Fist of the Heavens'), cltmissilea
holylightdelay. srvmissilea holylight is not read by srvdo 176.

Deviations (approximated, on the deviation list): two players count as allies; the target is D2R's skill target. Not a
deviation: core-rules halves the rolled damage, PD2 rolls between halved bounds (the same up to 1/256 of a life point).

## chit 66 (holylightdelay)

PD2's missile install `0x103a6c50` writes clthit 66 = `0x103a69c0` (D2Client table `[0x105e1364]`); D2R's 66 is
empty. On a hit: with the missile's owner and its CltHitSubMissile1 (1.13c `+0x2c`, D2R `+0x4e`) a valid row, the
client area search around the missile with radius cHitPar1 and callback `0x103a6bd0` puts that missile on each unit
(flags 0x22, the unit as origin and target, the row's Skill and the missile's level), skipping the unit hit. Returns 1.

PD2's holylightdelay names no CltHitSubMissile1, so the hit draws nothing more; the delay missile itself is drawn by
cltdo 36.

## chit 26 (fistoftheheavensdelay)

PD2 leaves clthit 26 vanilla: 1.13c D2Client `0xbad40`, read from the install's table at `0xdda10`. D2R's 26 is
`0x1adcb0`. Both: the missile's HitSubMissile1 must be set; owner, skill and level from the missile; zero when the
missile's target unit is gone; radius cHitPar1, else aurarangecalc (at least 1); unit mask `0xa683` when aurafilter is
set; per unit, CltHitSubMissile1 from the delay missile to the unit's position (flags 0x20).

They differ:

- **The limit.** Both compute cHitPar2, else calc4. 1.13c overwrites the result before storing it, so its callback
  (`0x6fb663c0`) compares the unit count against an unset stack slot and creates no bolt once that slot is not above
  the count.
  D2R stores it (at least 1) and creates at most that many. Fist of the Heavens' calc4 is ln12 = 50.
- **The struck unit.** D2R's callback (`0x1bc380`) aims the bolt for the missile's target unit at the caster; 1.13c
  aims it at the unit.
- D2R also sets the unit as the missile's target and passes UnitStateKey of the delay missile (`+0x68`); core-rules
  keeps D2R's key (1.13c's parameter block has no such field) and leaves the target unset, as 1.13c does.

So core-rules builds 1.13c's function in clthit 71 (`tools/pd2_ids.py` FUNCS moves 26 there) with no limit
(approximated, on the deviation list).

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w17`: `holy-light-cast` (heal range from cltcalc2/3, the halved side,
radius 6), `holy-light-client-hit` (the client drew holylightdelay and its chit 66 ran), `fist-delay-hit` (chit 71's
radius and one bolt per unit).
