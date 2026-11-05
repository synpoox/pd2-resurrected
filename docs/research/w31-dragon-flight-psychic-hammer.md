# Dragon Flight and Psychic Hammer's damage (W31)

Answers work item W31 (#106). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and PD2's 1.13c `D2Game.dll`
(RVAs named "D2Game") in the PD2 Ghidra project, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`); other
addresses are D2R RVAs. Read statically.

## srvdo 178 (Dragon Flight)

The install routine `0x1036ba20` writes srvdo 178 = `0x103b4180` (D2Game do table `+0x2c8`). D2R's 178 is empty. Only
Dragon Flight uses it. `(game, unit, skill, level)`:

1. The skill record (null: 0). The target is the unit's path target (`0x103096b0(unit + 0x2c)`); none: 0.
2. The teleport core `0x1036cd20(game, unit, 0)` (Teleport's, [sorceress-w26](sorceress-w26.md); the 0 skips the Hell
   level set, which is never filled); a refusal returns 0, with no kick.
3. A zeroed 0x70-byte damage block. To-hit = the skill's (`0x1030b960`) + the unit's progressive_tohit (stat 325).
4. The melee roll (`0x103048a0(target, unit, game, to-hit, 0)`: no always-hit). On a hit: damage% (block `+0x0c`) =
   **calc2** (`+0x13c`), then the melee fill (`0x103a1bc0`).
5. The queue (`0x103a1630`, flag `0x80`), the release (`0x103a1b20`, 1.13c's `0x91b70`: srvprgfunc for the charges,
   [w29-assassin-finishers](w29-assassin-finishers.md)), the hit (`0x10310eb0`).
6. PD2's aurastate helper `0x1036cb90(game, unit, unit, skill, level, 1, 0)`: a stat list of aurastat1-6 with
   aurastatcalc1-6 for auralencalc frames. It returns before anything when aurastat1 is empty, as on PD2's Dragon
   Flight row. Returns 1.

PD2's row: calc2 `ln12` (Param1 100, Param2 25: 100 + 25 per level after the first), anim `KK` (one do frame),
srvstfunc 12, no aurastate or aurastat.

**D2R's own (srvdo 52, `0x56fc80`)** splits the cast over two do frames, on a counter in the unit's data (`+0x24`):
the even call clears the unit's skill flag and teleports (`SkillTargetXY`, the level's `Teleport` column, collision
`0x804` when 2, `0x491ca0`); the odd call kicks: to-hit `0x339040(unit, skill, level)` + stat 325, roll
`0x44b720(game, unit, target, to-hit, always, 0)` with always-hit from Param8 (`0x574070`), damage% (block `+0x1c`) =
**calc1**, fill `0x573af0(game, &block, unit, target, skill, level)`, queue `0x44b600(..., skill +0x24d or 0x80)`,
release (`SrvPrgDispatch` on `QueuedHit`), hit `0x44b2b0(game, unit, target, 0, 1)`, then the skill flag back on
while the caster lives, and the block's cleanup. The block has `d2r::LeapKnockback`'s layout (list `+0x40`, tagged
pointer `+0x158`).

**Port.** core-rules' srvdo 178 does PD2's steps in one call with D2R's calls of srvdo 52's kick: Teleport's core
(`TeleportCore`, W26), then to-hit, roll with no always-hit, damage% = calc2, fill, queue (`0x80`), release, hit, and
the cleanup. It keeps srvdo 52's skill-flag clear and set (D2R's own bookkeeping, not in 1.13c, so not a deviation from PD2),
clearing it only after a teleport that went. The aurastate helper
is left out (nothing to do for the only row).

Deviation (approximated, on the deviation list): the target is D2R's skill target, where PD2 reads the path target,
as for Holy Light (W17); Teleport's core brings Teleport's approximations.

## dmg 15 (Psychic Hammer)

PD2's server missile install `0x103a7220` writes srvdmg 15-19 (D2Game table `[0x105e1648]`, `+0x3c`): 15 =
`0x103a6c90`. Only psychichammer uses it in PD2. D2R's 15 is Holy Bolt's (holybolt, fistoftheheavensbolt), so ours
takes 22 (20 and 21 are W6's and W9's; 18 and 19 stay for PD2's own).

`0x103a6c90(game, missile, target, damage)`: with the missile's row and skill valid and its owner (`0x103749f0`):

- hits = calc1 of the missile's skill at its level for the owner (`+0x138`); step = calc2 (`+0x13c`);
- left = the missile's chain count left (`0x1030b7b0`, version entry `0x1049b488` = D2Common #11018, the count
  srvhit 12 reads; D2R's `MissileBounces` `0x3bc0a0`);
- cut = (hits - left) * step; at more than 99, magic damage (block `+0x20`, D2R `+0x30`) = 0, else magic * (100 -
  cut) / 100 (32-bit signed; the port multiplies in 64, the same below 2^31).

PD2's Psychic Hammer: calc1 `min(2+(lvl/10), 6)` ("# hits"), calc2 20 ("dmg reduction per hit"). srvhit 12 (Chain
Lightning's chain, 1.13c D2Game `0x3d020`, D2R `0x45d5e0`) throws the next hammer with one bounce less, so the k-th
hit is cut by (k - 1) * 20%: the first hammer starts with calc1 bounces (Psychic Hammer's srvdo 26, D2R's own, sets
it; not read statically, seen in game: level 10, calc1 3, the first hit logs 3 bounces and the second 2).

**Is it reached?** Only D2R's srvhit 1 (`0x45bc40`) references the dmg table in Ghidra's xrefs, but D2R's collision
(`0x4639a0`) also calls `[0x2391270 + n*8]` (a computed call the xrefs list misses): after the row's srvhit, when its
result has bit 2, it builds the damage (`0x465420`), calls the row's dmg function and deals it (`0x462e40`). 1.13c does
the same in `0x3ede0`. srvhit 12 returns 3 on a hit in both, so dmg 22 runs on every hammer hit.

Port: exact (`SrvDmgChainFalloff`).

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w31`: `dragon-flight-kick` (damage% = calc2 on a hit, 0 on a
  miss) and `psychic-hammer-falloff` (calc1, calc2 and each hit's magic against PD2's rule).
- `tools/tests/test_ids.py`: psychichammer's dmg 15 merges as 22, Dragon Flight keeps 178.
