# Holy Bolt heal: PD2 srvhit 7 against D2R's (#2227)

Missiles: `holybolt`, `fistoftheheavensbolt`, `holynova`, the only rows on PD2 srvhit 7. Port:
`plugin/core-rules/missile/holy_bolt.cpp`, srvhit 70 (table entry `0x2391260`); `tools/pd2_port.py` maps 7 -> 70.

## PD2: ProjectDiablo.dll `0x103a7840` (PD2.gpr)

`fastcall(game, missile)`, target on the stack, `ret 4`.

1. Returns 1 when game, missile, target or the missile's unit data (`+0x14`) is null, when the missile's skill
   (unit data `+0xa`, level `+0xc`) is past the skills count (`+0xba0`) or has no record (`0x1037c110`), or when the
   missile row (`+0x4 * 0x1a4 + [tables + 0xb64]`) is null.
2. owner = `0x103749f0(missile, 0)`: the missile's owner, or the missile itself. master = `0x103749f0(owner, 1)`: a
   player is itself; a monster gives its AI owner (and that owner's, when a monster); no owner: the unit itself.
3. Heal when `0x1037c820(target)` is 0 (PD2's PvP arena test; PvP is cut, so always) and sHitPar1 (`+0x4c`) is set and
   any of:
   - `0x103a3d90(target, skill, owner)` -> 1.13c D2Game `+0xe0210` (version entry `0x104aa554`): the target is the
     owner's pet and the skill has its skills.txt flag (byte `+7` & 2, `gdwBitMasks` byte 4).
   - `0x103a3db0(target, game, owner, skill)` -> `+0xe0660` (`0x104aa724`): the owner is a player, the target a
     player or a player's monster in the owner's party, and the skill has its flag (byte `+7` & 4, `gdwBitMasks` byte 8).
   - master is not null and `0x103a3df0(master) == 0x103a3df0(target)`, through `+0xdfa60` (`0x104aa954`): a
     player's party id (1.13c `+0x9beb0`, 0xffff without a party) in AX, else `or ax, 0xffff`. The caller compares
     EAX whole: a player's upper half is its unit id's (0), any other unit's is its own pointer's. So two players in
     the same party match, **two players without a party match**, and a non-player matches only itself.
4. Heal: lo = `0x1030ad70(owner, skill +0x118 (cltcalc2), skill, level)`, hi the same with `+0x11c` (cltcalc3);
   hi = lo + 1 when hi <= lo; amount = (lo + rnd % (hi - lo)) << 8, rnd from the missile's seed (`0x1036f320`, the D2
   LCG). Below 1: `0x103a3dd0(21, -1, missile, 22)` -> `+0x39b40` (`0x104a8fac`): stats 21 / 22 of the missile, 0 when
   either is below 1, else min + `+0x11d0`(seed, max - min). Life = min(stat 6 + amount, max life), set; ProgOverlay
   (row `+0x36`) when not 0. **Returns 5.**
5. No heal: target type 0: sHitPar2 (`+0x50`) 0 -> 2, else 4. Type 1: sHitPar2 0 -> 2; 2 -> `0x1030bff0`, other ->
   `0x1030bfc0`, true -> 2, false -> 4. Other types -> 4.

## D2R 93847: srvhit 7 `0x45cb80`

1. Row (`0x166040`) or target null -> 1. skill = MissileSkill, level = MissileLevel, owner = MissileOwner.
2. Heal when sHitPar1 (`+0x70`), owner, and pet test `0x48e3a0` or party test `0x48e250` (game, owner, target, skill):
   the same two tests as 1.13c's `+0xe0210` / `+0xe0660` (skills.txt byte `+0x27` masks). No third test.
3. Heal: calc1 (`+0x190`) .. calc2 (`+0x194`) via EvalSkillCalc for the owner, x 256, roll on the missile's seed;
   the same 21 / 22 fallback inlined. Life clipped, ProgOverlay (`+0x58`) when > 0. Returns 1 when sHitPar3 is 1 or
   3, else 0.
4. No heal: player: sHitPar2 0 -> 3 when sHitPar3 is 2-3, else 2; other sHitPar2 -> 4 when row `+0x9c` < 1, else 2.
   Monster: sHitPar2 1 `0x3af2c0`, 2 `0x3af190`, 3 either. Other types -> 4.

With PD2's rows (sHitPar1 1, sHitPar2 0, sHitPar3 0) the no-heal results are equal (2 for players and monsters,
4 otherwise). Collision (`0x4639a0`): bit 4 skips the hit events and damage, bit 2 deals the damage, bit 1 plays the
hit; PD2's 5 is a shown hit with no damage, D2R's 0 runs the hit events and no effect.

## The port

srvhit 70 runs PD2's step 1-4 and, when no test passes, calls D2R's own srvhit 7 (read from table slot 7 at install):
its heal cannot fire then (its tests are PD2's first two), so undead and monster damage stay D2R's, which equal
PD2's for these rows. The pet and party tests and the party id getter are D2R's, found by signature (call sites in
`0x45cb80` and `0x48e250`). A missed signature leaves the slot on D2R's srvhit 7.

PD2 data: Holy Bolt cltcalc2 `edln`, cltcalc3 `1 + edln` (heals edln exactly); Holy Nova cltcalc2 = cltcalc3 (heals that
value); Fist of the Heavens has no cltcalc, so its bolts heal by their 21..22 roll.

Not ported: the non-player 64 KB pointer accident in step 3; PD2's PvP arena gate (PvP is cut). Without that gate, two hostile players without a party also heal each other (owner-approved). An ownerless missile goes
straight to D2R's srvhit 7: in PD2 it is its own owner and no test passes.
