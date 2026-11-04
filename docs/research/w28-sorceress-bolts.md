# Sorceress: bolt hits (shit 62) and Chilling Armor's event 3 (W28)

Answers work item W28 (#103). PD2 side read from `ProjectDiablo.dll` (VAs `0x10...`) and the 1.13c `D2Game.dll` in the
Ghidra project `PD2.gpr`, D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`). Addresses are D2R RVAs unless they
start with `0x10`; 1.13c D2Game addresses are offsets in that DLL.

PD2's missile install `0x103a9170` writes srvhit 62 = `0x103a86d0` (table `+0xf8`, next to 61 `0x103a84f0` and 63
`0x103a8800`). D2R's srvhit 62 is empty. The aura event install `0x1036c2b0` writes event 3 = `0x10346020`.

## shit 62 (`0x103a86d0`)

Rows: firebolt, icebolt (sHitPar1 3), blizzard1-4, chillingarmorbolt (sHitPar1 4); sHitPar2 empty, ResultFlags 4,
HitFlags empty. blizzard2/3 are reached by no data path, as the inventory found.

`(game, missile, hit)`, the hit unit unused:

1. Needs a game, the missile, its path and a valid missiles.txt row.
2. The owner: 1.13c D2Game `0xe03a0(game, missile +0x94 type, +0x98 id)` (the unit lookup by type and id); none:
   return 1.
3. The hit point is the missile's path x/y. Skill = row Skill (`+0x194`), level = missile level (data `+0xc`) +
   sHitPar2 (`+0x50`).
4. A zeroed 0x70-byte block: the skill's physical damage (D2Game `0x9f1a0`, version entry `0x104aaa14`) and its
   elemental damage (D2Game `0x9f210`, entry `0x104a96c4`; D2R's ElementalDamage `0x437d00`). Result flags |=
   ResultFlags (`+0xac`, u16) | 1, hit flags |= HitFlags (`+0xa8`), block `+0x5c` = the missile.
5. D2Game's area search `0xa0c70` (entry `0x104a93fc`) around the hit point, radius sHitPar1 (`+0x4c`), unit mask
   `0x8583`, callback `0x10304a90`.
6. Returns 1. D2R's collision (`0x4639a0`) deals the missile's own damage only on bit 2, so the struck unit takes
   only the splash.

Callback `0x10304a90`: copies the block (one roll for every unit), and with a missile owner (type 3) and a player
victim sets the victim's player data `+0x194` to the missile's skill for the apply; then PD2's area apply
`0x10305760`, which [w17-holy-light](w17-holy-light.md) found equal to D2R's AreaApply `0x436e90`. A missile owner is
never a missile, so that branch is not taken.

D2R missile record: HitFlags `+0xd0` (u32), ResultFlags `+0xd4` (u16), 1.13c + 0x28, read from the missiles field
table built by `0x3c2c70`.

core-rules fills srvhit 62 (`SrvHitBoltSplash`): MissileOwner, ElementalDamage into one block, then D2R's AreaUnits
around the missile with a copy of that block per unit through AreaApply, returning 1.

Deviation (approximated, on the unported list): no physical damage (none of the four skills has MinDam/MaxDam) and no
missile in the block. PD2's deal step reads block `+0x5c` for a missile kill; D2R's block has no field found for it.

## Chilling Armor's event 3: data

PD2's Chilling Armor and Chilling Armor SelfAura have `auraevent2 damagedinmelee` with `auraeventfunc2 3`, Shiver
Armor's vanilla event. D2R's Chilling Armor has only `hitbymissile 1`; the merge keeps PD2's columns.

PD2 `0x10346020 (game, event, unit, attacker, ?, skill, level)` and D2R `0x552a50` do the same:

| Step | PD2 | D2R |
|---|---|---|
| refuse | no unit, attacker or skill record | the same, and an attacker that is not a player or monster |
| damage | the skill's elemental damage at max(level, 1) from the unit | the same (`0x437d00`) |
| block | result flags `0x4021`, hit class `\|= 0xd`, flag byte 1 | the same |
| attacker's event skill | a player attacker's data `+0x194` = the skill | the attacker's event skill set, then restored |
| apply | PD2's event pass (area 1) `0x10305990`, deal `0x103025b0` | HitEvents `0x44ce80` (area 1), DealDamage `0x44a9b0` |
| overlay | the skill's srvoverlay on the attacker | the same |

D2R's event 3 asserts the event is 1 (damagedinmelee) or 3 (attackedinmelee), so Chilling Armor's damagedinmelee is
accepted. Only players and monsters strike in melee, and the restored event skill is not read after the apply, so the
two differ in nothing a player sees: event 3 stays D2R's and is data.

## Checks

- `tests/game/core_rules_checks.py`, tag `core-rules-w28`: `bolt-splash` (radius = PD2's sHitPar1, the row's Skill,
  fire or cold damage, units hit, all four skills) and the slot in `core-rules-loads`; `chilling-armor-event`
  asserts the merged Chilling Armor rows keep `damagedinmelee 3`.

Run on Slot7 (2026-11-04), a level 95 Sorceress with Fire Bolt, Ice Bolt, Blizzard and Chilling Armor at 10, normal
difficulty (the save has no other difficulty open), god mode, zombies in Blood Moor and quill rats for Chilling
Armor: all three checks pass. Blizzard radius 4, 172 cold on 3 units; Ice Bolt radius 3, 43 cold on 2; Fire Bolt
(level 11) radius 3, 52 fire on 1; chillingarmorbolt radius 4, 66 cold on 4. No crash.
