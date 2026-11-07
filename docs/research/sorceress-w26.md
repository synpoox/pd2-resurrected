# Sorceress: Teleport and blocked areas ([W26](https://github.com/synpoox/pd2-resurrected/issues/101))

Answers work item [W26](https://github.com/synpoox/pd2-resurrected/issues/101) ([#101](https://github.com/synpoox/pd2-resurrected/issues/101)). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) in the Ghidra project
`PD2.gpr`, D2R side from the 93847 dump. Addresses are D2R RVAs unless they start with `0x10`.

## Where PD2 puts it

The install routine `0x1036ba20` writes srvdo 155 = `0x103b16d0` (D2Game do table `+0x26c`). Four rows reach it:
Teleport, Blink, LucionBlink and Mon Bloodwarp. D2R fills srvdo 155 (Death Mark), and 187, the issue's first pick, is
`ItemEffect` 187 of the merged Lightning Bolt and Lightning Fury rows (the item-cast slot shares the do table), so the
merge moves 155 to the router (srvdo 190, by skill id: merged 54, 429, 642, 682).

## srvdo 155

1. The teleport core `0x1036cd20(game, unit, 1)`, also Blood Warp's ([w12-blood-warp](w12-blood-warp.md)); return 0
   if it fails.
2. `calc2` srvmissilea in PD2's ring (`0x1036d250`, the same ring as Poison Dagger's) around the unit's new point,
   flags 3. Only LucionBlink has one: 32 `lucionblinknova`.
3. If `aurastate` is a state: PD2's state helper `0x1036c920`, which calls the timed-state helper `0x1036d7d0` (the
   recast rule of the [W12](https://github.com/synpoox/pd2-resurrected/issues/87) note) with `auralencalc` frames and four stats, each set only on a new list:
   passive_fire_mastery (329), passive_cold_mastery (331), passive_ltng_mastery (330), passive_pois_mastery (332),
   each `-(r + trunc(mastery * r / 100))` with `r = Param2 - calc1` (`0x1037c4c0`: the stat times `r` over 100 as a
   double, truncated). Only Teleport has an aurastate: `teleportdebuff`, `auralencalc` `max(0, 25 - pvp_lld_cd)`,
   calc1 `min(edmn, 50)`, Param2 50. A length under 1 makes no list. Players get packet `0x83` with the timer.
4. Return 1.

There is no range rule: the cast's range is the skill's target point, as in D2R.

## The core and the blocked areas

`0x1036cd20`, in order (`game + 0x6d` is the difficulty):

| Check | PD2 | D2R srvdo 27 (`0x554c40`) |
|---|---|---|
| Room at the unit | none: refuse | same |
| levels.txt `Teleport` 0 or no level record | refuse and send the player the `TeleportBlocked` string (`0x1036d07c`) | refuse, no message |
| Hell, third argument set, level in the set at `0x105de86c` | as above | none |
| State 214 `disable_tele` (`0x1030c320`, the unit's list of a state) | refuse | none |
| `Teleport` 2: collision `0x804` at the point | refuse | same |
| `Teleport` 3: a path search over the map's tiles (`0x10380050`, `0x103097a0`) | may refuse | none (3 acts as 1) |
| The teleport (`0x103a1600`, D2Game's) | 1 on success | `0x491ca0`, same |

The Hell set is never filled: the only references to `0x105de86c` are its static constructor (`0x10190971`, an empty
set), its destructor (`0x103d6e20`) and the two lookups above; a byte search of the DLL for the address finds those
four and no more. So that check never refuses.

PD2's blocked areas are therefore levels.txt's `Teleport` column. PD2's values: 1 everywhere except Duriel's Lair (2),
the Butcher Event Map (0) and four map levels (3: Monastery Map, Realm of Terror, Sanctuary Of Sin Map, Imperial Boss
Map). The map levels are not merged, and every merged level has PD2's value (the `teleport-blocked-areas` check). Towns
refuse Teleport through the skill's `InTown`, as in D2R.

## core-rules

`plugin/core-rules/skill_functions.cpp`: `TeleportCore` refuses under `disable_tele` (merged state 260; PD2 gives it
to RathmaPrison's targets), then calls D2R's srvdo 27; Blood Warp now uses it too. The router's Teleport runs the
core, the ring and the debuff (`TimedStatList`, now taking several stats).

Deviations (approximated, on the deviation list): no `TeleportBlocked` chat line, no Teleport 3 path search (no merged
level has 0 or 3), and no `0x83` timer packet on an extended list (as Blood Warp).

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w26`: `teleport-cast` (the debuff's state, length and values
  against PD2's calc), `teleport-blocked-areas` (the merged `Teleport` column against PD2's).
- `teleport-cast` passed on Slot6 with a Sorceress at Teleport 5, Fire Mastery 10, Cold Mastery 4: reduction 46 for
  25 frames, fire 56 -> -71, cold 16 -> -53, lightning and poison 0 -> -46 (the skill's tooltip also reads -46%).
- `TeleportDebuff` has compile-time checks; `tools/tests/test_ids.py` checks the 155 -> 190 merge.
