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
- Apply: D2R's area apply `0x436e90` (the one its area knockback callback `0x43a740` calls), copied step by step:
  avoid rolls (`0x44b930`), `0x5889e0`, the hit's effects and events (`0x44df10`, `0x44ce80`), the damage
  (`0x44a9b0`). D2R's area apply asks `0x44ce80` for events 6 (domissiledamage) and 2; core-rules asks for a melee hit's
  5 (domeleedamage) and 1, as PD2's apply (`0x10305880`, event step `0x10305c30`) fires them.
- PD2 skips some handlers while a splash hit fires its events, so a splash never splashes again. The handler sets are
  built at start-up (`0x10111830`, `0x10111890`): Revive's event 32 (`0x10347370`) and item event functions 14 and 20
  (`+0x38`, `+0x50` of PD2's handler table), checked by the event walker `0x10347510`. core-rules refuses a splash
  inside a splash and skips Revive's event then. Other skill-on-hit procs and item event 14 still run per splashed
  unit (#125).
- Life leech: `0x44c030` fills it (`+0x120`), so halving it after the fill is PD2's rule.

Revive's event 32 calls the splash directly, as PD2 does. A struck unit with no position left (killed by the hit) gives
no splash but still returns 1.

Deviation (approximated, on the unported list): the current skill's damage, the item stat radius term, the weapon -1
and the room check are not ported.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w33`: `melee-splash` (radius against calc3, units hit) and
`revive-event` (a non-zero splash).
