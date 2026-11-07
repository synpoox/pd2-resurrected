# Necromancer: Poison Dagger

Answers work item [W11](https://github.com/synpoox/pd2-resurrected/issues/86) ([#86](https://github.com/synpoox/pd2-resurrected/issues/86)). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`, 1.13c addresses resolved through
its version tables) by static disassembly. D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are D2R
RVAs unless they start with `0x10`.

## srvst 68 (`0x103b6690`): data

Bounds-checks the skill id, then calls PD2's srvst table slot 32, the generic melee start (vanilla, not replaced by
PD2). D2R's 68 (`0x569b20`) is Cleave's start and differs from its 32 (`0x5695d0`), so the merge maps 68 -> 32
(`pd2_ids.FUNCS`) for Poison Dagger and BaalMinionBossBerserk. D2R's melee start fills the queued hit with the skill's
elemental damage (`0x567070` calls `0x437d00`), so the melee hit carries Poison Dagger's poison.

## srvdo 164 (`0x103b25c0`): router 190

1. Sets the skill flag and takes the target from the caster's path; no target returns 0.
2. Rolls to-hit (the skill's ToHit), fills the damage and applies the melee hit (D2Game `0xded30`).
3. Fills a second block with the skill's damage and takes its poison as `poison * rolledLength / ELen` (D2Common
   #10510 is ELen with synergies).
4. `0x1036d250`: a ring of srvmissilea at the target's position, MissileParams flags 3 (placed at x/y, target an
   offset), one missile at every `64 / Param3`-th of D2's 64 directions (Param3 0 or over 64: 64), so Param3 10 makes 11
   missiles. Each gets the poison added to poisonmindam (57) and poisonmaxdam (58); stats 21, 22, 59 and 326 get 0.
5. Returns 1, whether or not the hit landed.

The srvdo table ends at 190 (core-rules-inventory Q1), so 164 joins the router (skill ids 73 and 550, Spider Boss).
core-rules runs D2R's own Poison Dagger do function (srvdo 32, `0x55cc50`: skill flag, target, apply the queued hit)
for steps 1-2, then step 4. The poison is `0x437d00` on a zeroed block: `+0x38` = min + rand(max - min), `+0x3c` = ELen.
Since the fill's length is ELen, PD2's rescale is a factor of 1 (approximated on the deviation list). D2R's direction
offsets are the int32 tables at `0x1d1b650` (x) and `0x1d1b750` (y), radius 30, the ones Nova's srvhit 5 aims at.

Spider Boss has no Param3, so it makes 64 clouds, as in PD2.

## cltdo 102 (`0x103ab760`): 122

Takes the target from the caster's path and calls `0x1036d370`, the client ring: cltmissilea at the target's x/y with
count 6 (pushed as a constant), so 7 client clouds whatever Param3 is. D2R fills cltdo 102; core-rules uses free 122.

## shit 61 (`0x103a84f0`): 61

Needs a hit unit and the owner, then gives the hit unit (`0x1036c920`) state sHitPar1 for sHitPar2 frames from the
missile row's Skill at the missile's level, with stat sHitPar3 = cHitPar1 and stat cHitPar2 = cHitPar3 (a stat of -1 or
out of range is skipped). Returns 1 with no game, missile, row or valid state, 0 with no hit unit or owner, else
`2 + (row +0x186 != 0)` (1.13c's CollideKill; D2R's is `+0x1ae`, field table `0x3c4233`).

The result matters: D2R's missile collision (`0x4639a0`) calls the hit function through its table and builds and applies
the missile's damage (`0x465420`, `0x462e40`) only when the result has bit 2. A hit function that returns nothing deals
no damage, so core-rules returns PD2's value.

Its sHitPar1 / sHitPar3 / cHitPar2 are PD2 state and stat ids: the merge gives them the mod's ids
(`pd2_ids.HIT_PARAMS`; poison_strike 236 -> 282, the throwing potions' 229-232 -> 275-278).

`0x1036c920` also: halves a negative resist (stats 36-45, `0x1036df10`) on a non-player unit already over 99 in it;
handles states 16, 87 and 188 specially. No shit 61 row uses those states; the halving touches the throwing potions'
clouds (resists) and is approximated on the deviation list. Kanemith's blaze dot (state 44, velocity and attackrate -60)
is exact.

Rows on shit 61: poisondaggercloud, the nine `tpot_*_nova/_cloud/_bolt` rows, kanemithblaze2dotfiremedium/small.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w11` (Slot5, 2026-11-03): 4 passed. One level 10 stab made 11
  poisondaggercloud missiles (row 772) with poison 261 over 50 frames (261 * 50 / 256 = 51, the tooltip's 50-53), the
  client made 7, and the cloud hit gave a zombie poison_strike (282) for 52 frames. No crash.
- Same tag (Slot5, 2026-11-04), after the hit function returned PD2's result: 4 passed. The clouds carry poison 522
  over 50 frames (the 261 the cloud is created with plus the rolled 261, as PD2 adds them), the hit returned 2, and
  cloud-hit zombies lost life (a temporary log showed 12 -> 5, 11 -> 4).

Clouds come out on every Poison Strike attack, hit or miss: PD2's srvdo 164 runs the ring after the melee hit with no
to-hit branch (`0x103b2795`).
