# Melee splash: the current skill's damage and radius extras (W35)

Answers work item W35 (#125), the rest of [w33-melee-splash](w33-melee-splash.md). PD2 side from `ProjectDiablo.dll`
(VAs `0x10...`) and the 1.13c `D2Game.dll` / `D2Common.dll` in PD2's Ghidra project; D2R side from the 93847 dump,
RVAs.

## PD2

PD2's thunks (`0x1030xxxx` that jump through `DAT_105exxxx`) resolve through per-version tables. Index 0 holds
`-ordinal` for D2Common (module 1), or an offset into D2Game (module 6).

### srvdo 163's block (`0x103b2340`)

For a player (`*unit == 0`) whose current skill (`0x1030dd00` = D2Common #10511: `unit->pSkills->+0x10`) is not the
splash, the block gets that skill's id, its level read as the skill's `+0x28`, which is its hard points, and its record's
fields:

| block | 1.13c skill record | D2R skill record |
|---|---|---|
| SrcDam | `+0x1a5` (byte; 0x80 with no skill) | `SkSrcDam` 0x24d |
| damage% | `0x10305eb0`: calc1 (`+0x138`) for 0, 10, 96, 97, 107, 126, 133, 144, 147, 151, 152, 232, 260, 378, 380; calc2 (`+0x13c`) for 30, 106, 248, 255, 265, 275; else 0 | `SkCalc1`, `SkCalc2` |
| hit flags | `+0x130` | `SkHitFlags` 0x188 |
| result flags | `+0x12e` | `SkResultFlags` 0x186 |
| hit class | `+0x134` | `SkHitClass` 0x18c |

With no such skill it is Attack (0) at level 1. PD2's ids 378 and 380 are Joust and Blade Dance (merged 450, 452).

### Radius

`calc3 + 0x10381720(unit, 478) / 20`, minus 1 when the weapon (`0x103309b0`, D2Game) has a record (`0x103072e0`)
whose `+0x80` (code) equals `+0x84` (normcode), that is, a normal-tier base.

`0x10381720` reads every layer of stat 478 (`inc_splash_radius`) from the unit's full stat list (D2Common #10702). It
skips layer 0. It adds a layer's value when the wielded weapon (#10061) is of the item type the layer names (#10744).
The masteries put their passive stats on their passiveitype's layer: One Hand Mastery on 1han, Two Hand Mastery on
2han. The 1han and 2han layers also check the other hand (#10417 body location, #11139 item at a body location,
#10747 can dual wield, that is, a Barbarian or an Assassin):

- 2han: counted only when the other hand is empty or holds the same item.
- 1han: counted for a 1han weapon; for a 2han weapon held by a dual-wield class, only when the other hand holds a
  different item.

### Room check (`0x1037c820`)

It refuses when the unit's level (`path->room->roomEx->level->+0x1d0`) is 157, 159 or 166: Pvp Map, Pvp Map 2 and BR
Arena (`0x1037c7b0`).

### Callback (`0x10303190`), per unit

1. Result flags = 1 (no to-hit roll) | the skill's; damage% and hit class from the block; hit flags = the skill's.
2. Smite (97) by a player: `0x103060c0`, the shield's damage (record `+0xfe`/`+0xff` × 256, plus state 101's stats)
   through D2Game `0x103a4480`, hit flags |= 2, hit class 0x65. The kicks (255, 270, 275): the first queued combat
   entry's damage (`[unit+0xac]+0x14`, 0x70 bytes) replaces the block. Every other skill clears hit flag 2.
3. Concentrate (144) with an EType: conversion type = EType (`+0x1dc`), percent = calc4 (`+0x144`).
4. D2Game `+0x9f210`: the skill's elemental damage. D2Game `+0x9f1a0`: its physical damage. Then D2Game `+0xddde0`:
   weapon damage at SrcDam.
5. Life leech / 2 (at least 1). A player victim's data `+0x194` = the skill id. Apply: `0x10305880`, with flag 0.

The 1.13c physical fill `0x9f1a0` adds `min + rand(max - min)` (D2Common #10567 / #10297 with `(unit, skill, level,
0)`, the unit's seed at `+0x20`) to the damage's `+0x08`.

### Handlers skipped (`0x10305c30` -> walker `0x10347510`)

The apply passes flag 0, so the walker gets the handler set `0x105dc628` (built by `0x10111890`). It holds item event
14 (`item_freeze`), item event 20 (skill on hit: `item_skillonhit`, `item_skillonattack`, `item_splashonhit`,
`map_mon_splash`) and Revive's event 32. For events 21 and 30 the walker skips only procs of proc_SplashDamage (PD2
358, set `0x105dc5c8`). Those would be a splash inside a splash, which core-rules already refuses.

## D2R

- Current skill: `CurrentSkill` (0x34ba40), `UnitSkillId`, `UnitSkillLevel` (0x33d1e0, bonus 0 = hard points; the
  srvdo dispatcher 0x43acb0 calls it with 1).
- Elemental fill: `ElementalDamage` (0x437d00), as D2R's melee start (srvst 32, 0x567070) uses it.
- Physical fill: no D2R function adds it alone. core-rules adds `SkillPhysMin` (0x3385b0) + a roll below
  `SkillPhysMax` (0x338160) - min, on `UnitSeed`, to `DmgPhys`. Those are the min/max pair D2R's kick fill (0x573af0)
  reads.
- Concentrate: D2R's melee start sets the same conversion for a skill with an EType: type at +0x149 and calc4 at
  +0x14c (1.13c's +0x65 / +0x68).
- Kicks: `QueuedHit(attacker, struck)`; core-rules copies the block's plain fields (+0x00-0x40, +0x118-0x158), not its
  list.
- Radius: `UnitWeapon`, `ItemIsType` for each itemtypes row (count at DataTables +0x1350, ItemIsType's bound) and
  `UnitStat(unit, inc_splash_radius, type)`. Normal base: `ItemRecord` (0x314110) code +0x80 vs normcode +0x84 (items
  field table in 0x315fd0).
- Skipped handlers: nothing to port. Item event functions 14 (0x583580) and 20 (0x583b30) are reached only through
  `AuraEventTable` slots 14 and 20 (Ghidra's other reference to 0x583b30 is `.pdata`). The splash itself runs inside
  slot 20 (item_splashonhit), and its hits' event 5 never reaches slot 14 or 20: a test build wrapped both slots,
  and over Frenzy and attack splashes with Bladebone (40% Teeth on striking) the wrappers never ran during a splash.
  The walker (`0x5881e0`) jumps outside the dump, so whether it also drops other domeleedamage handlers on splashed
  units (crushing blow, open wounds, which PD2 fires) is not known.

## Deviations (approximated, on the deviation list)

- Smite's splash takes Smite's own skill damage. PD2 deals its shield damage and adds a state 224 list with
  inc_splash_radius = skill `+0x158`.
- The radius counts the layers of the weapon's own item types. PD2's off-hand rules for 1han and 2han are left out:
  D2R's item-at-body-location (0x3885e0) treats weapons in slots 5 and 6 specially and is not decoded.
- A player victim's last-hit skill (1.13c's `+0x194`) is not written.
- The room check is left out: it refuses only the PvP maps, which are cut, as for W9's bounce.

## Checks

`tests/game/core_rules_checks.py`, tag `core-rules-w35`: `splash-radius-extras` (radius = calc3 + weapon types / 20
- normal base), `splash-current-skill` (a Frenzy splash at hard points with damage% > 0, a splashed unit's physical
damage > 0). Tag `core-rules-w33`'s `melee-splash` reads the new log line.
