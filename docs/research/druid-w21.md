# Inferno and Arctic Blast (W21): srvdo 182, cltdo 111, the channel take-over

Answers work item W21 (#96). PD2 side from the baseline install `C:\Games\PD2\ProjectD2` (`ProjectDiablo.dll`, base
0x10000000; its table fill `0x1036ba20` writes cltdo n at `[0x105de71c] + n*4` and srvdo n at `[0x105de8fc] + n*4`),
D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). D2R's slots srvdo 182 and cltdo 111 are empty, so both keep
PD2's numbers.

PD2's rows (Inferno 41, Arctic Blast 230): srvstfunc 11, cltstfunc 15 (D2R's Inferno starts), calc1 range
(`min(ln12/2, 32)`, Arctic Blast 24), calc3 missiles (`min(2 + lvl/4, 5)`, Arctic Blast 2), srvmissilea / cltmissilea
the debuff missile.

## srvdo 182 (`0x103b4e40`)

1. A monster: vanilla srvdo 19 (`[0x105e25d0] + 0x4c`), D2R's Inferno (`0x554460`).
2. Otherwise: return 0 without a skill record, a current unit skill, srvmissilea, or a target point.
3. range = max(calc1, 1), count = max(calc3, 1).
4. `0x1037d860`: the target point moved to count/2 + 8 subtiles from the caster on the same line (doubles, truncated;
   unchanged when the target is on the caster).
5. `0x1037d780`: start and step of `count` points through that point at right angles to the aim: Multiple Shot's vector
   steps on caster - target (so the line runs the other way to D2R's srvdo 8). Under 2 subtiles PD2's code moves the
   squared length into y rather than 4 * y; the port keeps it (only reached when the target is on the caster).
6. One missile per point: flags 0x8020, owner and origin the caster, range (params +0x4c, D2R +0x5c) = range.
7. `0x1037a7c0 (game, unit)`: drops the caster's type 0 and 1 events (frame and mode end), moves its animation to the
   next whole frame and adds the frame events of the rest of the animation and a new mode end again. Not ported
   (`docs/unported.md`).

## cltdo 111 (`0x103abce0`)

Same target, range and count, from cltmissilea. One seed roll first: when count > 1 and roll % 100 < 40 the line is
pushed count subtiles further. The rolls are the seed's new low dword taken as signed (`IDIV`, signed multiply by
the reciprocal), so a negative roll always passes both tests; the port keeps that. Per point a roll r: the flame is drawn when r % count < 2, with range calc1 + r % 2 - 1.
PD2 also writes a velocity (missiles.txt Vel at 1.13c +0x9a, - 6 + r % 8, halved on the first point when over 2) but
its flags (0x8020) leave out 4; D2R's client create (`0x1b7760`) reads params velocity only with flag 4, so the flame
keeps Vel (D2R +0xc0), as in PD2. After each flame PD2 calls `0x103a45c0` (a D2Common pointer) with calc1 and the
missile; D2R's Inferno client (cltdo 24, `0x2203f0`) calls `0x34da60 (missile, MsAnimSpeed << 8 / calc1)` at the same
place, and the port does that. The ending matches cltdo 24 under state 12 (inferno): rewind the frame by 2, or set
+0x4c / +0x64; D2R does it only for other players, PD2 for every caster; the port keeps D2R's check.

## The channel take-over (gate A)

[plrmodes.md](plrmodes.md) put PD2's gate A on D2R's `0x429d10`. That function is an entry of D2R's mode table
(`0x141d18fb8`, stride 0x18) that requests neutral with no skill. D2R's gate is `0x42bfb0 (game, unit, requested
mode, the request's unit skill or null, flag)`: 0 allow, 1 refuse, 4 buffer (D2R's queued input). It switches on the
current mode (+0xc) like PD2's, and hands a busy player (modes 7, 8, 10-16, 18) to `0x42d840` with the same arguments,
which holds D2R's frame windows, `seqinput`, Concentration and bear rolls. `0x42de40` (the queued-command mode entry)
calls `0x42d840` with the queued skill, `0x42ced0` with none.

PD2's gate A (`0x10373290`), after its uninterruptable (state 54), dead and "flag 0 allows" steps, reads the current
skill's srvdofunc: 182 allows any request whose skill has a record; 48 refuses a request whose skill's srvdofunc is 48
too and allows any other. core-rules hooks `0x42d840` (entry `48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18`) and
applies those two rules when the request carries a skill, the requested mode is not death or dead and the player is
not uninterruptable; everything else goes to D2R's code. The skill id comes from `0x33e080`, srvdofunc from skills
+0x4e. Not covered: D2R's `flag` is not PD2's (it lets a neutral or walk request through `0x42bfb0` only), so PD2's
"flag 0 allows" step has no place here, and the client's own input handling is left as D2R has it.

## Game check

`channel-cast`, `channel-client`, `channel-take-over` and `blade-fury-refused` (tag core-rules-w21). Not run in game
yet.
