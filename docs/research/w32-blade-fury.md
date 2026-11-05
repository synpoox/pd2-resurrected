# Blade Fury's fragment ([W32](https://github.com/synpoox/pd2-resurrected/issues/107)): shit 34 -> 69, chit 6 -> 68, dmg 19, and the channel check

Answers work item [W32](https://github.com/synpoox/pd2-resurrected/issues/107) ([#107](https://github.com/synpoox/pd2-resurrected/issues/107)). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll`,
`D2Client.dll` and `D2Common.dll` in the Ghidra project `PD2.gpr`, D2R side from the 93847 dump (`ImageStamp
0x6ab3782c`). Addresses are D2R RVAs unless they start with `0x10`; 1.13c addresses are offsets in their DLL.

PD2's missile installs write srvhit 34 = `0x103a7c70` (`0x103a9170`, table `+0x88`), clthit 6 = `0x103a6530`
(`0x103a6c50`, `+0x18`) and srvdmg 19 = `0x103a7130` (`0x103a7220`, `+0x4c`). D2R fills srvhit 34 (echoingstrike);
clthit 6 is below D2R's free range (66 on), so the merge moves them to 69 and 68 (`tools/pd2_ids.py` FUNCS). dmg 19
is free.

Row bladefragment1 (Blade Fury's HitSubMissile1, and its own): Param1 50, sHitPar1-3 3 / 5 / 2, cHitPar1-3 10 / 5 /
3, dParam1-2 20 / 75, Size 1, CollideKill, Pierce, ClientSend; HitSubMissile1 and CltHitSubMissile1 itself.

## The calls PD2 makes

ProjectDiablo.dll reaches 1.13c through lazy thunks ([druid-w23](druid-w23.md)).

| PD2 call | 1.13c | What it is | D2R |
|---|---|---|---|
| `0x1030b7b0` / `0x1030b6c0` | D2Common #11018 / #10637 | missile data `+0x28` get / set: the fragment's generation | MissileBounces `0x3bc0a0` / SetMissileBounces `0x3bd8a0` (data `+0x30`); PD2's srvdo 139 sets the same field through `0x1030b6c0` |
| `0x10309590` / `0x10309560` | #10737 / #10970 | data `+0x10`: frames left | MissileSrvFrames `0x3bb1e0` / `0x3bd450` (float `+0x14`, frames * 40) |
| `0x103095f0` | #10130 | data `+0x08`, the activate frame | `0x3bae80` (`+0x0a`) |
| `0x10309500` | #10534 | data `+0x0e`, the range | MissileCltFrames' `+0x10` (frames run = range - left) |
| `0x10309650` | #10331 | the unit's room | UnitRoom `0x34b440` |
| `0x103097d0` | #10573 `(room, x, y, size, mask)` | collision at a subtile with the pattern of a size: 0-1 the subtile (`0x4c570`), 2 a plus of five (`0x4c5c0`), 3 a box (`0x4c530`) | `0x366250 (room, x, y, mask)`, the single subtile |
| path `+0x44` | | the unit's size | `0x34b580` (a missile's row Size `+0x1b2`, a player 2) |
| `0x10311b20` / `0x10328ee0` | D2Game `0x6f930` / D2Client `0xb30f0` | missile create | `0x5371a0` / `0x1b7760` |
| `0x103a5470` / `0x103a2ad0` | | missile owner, server / client | `0x490300` / `0x9a7b0` |
| `[0x105e1918]`, `[0x105e1974]` (and the client's copies) | D2Game `0xfb870`, `0xfb770` | D2's 64 directions, radius 30: x = cos, y = sin | DirectionX `0x1d1b650`, DirectionY `0x1d1b750`, the same values |
| `0x1037fe90 (missile, game)` | D2Game `0x7b0a0 (client, unit)` per player | sends the missile to every player's client in the game (`+0x1120` hash, player data `+0x9c` client in state 4) | not found |

Constants: `0x1044fa48` 1.0, `0x1044fbd0` 64 / 2π, `0x1044fc9c` 64.0. Create flags `0x22`: from the origin unit (the
fragment), aimed at an offset (D2R's create reads them the same way).

## shit 34 (`0x103a7c70`)

`(game, missile, hit)`. Needs the game, the missile, a valid row and an owner, else 0. n = sHitPar1, step = sHitPar2,
drop = sHitPar3 (each 1 when empty). While the generation is under cHitPar3:

- **A wall** (no unit): only when frames left <= the activate frame, >= 2 and < the range, the fragment's own subtile
  collides (mask `0x806`, its size + 1) and it has a path. n = owner -> fragment, normalised. For each of n fragments,
  offsets -((n - 1) / 2) * step, step apart: n reflected off normal (1, 0) first (x negated, a
  vertical wall), else off (0, 1) (y negated) (atan2 in D2's 64 directions, + the
  offset); the first mirror whose point `size` subtiles ahead is free gives a HitSubMissile1 fragment aimed at that
  direction's table offset, with generation + 1 and frames max(left - drop, 0), at least cHitPar1 on generation 0,
  0 under cHitPar2. Then the parent's frames are set to 0. Returns 0.
- **A unit**: one `rand()`. When its `% 100` is under min(Param1, 100), n - 1 fragments at directions (`rand & 63`) +
  offsets from -((n - 2) / 2) * step, the value growing by 0x11 per fragment; each with generation + 1, frames
  max(left - drop, 0) (0 under cHitPar2), and sent to every client.

Returns 3 (the hit's damage) after a unit hit or at cHitPar3. With PD2's row: up to 3 generations, a wall makes 3
fragments 5 directions apart, a unit hit 2 at 50%.

## clthit 6 (`0x103a6530`)

The wall half only, on the client: the same tests with the client owner, CltHitSubMissile1 and the client create.
Returns 1. So each client draws the wall bounces itself, while the unit-hit fragments reach it only from the server's
send.

## srvdmg 19 (`0x103a7130`)

`(game, missile, target, damage)`, for a missile: life, mana and stamina leech (1.13c `+0x38-0x40`, D2R `+0x120-0x128`)
times dParam2 / 100 (unsigned). Then a `rand() % 100` over dParam1 sets result flag 0x20 (`+0x04`) and, on a fragment
whose generation is not 0, zeroes the leech.

## The PD2-origin bug

Both halves take a direction as a signed `% 64` (`and 0x8000003f` with the sign fix): a negative index reads before
the tables. 1.13c keeps the y table right before the x table, so x reads y's tail (-2 to -14) and y reads the bytes
before it (0, -1, 1, 1, -1): the fragment leaves roughly west where directions 59-63 point east. With PD2's row a wall
bounce's first fragment and a split's are offset -5; it happens whenever the mirrored direction is under 5. core-rules
wraps the index (docs/pd2-origin-bugs.md).

## core-rules

`SrvHitBladeFragment` (shit 69), `CltHitBladeFragment` (chit 68) and `SrvDmgBladeFragment` (dmg 19) in
`plugin/core-rules/missile_functions.cpp`; the wall bounce is one template both halves run. Deviations (approximated,
docs/deviations.md):

- The split fragments are created on the server only: D2R's send of a missile to a client is not found (1.13c's room
  entry, D2Game `0x9dcd0`, never sends a missile, so the send must be explicit). They hit; another client draws them
  only if D2R syncs them.
- The wall test uses 1.13c's single subtile and plus patterns (sizes 0-2); bladefragment1 is size 1.
- Two guards PD2 lacks: a zero owner-to-fragment length (PD2 divides by it) and an invalid sub-missile row skip the
  bounce.
- The rolls use the D2 seed (owner's for the split, the fragment's for dmg 19) instead of `rand()`.

## The channel check

[W21](https://github.com/synpoox/pd2-resurrected/issues/96) built PD2's gate A for Blade Fury (srvdo 48): another skill takes over at once, a second Blade Fury does not cut
the first short ([druid-w21](druid-w21.md)). The game check found the held channel throwing one blade per hold, on
release, and the client drawing none: PD2's seqinput 3 makes D2R loop the sequence at frame 3, before the throw frame,
and D2R's gate buffers (4) the held re-request, so the Assassin stays in the channel. It is [W21](https://github.com/synpoox/pd2-resurrected/issues/96)'s Inferno and Arctic
Blast finding again (PD2's event re-timing, `0x1037a7c0`, is not ported). The row keeps D2R's seqinput 12
(`merge/standins.tsv`): the re-request then comes after the throw, D2R's gate allows it (0) and the next sequence
throws the next blade, at D2R's rate. A Blade Fury of another row is still refused by PD2's rule.
`blade-fury-refused` now expects the 0.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w32`: `blade-fragment-hit` (unit hits split at Param1 with 2 of 2
fragments, frames from the parent's), `blade-fragment-wall` (3 fragments on the server and the client, generation +1),
`blade-fragment-damage` (leech kept at dParam2 75%), `blade-fury-refused`, and the slots in `core-rules-loads`.

Run on Slot8 (2026-11-04, Hell, level 95 Assassin, Blade Fury 20 by `cheat sklvl`, god mode, Den of Evil): all six
passed. A wall bounce made 3 of 3 fragments on the server and the client (generation 0 -> 1); a zombie hit split into 2
of 2 fragments with 25 frames; a generation-2 fragment's damage kept 75% of the leech, rolled over dParam1 and dropped
it; held re-requests came back 0; blades show on screen. Split fragments on another client were not checked (one
player).
