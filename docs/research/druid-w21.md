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

[plrmodes.md](plrmodes.md) puts PD2's gate A on D2R's `0x429d10`. That function is an entry of D2R's mode table
(`0x141d18fb8`, stride 0x18) that, for one case, requests neutral with no skill; the switch on the current mode inside
it is D2R's gate, inlined. The path a skill request takes (queued commands, `0x4fdef0` called from `0x4fdb40`) is not
traced yet, so the take-over and the Blade Fury refusal stay `verify` / not yet: the next step is to find the function
that runs the same switch for a request carrying a skill, then check in game whether D2R already lets a skill start
mid-channel.

## Game check

`channel-cast` and `channel-client` (tag core-rules-w21). Not run in game yet.
