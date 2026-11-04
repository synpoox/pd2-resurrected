# P12: Ormus' Robes and skill on cast

PD2's Ormus' Robes (merged unique 358) carry `sorc-skill-rand-ctc` (par 2, min 12, max 26): one random sorceress
skill at +2, and a chance to cast that skill when the wearer casts a spell. The same "on casting" half is the generic
`cast-skill` property (properties func 11, D2R's own) on 62 live item rows, which writes the same stat,
`item_skilloncast` (merged itemstatcost 378, PD2 200). Before P12 none of them did anything in D2R.

## The property: func 25 (`0x103a12a0`)

- Picks an index 0..19 from the item's seed into PD2's list 8 (built at `0x10206ba0`), as merged ids:
  Ice Bolt 39, Ice Blast 45, Frost Nova 44, Glacial Spike 55, Blizzard 59, Ice Barrage 441, Frozen Orb 64,
  Charged Bolt 38, Lightning 49, Nova 48, Chain Lightning 53, Telekinesis 43, Fire Bolt 36, Fire Ball 47, Blaze 46,
  Fire Wall 51, Meteor 56, Lesser Hydra 455, Hydra 62, Combustion 448.
- Sets `item_singleskill` (107) with layer = skill and value = par.
- Chance = min, times 0.67 when the skill has a `localdelay` (Blizzard, Blaze, Fire Wall, Meteor, Combustion: 8%).
- Level = max - (reqlevel - 6) / 6, C truncation (Ice Bolt 26, Lightning 25, Blizzard 23, Hydra 22).
- Sets `item_skilloncast` with layer = skill << shift | level (the data tables' skill-layer split) and value = chance.

D2R's property slot 25 is `affix-rand`, so the rule takes the free slot 26 (`tools/pd2_ids.py` FUNCS, by value on
every properties funcN). D2R 93847: table `0x2386ab0`, 37 slots, 26-35 null; set-stat `0x3d5940`
(dataFlag, a1, a2, item, Property*, set, stat, layer, value, a8, a9, a10); item random `0x3d5860` (item, min, max).

## The proc: itemeventfunc 33 (`0x103473a0`) on `dospellcast`

PD2's event reads the stat's chance for the listener's layer, rolls the caster's seed below 100, and casts the
layer's skill at the event's target, or at the caster's aim point for a skill whose ItemTarget is 5 (Nova, Frost
Nova) or with no target. It holds the "event cast" unit flag (`0x10000000`) around the cast.

Where PD2 raises `dospellcast` was not found: no ProjectDiablo.dll call reaches 1.13c D2Game's event dispatcher
(`0x6fc57030`; its getter `0x10311a70` fills slot `0x105ebbdc`, copied to `0x105e0bd4`, which nothing reads). So the
trigger is approximated: after D2R's server skill dispatcher (`0x43acb0`) runs a skill for a player in cast mode
(SC, 10), not while the event-cast flag is set (no proc from a proc).

D2R would not run the listener even if the event were raised: equipping the robes does register a listener node on
the wearer (unit +0xe0; +0 event 18, +4 kind 2, +8 layer, +0xc stat 378, +0x38 next), but its function (+0x28) is a
D2RCore trampoline that dispatches through D2RCore's own copy of the item event table, which a plugin cannot patch.
Raising event 18 through `0x44d570` reached the node and never ran a function patched into D2R's table slot. So
core-rules walks the wearer's listener list itself and runs PD2's event for each `item_skilloncast` node, which also
keeps each item's own layer (two cast-skill items both proc). The data keeps PD2's itemeventfunc 33.

Casts: at a target `0x5896e0` (unit, skill, level, target, 1); at a point `0x589820` (unit, skill, level, x, y, 0).

## Checked in game (Slot7, Hell, level 95 Sorceress)

Rolls matched the formula (Meteor 8% level 23, Ice Bolt 12% level 26, Lightning 12% level 25, Blizzard 8% level 23,
Fire Bolt 12% level 26), and the tooltip reads e.g. "8% chance to cast level 23 Blizzard on casting". 150 Ice Bolt
casts with the Blizzard robes: 150 rolls, 13 procs (8.7%), each roll under 8, each Blizzard level 23 at the aim point.
A throw (Fire Blast, TH) or melee skill raises nothing, as PD2's spell-cast event would not.
