# Fire Claws and the Spirit Wolf start ([W24](https://github.com/synpoox/pd2-resurrected/issues/99)): srvdo 175, cltdo 110, srvst 77

Answers work item [W24](https://github.com/synpoox/pd2-resurrected/issues/99) ([#99](https://github.com/synpoox/pd2-resurrected/issues/99)). PD2 side from the baseline install `C:\Games\PD2\ProjectD2` (`ProjectDiablo.dll`, base
0x10000000; its table fill `0x1036ba20` writes srvdo 175 at `[0x105de8fc] + 0x2bc`, cltdo 110 at `[0x105de71c] + 0x1b8`
and srvst 77 at `[0x105de8ec] + 0x134`), D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). D2R leaves all three
slots empty, so each keeps PD2's number.

PD2's rows: Fire Claws (239: srvst 79, srvdo 175, cltst 9, cltdo 110; srvmissilea and cltmissilea
`clawfirestormmaker`; calc2 `par1 + (blvl / 5) * par2`, par1 4, par2 1; EType fire; aurastate `temp_skill_buff` with
aurastat1/2 `firemindam`/`firemaxdam` = edmn/edmx; restrict 2 with State1 wolf, State2 bear, so it is cast in Werewolf
or Werebear form only). Summon Spirit Wolf (227) and Summon Fenris (237): srvst 77, srvdo 119 (D2R's), pettype
spiritwolf (11) and fenris (12); Spirit Wolf's petmax
`(stat('no_wolves'.accr) > 0) ? 0 : "min(lvl,par3)" + stat('extra_spiritwolf'.accr)`, par3 6.

## srvdo 175 (`0x103b3ad0`)

1. Return 0 without a skill record or the skill's target unit (`0x103096b0`).
2. A melee hit on it, the finisher hit's pattern ([w29](w29-assassin-finishers.md)): to-hit from `0x1030b960` plus
   stat 325 (progressive_tohit, `0x10307e50`), roll `0x103048a0`, result |= 2, fill `0x1031b4a0` (0x80), queue
   `0x103a1630`, apply (D2Game `0x10310eb0`).
3. Return 0 if srvmissilea (1.13c skills +0x48) is not a valid missile.
4. count = calc2 (1.13c +0x13c).
5. Params: flags 0x21, owner and start x/y the caster, the maker callback `0x1036f220` (Firestorm's,
   [druid-w22](druid-w22.md)); target from the skill target point (`0x103a15c0`, return 0 without one).
6. count makers; return 1.

The fire is not in this hit: PD2's start (srvst 79, built in [W2](https://github.com/synpoox/pd2-resurrected/issues/45)) puts it on the caster as temp_skill_buff's aurastats.

## cltdo 110 (`0x103abbd0`)

The same makers on the client from cltmissilea (1.13c +0xea), aimed at the target unit's path x/y, through the client
create. No skill flag.

## How core-rules builds Fire Claws

D2R's own Fire Claws start (srvst 58, `0x577c90`) rolls the claw hit on the target (`0x48fe20`, roll `0x437000`), adds
the skill's fire (ElementalDamage `0x437d00`) and queues it (`0x44b600`); D2R's srvdo 2 (`0x430240`) applies a queued
hit. D2R's generic melee start (`0x567070`) also sets the aurastate, so the fire would be counted twice with PD2's
start; the merged row keeps D2R's srvst 58 instead.

srvdo 175 applies the queued hit through srvdo 2 (as core-rules' Bash does), then sends PD2's calc2 makers through
[W22](https://github.com/synpoox/pd2-resurrected/issues/97)'s maker loop with callback 1 (D2R's Firestorm maker callback). cltdo 110 draws the same count. Approximated
(`docs/deviations.md`): the hit is rolled at the start frame, not the do frame; progressive_tohit is not added; the fire
comes from the skill's elemental damage, not from temp_skill_buff.

## srvst 77 (`0x103b6c80`)

Needs player data (D2Common #10920) with a pet table (+0x44). pettype = skill +0xbe; then rule `0x103747b0 (game,
unit, 1, pettype)`:

1. Pet types other than 11 and 12 refuse.
2. limit = `0x103806d0 (unit, 227 or 237)`: petmax of the type's skill at the unit's level in it with bonuses, 0
   without the skill.
3. entry = pets + type * 0xc: {first pet, count, max}. max <= 0 allows.
4. Otherwise, while there is a first pet: allow if count < limit, else kill it (`0x103a4660 (unit, type)`,
   `0x10319ac0 (game, unit, pet, 1)`).
5. With none left, limit 0 refuses.

A refusal calls `0x1036d1b0 (unit, 0x13, 0)`: unit +0x6e = 0x13, +0x70 = 0, D2Common #10158 (update), flag 0x400
(HASEVENTSOUND), the player's "I can't".

D2R's pets (PlayerPets.cpp): player data `0x34b240`, pet table +0x98; `0x5013f0 (dataFlag, table, type)` gives the
entry (stride 0x20: first pet +0, node id +4, next +0x18; count +8; max +0xc); `0x4ff1a0 (game, owner, type, 1)` the
first pet; `0x4ffd30 (game, owner, pet id, 1)` takes it off the list and kills it. D2R's srvdo 119 (`0x575990`) reads
pettype at skill +0x112 and petmax at +0x114 and summons at least one.

core-rules builds the rule with D2R's pet table, taking petmax from the cast skill at the cast level (the only rows
with these pet types are the types' own skills). Approximated (`docs/deviations.md`): a refusal is silent.

## Game check

Tag core-rules-w24, Slot8, Hell, god mode, Cold Plains, a level 95 Druid with Fire Claws 10, Werewolf 5, Spirit Wolf 5,
no +skills (2026-11-04):

- `fire-claws-hit`: in Werewolf form, a claw hit on a zombie logged result 5, fire 48, 6 of 6 makers (4 + 10/5); a
  miss logged result 0 and the same makers.
- `fire-claws-client`: 6 of 6 makers.
- `wolf-start`: petmax 5 (min(5, 6)); starts accepted; at 5 wolves the start killed 1 and was accepted; 5 wolves on
  screen.
- No crash. A refusal (petmax 0, `no_wolves`) and Fenris were not cast in game.
