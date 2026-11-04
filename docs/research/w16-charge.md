# Paladin: Charge (W16)

Answers work item W16 (#91). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and its 1.13c `D2Common.dll` in
Ghidra, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are D2R RVAs unless they start with `0x10`
or `0x6f`.

## Where PD2 puts them

The install routine `0x1036ba20` writes srvdo 180 = `0x103b4870` (do table `+0x2d0`), srvst 72 = `0x103b67e0` (start
table `+0x120`) and cltdo 112 = `0x103abfb0` (client do table `+0x1c0`); the client start routine `0x103ad290` writes
cltst 57 = `0x103ace50` (`+0xe4`). PD2's Charge row is the only one using these four numbers. Vanilla's are srvst 31,
srvdo 67, cltst 25, cltdo 37; D2R keeps them (`0x564dc0`, `0x5622c0`, `0x22e1a0`, `0x22d420`).

D2R leaves srvdo 180 and srvst 72 empty, so core-rules installs there. cltst 57 is D2R's (`0x2b68c0`); cltdo 112 is
free.

## What PD2 changes

All four are vanilla's Charge (`SkilPal.cpp` line `0x330` asserts in both start functions) with two server changes:

1. **Start speed.** Both starts set the path speed to `base * (max(velocitypercent, 50) + Param1) / 100`, base the
   class's charstats byte (1.13c `+0x41`) `<< 8`. PD2 reads it for monsters too (indexed by the monster's id). D2R's
   client start (cltst 25) does the same with Param1 at skill `+0x1b8`; D2R's server start (srvst 31) adds **calc2**
   (`+0x194`) instead, which PD2's row leaves empty, so on merged data D2R's server charged at a third of PD2's speed
   while its client drew PD2's.
2. **Distance and splash.** The server start stores the distance from the caster to the path's first point in the
   skill's param (`0x103091d0`, D2R's unit skill `+0x24`): D2Common ordinal 10080 (`0x6fdcf5e0`), `max(dx, dy) +
   min(dx, dy) / 2`. On the hit (flag 1 clear, target in reach), before the damage is applied, if the caster has no
   `temp_skill_buff` (224, merged 270) list it gets one ending 3 frames later with `inc_splash_radius` (478, merged
   531) = `param / 4 * 20`, and the param goes back to 0.

The hit itself is vanilla's: damage% calc1 (1.13c `+0x138`, D2R `+0x190`), calc4 when the elemental byte is set, then
the damage apply and the target's 0x93 flag; D2R's srvdo 67 does the same calls (`0x44b600`, `0x44b2b0`, `0x349020`).

The client halves change nothing: cltdo 112 counts the param up to 2 like D2R's cltdo 37 (`+0x24`), and cltst 57 sets
the same speed as D2R's cltst 25.

## core-rules

`plugin/core-rules/skill_functions.cpp`:

- srvst 72 calls D2R's srvst 31, then, when it set off a charge (skill flags `0x1001`), sets PD2's speed (players from
  charstats `+0x3d`, monsters from monstats Velocity, as D2R's client does) and stores the distance to the skill's
  target point in `+0x24`.
- srvdo 180 builds the splash list on the hit frame (unless the caster already has a `temp_skill_buff` list), spends
  the distance, then calls D2R's srvdo 67.
- The merge points the client columns at D2R's 25 and 37 (`tools/pd2_ids.py` FUNCS).

The splash radius does nothing until the melee splash row (srvdo 163, `inc_splash_radius`'s reader) is built; Frenzy's
start (srvst 78) waits on it the same way.

Deviation (approximated, on the unported list): the splash list is built on the hit frame before D2R's do function
looks for the target, so a charge that ends without a hit gets a 3-frame list that nothing reads; the distance is
measured to the skill's target point, not the path's first point; a monster's speed is its monstats Velocity.

## The Charge/Whirlwind rule (gate A)

PD2's gate A ([plrmodes](plrmodes.md)) keys on `srvdofunc`: Charge (67 and 180) and Whirlwind (76) allow the same skill
again, and PD2 also lets requests with no skill through to gate B (frame check for the sequence mode). D2R's mode
change (`0x429d10`) keys on skills.txt flags instead (skill `+0x24` & 0x10, `+0x27` & 0x80, `+0x36`), and the merged
Charge and Whirlwind rows have the same flags, so Charge stays in Whirlwind's group with srvdo 180. A skill-less request
during either one reads as queued until the last 2 frames of the sequence in the decompile; PD2's frame-check window
is not compared. Not checked in game: the plugin does not log mode changes. Approximated row on the unported list.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w16`: the start's speed against PD2's formula, and the hit's
  damage% and splash radius against PD2's calc and distance rule.
- Compile-time checks (`static_assert`) beside `ChargeDistance`, `ChargeSplash` and `ChargeVelocity`.
