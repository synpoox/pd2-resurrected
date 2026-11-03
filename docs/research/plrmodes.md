# What PD2's PlrModes.cpp changes

Answers work item W4 (core-rules Q6, the PlrModes part). Read from the PD2 baseline install `C:\Games\PD2\ProjectD2`:
`ProjectDiablo.dll` (sha256 `2ff6c327…60bbfb`, same as `Live\`) and its 1.13c `D2Game.dll` (sha256 `24b293b8…14f53`,
base `0x6fc20000`). Static disassembly only, nothing run. D2R side from the 93847 dump in `C:\ghidra-work` (Ghidra
decompile). Addresses below are D2Game RVAs and ProjectDiablo VAs.

## How PD2 hooks it

`ProjectDiablo.dll` keeps a patch table of 20-byte records `{dll, offset, target, type, len}` (dll 3 = D2Game, type 1 =
rewrite a `call` rel32). Function calls go through per-version stubs (`[version*4 + table]`, version 0 = 1.13c). Vanilla
`PlrModes.cpp` code sits at D2Game `0x77800-0x79900` (its 9 assert references span `0x778b9-0x79881`). Eight PD2 patches land
there. Only one PD2 function names `PlrModes.cpp` in an assert, so string search alone finds one of them.

| Call site (D2Game) | Vanilla callee | PD2 target | What it is | Decoded |
|---|---|---|---|---|
| `0x784ba`, `0x7859e` | `0x77fa0` | `0x10373290` (via stub `0x103a41b0`) | Gate A: can the current action be taken over | yes |
| `0x7849b`, `0x78583` | `0x78340` | `0x1037bb20` (via stub `0x103a4470`) | Gate B: can the new mode start from the current mode | yes |
| `0x77cba` | D2Common GetUnitStat | `0x102fc000` | life regen read (`hpregen`, stat 74) | yes |
| `0x798fd` | `0x79730` (with mode 17, dead) | `0x1036fcd0` | player death handler, large | no |
| `0x7980f` | `0x54320` | `0x103a19b0` -> `0x1036e950` | in the death path | no |
| `0x77ef7` | `0xf02a0` | `0x103a44b0` -> `0x10374020` | also patched at `0x7ad17`, `0x7ad2a` | no |

Both gates run one after the other in the two player set-mode paths (the code around `0x784ba` and the function at
`0x78550`); a 0 return from either drops the mode change.

## Gate A (`0x77fa0` -> `0x10373290`)

Arguments: game, player, requested mode, a flag, the skill the new mode will use. Unchanged parts: refuse if the player is
`uninterruptable` (state 54), dead or dying; allow if the request is death or the flag is 0; a skill flagged in skills.txt
(dword +4 bit 31, read here as `interrupt`; inferred, not confirmed) takes the `concentrate` (state 15) / `concentration`
(state 42, roll against `skill_concentration`, stat 164) path; otherwise a neutral player is put through `0x78550` and
allowed, and a busy one is allowed only for listed modes that pass the frame check `0x77e00`.

PD2's changes, keyed on the current skill's `srvdofunc`:

1. **Inferno and Arctic Blast (182)**: any request that carries a skill is allowed at once. New in PD2 (182 is PD2's own
   function number).
2. **Blade Fury (48)**: a request with a different skill is allowed at once; another Blade Fury request is refused, so
   the channel keeps going. New.
3. **Charge (PD2's 180)** joins Charge (67) and Whirlwind / Blade Dance (76) in the "same skill again is allowed" rule, and
   the rule now also allows requests with no skill (vanilla: same skill only).
4. **Run (mode 3)** is added to the modes a busy player may switch to when the frame check passes (vanilla: attack 1/2,
   cast, throw, skill 1, sequence).
5. **Concentrate / Concentration**: when the roll keeps a busy player uninterrupted, vanilla refuses every request; PD2
   still allows the listed modes when the frame check passes. Net: Concentration blocks get-hit, not your next attack.

## Gate B (`0x78340` -> `0x1037bb20`)

Arguments: player, game, requested mode. Requests for death, neutral, town neutral and dead are always allowed in both.
Otherwise the result depends on the current mode:

| Current mode | Vanilla | PD2 |
|---|---|---|
| 0 death, 17 dead | refuse | refuse |
| 1 neutral, 3 run | allow | cast (10) or sequence (18) refused until 5 frames after a timestamp at player data `+0x16c`; the rest allowed |
| 4 get-hit, 9 block | refuse | allowed in the last 2 frames before the mode's end event |
| 7, 8, 10, 11 attack, cast, throw | frame check, else only get-hit/block | same |
| 12 kick, 13 skill 1, 15 skill 3 | frame check; class not Amazon; class not Druid | same |
| 18 sequence | allowed if the skill has `seqinput`, else frame check | same check, PD2 reads its own copy of the skills table |
| 2, 5, 6, 14, 16 | allow | allow |

The end frame is the unit's earliest type-1 timer event (`0x8e490`, the mode-end event). The vanilla check is
`game frame <= end + 5`; PD2's inline checks are `game frame + N >= end`. Which window a player feels needs a game check.

## Life regen (`0x77cba` -> `0x102fc000`)

PD2 wraps the `hpregen` read in the player regen tick: when `0x1037c820` is true for the player's level (a level-id test
through `pPath->pRoom->...->pLevel+0x1d0`, not decoded further) and the value is over 30, it returns 30, or the
`bloodwarp` (state 205) stat list's value when that state is on. A level-based regen cap, most likely for maps.

## D2R 93847

D2R has its own `PlrModes.cpp` (`D2Game\src\Player\PlrModes.cpp` asserts). Its mode change (`0x429d10`) is a rewrite:
it switches on the current mode, reads skill flags at txt `+0x24`/`+0x27`/`+0x36` instead of `srvdofunc` numbers, has
input windows of 2 and 6 frames, and adds a `bear` (state 140) roll next to Concentration. So D2R is not 1.13c here, and
PD2's patches can't be carried over as code.

## What this means for the port

- Each change above is a `verify` row: first check in D2R whether it already behaves as PD2 (D2R's buffered input may
  already cover 4 and part of gate B), then hook `0x429d10` only for what differs. Rows added to core-rules.
- 1-3 key on PD2's function numbers (180, 182), so they wait for the skills that use them (P2 remaps ids; W8-W10 cover
  Charge's group). Whirlwind/Blade Dance (76) and Blade Fury (48) are vanilla numbers.
- The regen cap is part of the map system (Q7, `map plan`).
- Not decoded: the death handler and the two other hooks. They need their own read before any death-rule row is listed.
