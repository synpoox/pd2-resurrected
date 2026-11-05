# Assassin charge-up finishers: server half ([W29](https://github.com/synpoox/pd2-resurrected/issues/104))

Answers work item [W29](https://github.com/synpoox/pd2-resurrected/issues/104) ([#104](https://github.com/synpoox/pd2-resurrected/issues/104)), on the slots [w25-assassin-finishers](w25-assassin-finishers.md) chose. PD2 side read
from `ProjectDiablo.dll` (VAs `0x10...`) and PD2's 1.13c `D2Game.dll` (RVAs named "D2Game") in the PD2 Ghidra
project, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`); other addresses are D2R RVAs. Read statically.

## What [W25](https://github.com/synpoox/pd2-resurrected/issues/100) left open

- **Shared charges (`0x1036ee80`)**: confirmed. For each progressive state (data tables `+0x16c`, count `+0x170`) the
  unit has a list of, it reads the list's skill (stat 350), that skill's `aurastat1`, and the list's value of it. It
  returns the highest, clamped to 0-3.
- **Release dispatcher**: PD2 does not hook it. Its version table holds 1.13c's release (D2Game `0x91b70`) once
  (`0x104a90f4`), read by an address accessor (`0x1031b5b0`) whose one user is a call wrapper (`0x103a1b20`) that two
  PD2 do functions (`0x103af640`, `0x103b4180`) call. So the finishers release through D2R's own `SrvPrgDispatch`
  (`0x56c3e0`), which does what 1.13c's does ([W25](https://github.com/synpoox/pd2-resurrected/issues/100)). Both its prg calls pass the level in R9, so a prg function gets
  `(game, unit, skill, level)` whether the release or srvdo 170 calls it.
- **The 139 missile helper**: `[0x105e08b0]` is resolved from version table entry `0x104a91a4` to D2Game `0x91340`,
  vanilla's charge missile: a skill with charges gets srvmissilea/b/c for 1/2/3+ charges, any other skill
  srvmissilea. D2R has the same function (`PickSkillMissile`, `0x571700`, used by D2R's srvdo 37). D2R's own 139
  (`0x585440`) takes srvmissilea (`+0x68`), so the four monster rows on 139 get the same missile from both: the
  core-rules `verify` row is data.
- **The Cobra Strike branch of 171/172** (skill 265 only, not RakanothStrike): it fills a hit's damage block for the
  skill and adds its poison, scaled by its rolled length over ELen (D2Common [#10510](https://github.com/synpoox/pd2-resurrected/issues/10510), version entry `0x104a8b1c`), to
  each missile's poisonmindam and poisonmaxdam (`0x1036e500`). As in Poison Dagger ([W11](https://github.com/synpoox/pd2-resurrected/issues/86)), the port takes the
  skill's rolled poison from D2R's fill, whose length is ELen, so the scale is 1.

## srvdo 170 (`0x103b2c60`): router 190

1. Shared charges (above). At 3 every progressive list is freed and the count is 0.
2. The target is the path's target unit (not the caster); `0x103049e0(unit, target, 5)` must pass. For a player
   target, `0x1037c820` sets a flag that lets results with `0x8390` count as hits.
3. Roll: to-hit = the skill's ToHit (`0x1030b960`) + the unit's progressive_tohit (stat 325).
4. On a hit (bit 0, and the player flag or none of `0x8390`): charges are added (PD2's AddCharge copy `0x1036efe0`)
   while this skill's count is at most the shared one, so it ends at the shared count + 1. Cobra Strike and
   RakanothStrike (`aurastat1` 319, `progressive_steal`), at the 2nd charge: a stat list (flags 4, until frame + 3, no
   state or event) with lifedrainmindam and manadrainmindam = calc1 and damagepercent = calc2.
5. Vanilla `0x922a0(unit, block)` (progressive states whose record byte `+0x44` is 1), the damage fill
   (`0x1031b4a0`), then vanilla `0x91e30(unit)` (types 2-4) when the count is 1 and the skill isn't Cobra Strike, and
   the queue (`0x103a1630`). Vanilla runs those two only in its release; their per-type bodies (D2Game `0x91f10`,
   `0x91400`, `0x91a30`, `0x918d0`) are not read.
6. Hit after the fill: srvprgfunc[min(charges, 3)] through the do table (bound 191) as (game, unit, skill, level),
   then the charge packet (`0x103a1c40`: target, unit, skill, level, the unit's next random number with the seed
   left as it was, 0, n), which is the release packet. Miss: the charges added in step 4 are taken back and an empty
   list is removed. Then the hit is applied (D2Game `0x10310eb0`).

PD2 calls no hand-alternating function (D2R's srvdo 35 alternates claws before calling 34).

**Port.** D2R's srvdo 34 (`0x570a70`) is the same hit with one charge: target, to-hit (`0x339040`), roll
(`0x44b720`), queue (`0x44b600`), `AddCharge` (`0x56dae0`, D2R's copy of step 4's adder: list with stats 350/351,
timer, +1 up to 3, aurastat2) when the queued hit has bit 0, apply (`0x44b2b0`). core-rules' `SrvDoChargeUp` does
step 1, calls D2R's 34, and when the count went up adds charges to the shared count + 1, fires the prg function and
sends `SendChargePacket` (`0x5396d0`). Building the hit in core-rules would mean rebuilding srvdo 34's damage block
(an inline list and a tagged pointer with destructors), so the deviations stay (deviation list, server half):

- to-hit without progressive_tohit (whether D2R's `0x339040` adds stat 325 is not read);
- a hit counts on the queued result: PD2's `0x8390` results on monsters build charges here, and a hit the fill turns
  into a miss keeps its charge;
- step 5's progressive bonuses are not run (D2R runs them only inside its release, `0x56bfe0`);
- the prg function fires after D2R's 34 has applied the hit, where PD2 fires it before (a killing hit's prg missiles
  aim at the target only if it is still the skill's target once dead: not checked; the game check's ring position
  shows it);
- the steal list is added before the call, whenever a hit would pass 2 charges (count at most 1, shared at least 1),
  so it is there for that hit's leech; a miss keeps it its 3 frames. Built as PD2 builds it: `NewStatList`
  (`0x2f7300`, flags 4, frame + 3) and `AttachStatList` (`0x2f2cb0`), no unit event.

Only aurastat2 (progressive_tohit) is set on the finishers' rows, which both adders handle; PD2's also adds
aurastat3-6.

## The prg functions

- **171 -> 184** (`0x103b30a0`): prgcalc[charge] (1.13c `+0x38`, D2R `SkPrgCalc1` `+0x58`) missiles of the charge
  missile, flags `0x10820` (D2R's CreateMissile reads `0x10000` as 1.13c's does: it goes with params `+0x64` to the
  missile's setup `0x3bf9b0`), 1.13c params `+0x44` = 1, from the caster to points along PD2's spread (`0x1037d780`):
  Multiple Shot's line through the target, stepping the other way, with PD2's own slip for a target within 2 subtiles
  (`ChargeSpread`).
- **172 -> 185** (`0x103b3360`): PD2's ring (`0x1036d250`, the one Poison Dagger uses) of prgcalc[charge] (64 for
  every user) of the charge missile at the target's position, or the caster's with no target, flags 3.
- **39 -> 186** (`0x103aee00`): vanilla srvdo 38, then 39 (D2R's 38 `0x56ef30`, 39 `0x571860`).
- **139 -> 181** (`0x103b0cf0`): D2R's 139 with the charge missile (Blades of Ice's charge 2: srvmissileb,
  boiwavemaker).

All four read the charge with PD2's `0x1037ddf0` rule (the list's aurastat1 value, run only at 1-3).

## Merge

`tools/pd2_ids.py` FUNCS: srvdofunc 170 -> 190; srvprgfunc1-3 171 -> 184, 172 -> 185, 39 -> 186, 139 -> 181 (only
the finishers use those values there). The stand-ins (`merge/standins.tsv`) keep only the client columns
(cltprgfunc1-3) for [W30](https://github.com/synpoox/pd2-resurrected/issues/105).
