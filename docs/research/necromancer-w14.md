# Necromancer W14: Golem start, Revive event, PlrModes gate B

Answers work item W14 (#89), research half. PD2 side from the baseline install `C:\Games\PD2\ProjectD2`
(`ProjectDiablo.dll`, VAs `0x10...`, and its 1.13c `D2Game.dll`, RVAs), static disassembly. D2R side from the 93847 dump
(`ImageStamp 0x6ab3782c`), RVAs. Merged ids from `generated/data` (pd2_merge on this branch).

## Where PD2 fills its tables

`ProjectDiablo.dll` fills D2Game's tables at start-up with plain stores through table pointers:

- srvstfunc: `[0x105de8ec]` (`0x1036bdba`): 27, 28, 29, 37, 66-79 (78 is `0x103b6cf0`, as W10 found).
- skill aura event functions: `[0x105de8f8]` (`0x1036c2b0`): 3, 6, 7, 15, 16, 19, 22-25, 27, 32, 33.

## Golem start (PD2 srvst 74, `0x103b69d0`)

`(game, unit, skill, level)`: refuse unless the unit has a skill record with `+0x44` set; then `0x103746c0(game, unit, 1)`;
on 0 it sends the "can't" message (`0x1036d1b0(unit, 0x13, 0)`) and refuses the start.

`0x103746c0` (the golem count rule), on PD2's pet types (golem 28, irongolem 3):

1. limit = max of petmax(Clay Golem 75), petmax(Blood Golem 85), petmax(Fire Golem 94) (`0x103806d0(unit, skill)`);
   -1 (none) falls back to the golem pet list's own max.
2. limit -= the number of Iron Golems (pet type 3) the player has.
3. While the player has golems (type 28) and their count >= limit, kill the oldest (`0x103a4660(unit, 28)`,
   `0x10319ac0(game, unit, pet, 1)`).
4. Refuse when limit is 0, else allow.

PD2's petmax: `((lvl>0)?1:0) + (skill('Golem Mastery'.blvl) / 5) + stat('extra_golem'.accr)` for all three; Iron Golem
`(lvl>0)?1:0`. So Golem Mastery adds a golem every 5 base levels, an Iron Golem takes one of the places, and a
different elemental golem replaces the oldest one instead of all of them.

Merged ids: pet type golem = **3** (D2R's own row, takes PD2's values), irongolem = **22** (appended). D2R's srvstfunc
table has 91 slots, 74 is empty. The golem rows' srvstfunc is a stand-in (blank) until this lands.

D2R: summon is `0x4350e0` (enforces petmax per pet type); golem do (srvdo 56) is `0x55c080`.

## Revive aura event (PD2 event 32, `0x10347370`)

`(game, eventId, unit, target, ...)`: when unit and target are set, cast skill 358 (`proc_SplashDamage`) at level 1 from
the unit as an event (`0x103b2340`, the item-event cast). Revive's row has `auraevent1 domeleedamage`, so a revived
monster's melee hits splash.

D2R: the aura event function table is `0x238e5c0`, 41 slots (dispatcher `0x43823a`, bound `cmp eax, 0x28`; lookup
`0x438f70`, `cmp ecx, 0x29`). Every slot 1-40 is filled. Slot 32 is `0x42eea0`, a skill damage event that is **not**
PD2's; no D2R or merged row but Revive uses event 32, so core-rules can take slot 32. Golem events 23/26/27 are in
SkillNec code (`0x55b2d0`, `0x55b0c0`, `0x55b6a0`), Chilling Armor's 3 is `0x552a50`.

Merged id of `proc_SplashDamage`: **430** (PD2 358). Skills record: auraevent1-4 u16 `+0xa4..+0xaa`, auraeventfunc1-4
u16 `+0xac..+0xb2` (field table `0x302380`). The aura path copies both into the state list (`0x55c840`,
`0x55a7e0` -> `0x438370`).

Game check: D2R's Revive do (srvdo 58, `0x55e7e0`) never registers Revive's aura events on the revived monster; with
slot 32 filled, six revives fighting fallen never reached it. Golems get theirs elsewhere. The skill-event register
is srvst 66, `0x42fa90 (game, unit, skill, level)`: it reads auraevent1-4 / auraeventfunc1-4 of the skill and calls
`0x588670` then `0x438230 (game, unit, event, skill, level, ..., func, ...)` per event (the `0x438370` path does the
same for aura states). core-rules wraps srvdo 58 and calls it on the target after D2R's Revive: the corpse is revived
in place (same unit, return 1, mode neutral; the owner fields at `+0xe8` stay 0/0 for a pet). Then a revive's melee
hit fires event 5 (domeleedamage) into slot 32.

The event's cast (`0x5896e0` -> `0x589930`) of proc_SplashDamage returns 0: the skill has no do function yet (W33).
`0x589930` refuses a skill whose `ItemEffect` (`+0x20a`) is 0; proc_SplashDamage's is 1, so the refusal is later.

## Gate B (PD2 `0x1037bb20`)

Arguments `(player, game, mode)`. Decoded:

- Current mode get-hit (4) or block (9): allow when `game frame + 2 >= end`, end = the unit's earliest type-1 timer
  (`0x103a4290(unit, 1)`, the mode-end event); else refuse.
- Current mode neutral (1) or run (3), requested cast (10) or sequence (18): refuse while
  `[PlayerData + 0x16c] + 5 > game frame`.

The stamp: 1.13c PlayerData ends at `+0x168` (`dwGameFrame`, written by the walk/run message handler at D2Game
`0xcc00d`; D2MOO's layout agrees), so `+0x16c` is past the vanilla struct. No store to `+0x16c` on a player data pointer
was found in `ProjectDiablo.dll`, `PD2_EXT.dll` or `D2Game.dll` (byte scan for every `+0x16c` operand; the only stores
are a client-side tick stamp at `0x1029260a`, a data-table field at `0x1036c21f` and a list head in D2Game). PD2's patch
table is not stored as plain `{dll, offset, ...}` records, so a byte patch into D2Game could not be ruled out.

D2R's mode change `0x429d10` is a rewrite (see [plrmodes](plrmodes.md)). Current mode 4, 9 or 19 goes through a
jump table at `0x42a2ca` (index bytes `0x42a5c8`, targets `0x42a5b8`): requested 9 and 19 keep the refusal; 1 checks
the unit first (`0x42a2e8`); the rest fall to `0x42a309`. Which of these allow needs a game check.
