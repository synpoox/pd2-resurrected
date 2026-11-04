# Paladin: Blessed Hammer (W20)

Answers work item W20 (#95). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and its 1.13c `D2Game.dll`,
`D2Client.dll` and `D2Common.dll` in the Ghidra project `PD2.gpr`, D2R side from the 93847 dump (`ImageStamp
0x6ab3782c`). Addresses are D2R RVAs unless they start with `0x10`; 1.13c addresses are offsets in that DLL.

## Where PD2 puts them

The missile do installs write sdo 40 = `0x103a7410` (`0x103a7460`, server table `[0x105e168c] +0xa0`, next to 38
`0x103a7250` and 39 `0x103a72d0`) and cdo 72 = `0x103a6300` (`0x103a6330`, client table `[0x105e1240] +0x120`).
PD2's blessedhammer is the only row that uses them (vanilla's has sdo 1 / cdo 1).

D2R fills both slots: sdo 40 `0x455450` and cdo 72 `0x1b4ea0` create the row's sub-missile once (flag 4), then run
the default frame. So core-rules takes the next free slots, sdo 47 and cdo 80, and the merge points the row there
(`tools/pd2_ids.py` FUNCS).

## sdo 40 (`0x103a7410`)

`(game, missile)`:

1. No game, missile, missile data (1.13c unit `+0x14`) or path (`+0x2c`): return 0.
2. D2Common #11164 (`0x348b0`) on the path with `0x180`: the path's collision mask (1.13c path `+0x50`).
3. The saved vanilla frame (`[0x105e0ec4]`, filled from D2Game `0x3f320`, 1.13c's srvdo 1; it matches D2R's
   `0x463100` step for step).
4. A result of 2 with range frames left (D2Common #10737, missile data `+0x10`) becomes 1.

## cdo 72 (`0x103a6300`)

`(missile)`: no missile, data or path: return; else the same mask, then 1.13c D2Client `0xb17c0`, vanilla's cltdo 1
(D2R `0x1b5380`).

## What it changes

Missile create sets the path's mask from CollideType's entry in a table (D2R `0x5371a0` through `0x342740`, table
`0x2391378`): CollideType 3, the hammer's, is `0x184`. PD2's `0x180` drops `0x4`, the missile-blocking walls, so the
hammer flies through walls and still hits players and monsters (bits `0x80`, `0x100`). srvdo 1 returns 2 when the
path reports a collision bit `& 5`; PD2 keeps the hammer flying on any 2 while it has range left, whatever caused it
(which collisions still set those bits under `0x180` is not traced).

D2R's mask setter `0x342740` writes path `+0x64`. D2R's srvdo 1 also reads the CollideType table directly for its unit
test, and that part is unchanged in PD2.

## core-rules

`plugin/core-rules/missile_functions.cpp`: sdo 47 (`SrvDoBlessedHammer`) and cdo 80 (`CltDoBlessedHammer`) do the steps
above with D2R's functions: `PathSetCollisionMask`, then `MissileSrvDoDefault` / `MissileCltDoDefault`, and on the
server 2 -> 1 while `MissileSrvFrames` is above 0. No deviation from PD2.

For the log, the server half compares the missile's last unit hit (missile data `+0x10`, type `+0x20`, id `+0x24`,
written by `0x3bd580` in the collision for LastCollide rows, which blessedhammer is) before and after the frame.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w20`: `blessed-hammer` (merged sdo/cdo 47/80, every logged path
  mask `0x180`, the hammer moved, a monster hit, the client half ran) and the two slots in `core-rules-loads`. The
  log counts a hit only when the struck unit differs from the last one, so a repeat hit on the same unit is not logged.
- `tools/tests/test_ids.py`: the merge moves 40 / 72 to 47 / 80.

Run on Slot6 (2026-11-04), the level 90 Paladin with Blessed Hammer at 20, Hell, god mode, 8 zombies by the Den of
Evil cave in the Blood Moor: both checks pass. The path mask reads `0x180` on server and client, a hammer hit a zombie
(unit type 1) 1 frame out, frames ran down from 89, and collisions near the end were kept flying (2 -> 1); a hammer
was drawn over the cave's rocks. No crash.
