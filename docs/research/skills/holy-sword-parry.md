# Holy Sword parry (#1466)

## PD2 (ProjectDiablo.dll)

The avoid roll is `0x103035f0(defender, attacker, ranged)`.

- Chance: `FUN_103a2590(defender)`, the D2Game block-chance function (lazy thunk at `DAT_105e0e94`). Holy Sword's aura adds to it through stat `passive_weaponblock` (skills.txt id 116, calc `dm56 + stat('toblock'.accr)/5`, `2hsw`/`2hcs`, srvstfunc 73).
- Cap: 75 (`0x4b`). A Lucion attacker (class `0x458`; merged id 1129) divides the chance by 3.
- Weapon class: `FUN_1030de50(defender, defender+0x18, &cls, -1, 1)`, a D2Common composit lookup, 0-based: hth 0, bow 1, 1hs 2, 1ht 3, stf 4, **2hs 5**, 2ht 6, ..., **ht2 13**. Class 5 is the two-handed sword swing; class 13 is claws (the Assassin block).
- Class 5 or 13: `rand % 100 < chance` (halved in levels 157, 159, 166) returns `0x10` (block). Class 5 also plays sound 4713 (packet `0x2c`, `0x1037f2c0`).
- After an avoid PD2 holds a 4-frame no-avoid window (`playerdata+0x1c0 = frame + 4`).
- The block rolls before evade, dodge and avoid, in any player mode.

## D2R (build 93847)

- Avoid: `0x44ef70(attacker, defender, ranged) -> uint32`, called from `0x44b930`. Results: `0x10` block, 8 evade, 4 dodge, 2 avoid. Entry bytes `48 89 5c 24 08 48 89 6c 24 10 56 57 41 56 48 83 ec 30`.
- Block chance: `0x450070(defender)`. Class: `0x3b6e00(defender, 0x34a360(defender), &out, -1, 1)`. D2R blocks only for class 13; its player branch skips modes 2 and 3 (walk, run), its monster branch modes 2 and 0xf.

## Port

`core-rules` wraps `0x44ef70` (found by a masked 76-byte signature, one match on 93847) (`AvoidWithParry`, `skill_functions.cpp`). For a defender with class 5 it rolls PD2's parry (`plugin/common/parry_roll.h`, checked by `plugin/tests/parry_roll_check.cpp`); a parry returns `0x10`, otherwise the original runs, so D2R's own class 13 block is untouched.

Not ported: the 4-frame window, the level 157/159/166 halving (those levels are cut), sound 4713, PD2's class 13 (claw) block while walking or running (D2R's own skips it). The class 5 parry rolls before the original, so it works in every mode.

## Slot check

Slot 2, level 99 Paladin, two-handed swords, `cheat stat passive_weaponblock,50` (the aura was not cast from the skill list), zombies: log `parry: class 5, block chance 50 ... 20 rolls, 8 parries`.
