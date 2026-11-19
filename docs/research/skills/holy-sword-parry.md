# Holy Sword parry (#1466)

## PD2 (ProjectDiablo.dll)

The avoid roll is `0x103035f0(defender, attacker, ranged)`.

- Chance: `FUN_103a2590(defender)`, the D2Game block-chance function (lazy thunk at `DAT_105e0e94`). Holy Sword's aura adds to it through stat `passive_weaponblock` (skills.txt id 116, calc `dm56 + stat('toblock'.accr)/5`, `2hsw`/`2hcs`, srvstfunc 73).
- Cap: 75 (`0x4b`). A Lucion attacker (class `0x458`; merged id 1129) divides the chance by 3.
- Weapon class: `FUN_1030de50(defender, defender+0x18, &cls, -1, 1)`, a D2Common composit lookup, 0-based: hth 0, bow 1, 1hs 2, 1ht 3, stf 4, **2hs 5**, 2ht 6, ..., **ht2 13**. Class 5 is the two-handed sword swing; class 13 is claws (the Assassin block).
- Class 5 or 13: `rand % 100 < chance` (halved in levels 157, 159, 166) returns `0x10` (block). Class 5 also plays sound 4713 (packet `0x2c`, `0x1037f2c0`).
- No-avoid window (#1482): the roll's first step, for a player (unit type 0) with playerdata (`unit+0x14`) and game (`unit+0x80`): `game+0xa8` (frame) `< playerdata+0x1c0` returns 0 at once, before the block, evade, dodge and avoid rolls. Every avoid sets `playerdata+0x1c0 = game+0xa8 + 4`: block/parry (0x10), evade (8), dodge (4), avoid (2). Only a player with playerdata and a game writes it (a monster's game pointer stays 0, so the write is skipped), so the window is players only and shuts all four avoids; frames f..f+3 are refused (f itself too), f+4 may avoid again.
- The block rolls before evade, dodge and avoid, in any player mode.

## D2R (build 93847)

- Avoid: `0x44ef70(attacker, defender, ranged) -> uint32`, called from `0x44b930`. Results: `0x10` block, 8 evade, 4 dodge, 2 avoid. Entry bytes `48 89 5c 24 08 48 89 6c 24 10 56 57 41 56 48 83 ec 30`.
- Block chance: `0x450070(defender)`. Class: `0x3b6e00(defender, 0x34a360(defender), &out, -1, 1)`. D2R blocks only for class 13. Its unit-type test is `0x34b9d0` (the type, 0 = player), the mode `0x34ab60` (`Unit+0xc`). A player in mode 2 or 3 (plrmode.txt: Walk, Run), or a monster in mode 2 or 0xf (monmode.txt: walk, run), jumps to the evade roll (stat 0x154) and returns 0 or 8: no block, dodge or avoid. PD2 has the same evade-in-walk/run structure but rolls the class 5/13 block first in every mode.

## Port

`core-rules` wraps `0x44ef70` (found by a masked 76-byte signature, one match on 93847) (`AvoidWithParry`, `skill_functions.cpp`). For a defender with class 5 it rolls PD2's parry (`plugin/common/parry_roll.h`, checked by `plugin/tests/parry_roll_check.cpp`); a parry returns `0x10`, otherwise the original runs, so D2R's own class 13 block is untouched. #1483: class 13 also rolls PD2's block (cap 75, Lucion third) in the modes D2R skips (`D2RSkipsBlock`), so claws block while walking and running; in the other modes D2R's block runs once (rolling both would double the chance). The known difference there is Lucion's third, which D2R's claw block lacks: kept. D2R's block chance function `0x450070` has its body behind a jump into the protected section, not in the dump, so whether it caps at 75 is unknown. #1496: in walk and run D2R's avoid returns 0 or 8 at the evade roll (verified in the decompile of `0x44ef70` against PD2's `0x103035f0`); PD2 falls on to the dodge (stat 0x152, melee) or avoid (0x153, ranged) roll when evade is above 0 and its roll failed. The wrapper does the same: original result 0, walk/run mode, evade above 0, then the dodge or avoid roll (4 or 2).

The window: D2R's frame counter is `Game+0x170` (`rva::GameFrame`), the game is `Unit+0xd8`. D2R has no playerdata field to spare and a unit stat is not allowed, so `pd2::parry::AvoidWindow` (`parry_roll.h`) keeps a 16-slot table of (player id, until frame) beside the hook, server thread only. An entry is live only while `0 < until - frame <= 4`, so a new game (frame counter restarted) or a passed window frees its slot at no cost, and a full table overwrites round-robin (`next_ % 16`), which cannot happen with 8 players. The wrapper refuses (returns 0) while live and sets it when the result has a bit of 0x1e, so D2R's own block, evade, dodge and avoid are covered. Checked by `plugin/tests/parry_roll_check.cpp`.

Not ported: the level 157/159/166 halving (those levels are cut), sound 4713, Lucion's third for the class 13 block outside walk and run. The class 5 parry rolls before the original, so it works in every mode.

## Slot check

Slot 2, level 99 Paladin, two-handed swords, `cheat stat passive_weaponblock,50` (the aura was not cast from the skill list), zombies: log `parry: class 5, block chance 50 ... 20 rolls, 8 parries`.

Window check (#1482), slot 2, Hell Blood Moor, level 99 Paladin, two-handed sword, `passive_weaponblock` 50, `cheat god 1`, 8 `fallen1`: 24 logged avoids (`avoid: player 1 result 0x10 at frame N, no avoid until frame N+4`), smallest gap between two avoids 4 frames, none inside the window; two rolls refused at frames 3396 (avoid at 3395) and 3451 (avoid at 3448).
