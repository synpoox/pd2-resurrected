# Gate B (W34): the get-hit/block end window and the cast stamp

Answers work item W34 (#120). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll` in the
Ghidra project `PD2.gpr`, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are D2R RVAs unless they
start with `0x10` or `0x6f`.

## PD2's gate B (`0x1037bb20`)

`(player, game, requested mode)`, 1 allow, 0 refuse. A unit that is not a player goes to vanilla (`0x103a42b0`).
Requests for death (0), dead (17), neutral (1) and town neutral (5) are always allowed. Otherwise it switches on the
current mode (table `0x1037bc98`, targets `0x1037bc74`; a mode over 18 is allowed):

| Current mode | PD2 |
|---|---|
| 0 death, 17 dead | refuse |
| 1 neutral, 3 run | a cast (10) or sequence (18) request is refused while `[PlayerData + 0x16c] + 5 > game frame`; the rest allowed |
| 4 get-hit, 9 block | allow when `game frame + 2 >= end`, end = the earliest type-1 (mode-end) timer (`0x103a4290`); any requested mode |
| 2, 5, 6, 14, 16 | allow |
| the rest | as [plrmodes](plrmodes.md) |

## The cast stamp at PlayerData + 0x16c

Every `+0x160`-`+0x174` operand in `ProjectDiablo.dll` that is not on the stack was listed (FindDispRange). The only
store to a player data `+0x16c` is `0x102925a0`, and it is client code:

- it reads the client's own player (`[[0x105d6c78]]`, a unit of type 0) and its player data (`+0x14`);
- it stores `GetTickCount() + 250` (milliseconds, not game frames) at `+0x16c`, counts calls in `+0x174`, and sends a
  `0x5b` message (`0x103a1cd0` -> `[0x105e083c]`) with 2 on the first call and 0 when a 250 ms window passes 200
  calls; then it calls the hooked function (`0x10325280`). It has no static caller: PD2 installs it as a hook.

The server's player data is a different block from the client's (1.13c's single player keeps separate server and
client units), and no store to it was found in `ProjectDiablo.dll`, `PD2_EXT.dll` or `D2Game.dll`
([necromancer-w14](necromancer-w14.md)). So on the server gate B compares the game frame with a field nothing
writes: a zeroed block reads 0, and `0 + 5 > frame` only holds in a game's first 5 frames. By this reading
PD2's single-player server never delays a cast from neutral or run; no runtime read of PD2 confirms it. Its online servers run their own build, which this install does not
ship, so a writer there can't be ruled out. 1.13c's own player data ends at `+0x168`; the server allocation size in
`D2Game.dll` was not found, so whether `+0x16c` lies inside the block is not settled. A read past the block would
give the neighbouring heap bytes, and a large value there would refuse every cast, which players would have noticed.

D2R allows every request from neutral (`0x42bfb0`, case 1 and 5), so it already does what PD2's single-player server
does. Nothing is ported for this half.

## D2R's get-hit/block gate (`0x42d700`)

`0x42bfb0 (game, unit, mode, unit skill, flag)` hands current modes 4, 9 and 19 (knockback) to `0x42d700 (game, unit,
requested mode, flag byte)`. A flag that is not 0 only reaches it for a neutral request (`0x42bfb0` refuses the rest
first) and is allowed. With flag 0:

- requested 0 or 17: allowed;
- requested 4: refused (except one knockback case, `0x343240` on the unit's `0x34ae80` returning 0), 9 and 19 refused;
- requested 2, 3 or 6 (walk, run, town walk): buffered (4) while `end - frame > 1` (`cmp eax,1` at `0x42d7d0`);
- anything else: refused while `end - frame > 1` (`cmp eax,1` at `0x42d806`);
- refused outright when the unit is in knockback (`0x34ab60` is 19).

end is `0x48b230 (game, unit, 1, 0)`, the earliest type-1 timer, 0 when there is none. So D2R opens the window one
frame later than PD2: PD2 allows at `end - frame <= 2`, D2R at `<= 1`.

`0x429d10`, the path the issue named, is the mode-table entry for a neutral request, not the gate
([druid-w21](druid-w21.md)); its jump table at `0x42a2ca` was not read again here.

## core-rules

The install writes 2 over both immediates (`0x42d7d2`, `0x42d808`) and hooks `0x42d700` only to log the result per
frames left (`hit window: mode m request r, n frames left: result`). What stays D2R's (approximated,
docs/deviations.md):

- a get-hit, block or knockback request in get-hit/block is refused; PD2 allows it in the window;
- a walk or run request outside the window is buffered (D2R's queue) where PD2 refuses it;
- a neutral (1) or town-neutral (5) request with flag 0 is refused outside the window; PD2 always allows it;
- D2R's client holds a request made in get-hit and sends it when its own window opens: every request the server
  saw came at 1 frame left (below). So in single player the 2-frame window is not reached. PD2's own client most
  likely does the same (below), so this is listed but not counted as a deviation.
  The client side was not found. What is ruled out:
  - the server does not hold the request: temporary logs on `0x42d020` (StartMode, entry `4c 8b dc 4d 89 43 18 53
    55 56 57 41 54 41 55`) saw every request made in get-hit come from the skill message handler (`0x4f8de0`, return
    `0x4f8f66`) with 1 frame left, so the message itself arrives then. `0x4fdca0`, which `0x42d020` calls first, is
    skill validation (its `0x4249f0` test is not a mode test), not a queue;
  - the client's held command (`D2Client\src\Unit\UnitMode.cpp`; stored in the client player data `+0x1c8`-`+0x1d4`
    by `0xf9680`, `0xfbef0` and the click handlers such as `0x1017b0`, released by `0xf9bc0` every client frame
    from `0x8b3c0` and `0x94b40`) is the "move, then act" follow-up and has no frame test;
  - the sender `0xfabe0` skips only for a skill flag (`0xffac0`), not for get-hit.
  So the hold comes from the client's own player mode: the local get-hit animation ending, one frame before the
  server's mode end.

  PD2's client most likely holds the same way. 1.13c's `D2Client.dll` has the same `..\Source\D2Client\UNIT\UnitMode.cpp`:
  `0x2a7c0` is D2R's `0xf9bc0`/`0xfabe0` pair, with the held command at player data `+0x150`-`+0x15c` (D2R
  `+0x1c8`-`+0x1d4`), and `0x29f30` lets a skill through only from neutral or town neutral. A scan for code
  comparing one value with both 4 and 9 within six instructions finds no get-hit/block test in 1.13c's client
  (`0x69660` walks a skill list) or in `ProjectDiablo.dll` (only gate B and `0x102fcb40`, an item count fill).
  So PD2 did not patch its client here, and its 2-frame server window is most likely not reached from a player's
  input there either: the client hold is not a deviation. Not confirmed in a running PD2, and `PD2_EXT.dll` is not
  in the Ghidra project, so it was not scanned.

## Game check

`hit-window` (tag core-rules-w34). Run on Slot8 (2026-11-04, Hell, level 95 Assassin, god mode, Blood Moor, six Venom
Lords (`megademon3`) and eight zombies, attacking and casting while they hit): all three checks passed (again on Slot6 after W19 landed: level 90 Paladin, 60/60 parts installed); the window
lines read `1 frames left: 0` (attack, skill 4 and sequence requests). A temporary log on `0x42bfb0` saw five
requests while the player was in get-hit, every one at 1 frame left, none earlier: the client holds them.
A request at 1 frame left is allowed with or without the patch, so the check shows the hook and the window's results,
not the 2-frame patch itself, which the install checks by its bytes. The cast delay has no game check: nothing is
ported for it.
