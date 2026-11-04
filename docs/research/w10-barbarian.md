# Barbarian group: Bash, Find Potion / Find Item, Frenzy start

Answers work item W10 (#51). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) by static disassembly, plus a
runtime read of PD2's Game.exe for its tables. D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are
D2R RVAs unless they start with `0x10`.

## Find Potion / Find Item (PD2 srvst 70, cltst 55, srvdo 168, cltdo 104)

PD2's four functions are D2R's own (srvst 33/34, cltst 26/28, srvdo 69/72, cltdo 38/40) with one guard in front:

- refuse when the corpse's owner is a player (unit owner type `+0xe8` = 0 with an owner id; 1.13c `+0x94/+0x98`),
  so summons and minions can't be searched;
- refuse for 46 monsters (PD2 hcIdx 743, 746, 747, 750, 755, 800, 809, 826, 861, 870, 879, 882-884, 893, 894, 915,
  963, 964, 996-998, 1000, 1025, 1026, 1058, 1060-1063, 1094, 1096, 1103-1105, 1121, 1122, 1148, 1149, 1164, 1166,
  1174, 1175, 1240, 1242, 1243): map bosses, ubers and the PD2-only-area bosses. Not ported yet (map plan).

core-rules runs the guard in new srvst 82 / cltst 62 and calls D2R's start from its table; the do functions map to
D2R's slots as data, since the start already checked the same target.

## Bash (PD2 srvdo 158, `0x103b1c10`)

Clears the queued hit's knockback bit, applies the hit, and when it landed on a target with flag `0x8000`, rolls
`rand() % 100 < 50` for a second, knockback-only apply. PD2's Bash row has empty ResultFlags (D2R's has 8, always).

D2R: `QueuedHit 0x44d660` (attacker, defender) returns the queued damage block (+0 hit flags, +4 result flags: 1 hit,
2 kill, 8 knockback); srvst 32 (`0x5695d0`) rolls the hit at the start frame. core-rules' srvdo 190 (router) sets bit 8
from a 50% roll on the attacker's seed when bit 1 is set, then calls D2R's srvdo 2 (`0x430240`, the melee do function
that applies the hit).

## Frenzy / Tiger Strike start (PD2 srvst 78, `0x103b6cf0`)

Gets or makes a permanent `temp_skill_buff` state list on the caster and sets `inc_splash_radius` on it:

- Frenzy (147): `calc2 * skill_frenzy / max(calc4, 1)` (PD2's row: calc2 `dm34` with Param3 0, Param4 45; calc4 5);
- Tiger Strike (254): `calc2 * charges` while charges (aurastat1, `progressive_damage`) are under 3, else 0;
- any other skill: calc2.

Merged ids: state 270, stats 169 and 531. D2R's skills record keeps calc4 at `+0x19c` (field table `0x306193`).

## PD2's own slots

PD2 replaces these vanilla slots with its own code (so data that names them needs a check before it is mapped):
srvdo 4, 6, 8, 23, 25, 39, 44-51, 54-57, 64, 71, 104, 112-115, 118, 119, 139, 143, 148, 150; srvst 10, 27-29, 37;
cltst 9 and 54-58; cltdo 8, 83, 94, 97, 98, 100-119.
