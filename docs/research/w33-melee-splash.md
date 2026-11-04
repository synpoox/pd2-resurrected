# Melee splash (W33)

Answers work item W33 (#119). PD2 side from `ProjectDiablo.dll` (VAs `0x10...`) in Ghidra, D2R side from the 93847
dump (`ImageStamp 0x6ab3782c`), RVAs.

## PD2

The install routine (`0x1036ba20`) writes srvdo 163 = `0x103b2340` (D2Game do table `+0x28c`). Skills: proc_SplashDamage
(merged 430), Golem Splash (461), Skeleton Splash (465). Revive's aura event 32 (`0x10347370`) calls the do function
itself with skill 358 (merged 430) and level 1; it does not cast it.

srvdo 163 `(game, unit, skill, level)`:

1. The struck unit is the unit's path target (`0x103096b0(path)`); none refuses. A check on the unit's room
   (`0x1037c820`, not decoded) refuses too.
2. For a player whose current skill is not the splash: that skill's id and level, and its record's `+0x130`, `+0x12e`,
   `+0x134`, `+0x1a5` go into the callback's block, so its damage rides along. Smite (97) with `+0x158` set adds a
   state 224 list with stat 478. Otherwise the skill is 0 (Attack) at level 1.
3. Radius = calc3 + `0x10381720(unit, ...)` / 20 (an item stat sum by weapon set, not decoded), 1 less when a weapon
   record's `+0x80 == +0x84`.
4. D2Game's area search (`0x103a1450`) around the struck unit's x/y with unit mask `0x8583` and callback `0x10303190`.
   Returns 1.

The callback `0x10303190`, per unit in range:

- The struck unit is skipped.
- A hit with no to-hit roll (result flags 1, or'd with the current skill's).
- The skill's damage (two D2Game fills), the attacker's weapon damage, then the life leech is halved (at least 1) and
  PD2's own apply (`0x10305880`) deals it. The kicks (255, 270, 275) copy the attacker's cached kick damage instead.

proc_SplashDamage's row: calc3 `max(6 - lvl + (stat('inc_splash_radius'.accr)/20), 0)`, SrcDam 128, ResultFlags 5,
HitFlags 2. item_splashonhit is itemeventfunc 20 (skill on hit) casting it.

## D2R

D2R fills srvdo 163, so the function joins the srvdo 190 router (`tools/pd2_ids.py` FUNCS). The pieces, all from D2R's
own melee and area code:

- Area search: `0x4398b0` (`AreaUnits`), as Leap's landing uses it, centred on the struck unit.
- Weapon damage: `0x44c030 (game, attacker, defender, &damage, 0, srcdam)`, D2R's melee fill (Leap Attack reaches it
  through `0x44b600`). The block is Leap Attack's (`0x56b170`), and so is its cleanup.
- Apply: `0x436e90 (game, attacker, victim, &damage)`, which D2R's area knockback callback (`0x43a740`) calls: avoid
  rolls, then the damage. It fires no melee events, so a splash can't splash again.

Revive's event 32 calls the splash directly, as PD2 does; D2R's event cast (`0x5896e0`) was refused before W33.

Deviation (approximated, on the unported list): the current skill's damage, the item stat radius term, the weapon -1
and the room check are not ported.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w33`: `melee-splash` (radius against calc3, units hit) and
`revive-event` (a non-zero splash).
