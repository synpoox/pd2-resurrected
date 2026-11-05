# Firestorm and the wall makers (W22): srvdo 166, cltdo 119

Answers work item W22 (#97). PD2 side from the baseline install `C:\Games\PD2\ProjectD2` (`ProjectDiablo.dll`, base
0x10000000; its table fill `0x1036ba20` writes srvdo 166 at `[0x105de8fc] + 0x298` and cltdo 119 at
`[0x105de71c] + 0x1dc`), D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). D2R fills srvdo 166 (`0x522e70`), so
the server half joins the router (190); D2R's cltdo 119 is empty, so the client half keeps PD2's number.

PD2's rows: Firestorm (srvdo 166, cltdo 119), DiabWall, UberDiabWall, FingermageBossDiabWall, KanemithFirestorm (srvdo
166, cltdo 58, D2R's own). calc1 is the maker count (Firestorm and KanemithFirestorm `par1+(blvl/5)`, par1 3 and 5;
DiabWall and UberDiabWall `lvl`; FingermageBossDiabWall 3); srvmissilea the maker. The stand-ins were D2R's Firestorm
(srvdo 117, cltdo 67) and DiabWall (srvdo 102).

## srvdo 166 (`0x103b29d0`)

1. Return 0 without a skill record or a valid srvmissilea (1.13c skills +0x48).
2. Set the caster's skill flag (+0xc4 |= 0x40; D2R's `0x34e790 (unit, 1)`).
3. count = calc1 (1.13c +0x138).
4. Params (1.13c layout): flags 0x21, owner the caster, missile srvmissilea, start x/y the caster's position, skill
   and level, callback `0x1036f220` with the caster as its argument; target from the skill's target point (return 0
   without one).
5. count makers, each with the same params. Return 1.

## cltdo 119 (`0x103acb70`)

The same steps on the client (client skill target `0x103a15e0`, client create), the same srvmissilea column and
callback. No call after the create (D2R's cltdo 67 sets each maker's anim range to 0x200).

## The maker callback

PD2's `0x1036f220 (missile, caster)`: the maker's frames capped at 77 (0x4d); the caster's counter at +0xcc bumped
(`0x103805f0`: high 12 bits a generation, low 20 a roll) and written to the maker's seed (+0x20); then a type 10 path
of that many frames on the maker.

D2R replaced create callbacks with an index (params +0x70) and an argument (+0x74), run by the create (`0x3bac90`) from
the table at `0x2381f30`. Index 1 (`0x3b75c0`, D2R's Firestorm) is PD2's callback but for the seed: it seeds the maker
from the argument plus a path value. Index 7 (`0x3b7f70`, DiabWall) is the same and then skips the path one time in
five. D2R's Firestorm (`0x5764e0` -> `0x5769f0`) also sends its first maker through a plain create, without the
callback, and the rest with callback 1 and their index; DiabWall (`0x578bb0`) sends all with callback 7.

core-rules builds PD2's loop with callback 1 and the next value of one game-wide counter as each maker's argument, so
every maker gets a different seed on each cast, as PD2's counter gives; the values are not PD2's (`docs/deviations.md`).

## Game check

`wall-cast` and `wall-client` (tag core-rules-w22): a Firestorm cast logs calc1 makers (3 + blvl / 5), all created,
and the client the same count. Both passed on Slot8 (2026-11-04, a level 95 Druid, Firestorm base 10 with +1 from gear,
out of town in the Blood Moor): 5 of 5 makers on the server and 5 of 5 on the client; the flames show on screen. No
crash. The wall makers (monster rows) were not cast in game.
