# Assassin charge-up finishers: client half ([W30](https://github.com/synpoox/pd2-resurrected/issues/105))

Answers work item [W30](https://github.com/synpoox/pd2-resurrected/issues/105) ([#105](https://github.com/synpoox/pd2-resurrected/issues/105)), on the slots [w25-assassin-finishers](w25-assassin-finishers.md) chose. PD2 side read
from `ProjectDiablo.dll` (VAs `0x10...`) and PD2's 1.13c `D2Client.dll` / `D2Common.dll` (RVAs named "D2Client",
"D2Common") in the PD2 Ghidra project, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`); other addresses are D2R
RVAs. PD2 reaches vanilla code through version-table thunks (module index, offset in column 0 for 1.13c), resolved
here by reading the table entries. Read statically.

## How the client gets a charge

D2R's handler for the charge/release packet (`0x216380`, which the server's `SendChargePacket` feeds on each
charge-up hit and on a release) does what [W25](https://github.com/synpoox/pd2-resurrected/issues/100) describes for the release: it points the caster's path at the packet's
target, and, with `prgstack`, calls `cltprgfunc1..n-1` through the cltdo table, setting the list's charge to i before
each call, then `cltprgfunc n` (`(unit, skill, level)`, the cltdo signature). So a client prg function reads its
charge from the progressive list, as the server ones do.

## The four functions

- **cltprg 106 (`0x103ab920`) -> cltdo 127**: the client half of srvprg 171. The charge (PD2's `0x1037ddf0`, run only
  at 1-3), the client missile (1.13c D2Client `0x40550` through `[0x105e0a08]`: cltmissilea/b/c for 1/2/3 charges,
  cltmissilea for a skill without), and cltcalc[charge] (1.13c `+0x114`, D2R `+0x16c`) missiles. The aim is the
  path's target unit (D2Common #10392), else the path's target point. Params as the server's: flags `0x10820`,
  owner and origin the caster, start at the caster, 1.13c `+0x44` = 1, aim points along PD2's spread (`0x1037d780`,
  core-rules' `ChargeSpread`). A do-while: at least one missile.
- **cltprg 107 (`0x103aba80`) -> cltdo 128**: the client half of srvprg 172. The same charge, missile and count, then
  PD2's client ring (`0x1036d370`, the server ring's twin: flags 3, the count 0 or over 64 makes 64, one missile at
  every 64/count-th of the 64 directions) at the target unit's position, else the caster's.
- **cdo 69 (`0x103a60d0`) -> cdo 79** (fistsoffireexplode, bladesoficecubes): first the missile's step (1.13c
  D2Client `0xb17c0`, the body almost every vanilla client do function calls; D2R's cltdo 1), then, when the
  missile's frame counter (D2Common #10737, data `+0x10`, counting down) equals its range (#10534, `+0xe`) - 1, its
  `CltSubMissile1` (fistsoffirefirewall, bladesoficeexplode) placed at its position (flags 3, no aim), with the
  missile's skill (`+0xa`) and level (`+0xc`). That is the missile's **first** frame, not its last as [W25](https://github.com/synpoox/pd2-resurrected/issues/100) read it.
  D2R's `0x3bb0f0` returns the range minus the frames left, and D2R's own cltdo 1 tests it for 1 the same way.
- **chit 67 (`0x103a6aa0`)** (clawsofthunderbolt): on a hit unit, `CltHitSubMissile1` (cotthunderstorm) placed at the
  unit hit (D2Client `0x1630` / `0x1660` on it; flags 1), with the row's `Skill` and the missile's level. On the new
  missile it sets the graphic's height to `CHitCalc1` (280, "z height offset") through 1.13c D2Client `0xc7e30`
  (`[0x105e0608]`; gfx `+0x30` `+0xc`, << 11), and writes `cHitPar1-3` (0, 0, -81920, "movement offset * 256") raw to
  the graphic's motion (`+0x10..0x18`): the storm bolt falls from above onto the unit. Returns 1.

## D2R's matches

D2R's Meteor client do (cltdo 9, `0x1b6480`) is 1.13c's (`0xb8f00`) and makes the same two writes on its falling
meteor: `0x193a80(missile, x, y, z)` stores the offset << 11, and `0x193ad0(missile, x, y, z, shift)` the motion,
shifted with `shift` set, raw without. So chit 67 is built exactly: `SetMissileOffset(bolt, 0, 0, calc)` and
`SetMissileMotion(bolt, cHitPar1, cHitPar2, cHitPar3, 0)`. `CHitCalc1` is at D2R `+0xb0` (1.13c `+0x8c` + 0x24, as
the cHitPar and DmgCalc fields move), `CltSubMissile1` at `+0x40` (D2R's cltdo 9 reads it there).

The port evaluates `CHitCalc1` only when the missile has an owner on the client (D2R's missile calc is only known to
run with one); PD2's one user has the constant 280, so that changes nothing in play (an approximated row on the
deviation list). cdo 69's owner test is PD2's own.

No PD2 row names bladesoficecubes as a sub-missile or skill missile, so in the data only fistsoffireexplode (Fists of
Fire's cltmissilea, RakanothStrike's cltmissilec) reaches cdo 79; whether vanilla client code makes bladesoficecubes
by id is not read.

## Slots and merge

`tools/pd2_ids.py` FUNCS: cltprgfunc1-3 106 -> 127, 107 -> 128 (only the finishers use those values there; D2R's
cltdo 106 is Bind Demon's), missiles pCltDoFunc 69 -> 79 (D2R's 69 is corpseexplosion's; only fistsoffireexplode and
bladesoficecubes use it in PD2). chit 67 is free in D2R and keeps its number. The stand-ins and the deviation list's
"not yet" client half row are gone.
