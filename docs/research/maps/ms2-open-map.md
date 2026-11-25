# [MS2](https://github.com/synpoox/pd2-resurrected/issues/217): opening a map in D2R

Resolves the D2R side of [MS2](https://github.com/synpoox/pd2-resurrected/issues/217) ([#217](https://github.com/synpoox/pd2-resurrected/issues/217); PD2's side is
[pd2-map-system-dll.md](../../pd2/map-system-dll.md), section 1). D2R addresses are RVAs in the 93847 dump
(`ImageStamp 0x6ab3782c`), read in Ghidra (`D2R.gpr`); PD2's are VAs in `ProjectDiablo.dll` (`PD2.gpr`).

## The data

- PD2's map spell `pSpell` 12 is D2R's Resist Scroll, so the merge moves it to 16 (`tools/pd2_port.py` FUNCS), the
  first empty entry of D2R's 20-entry item spell table (`0x236fe30`).
- PD2 evaluates misc `len` as the level the map opens. The merge writes it in the port's level ids on every PD2 pSpell
  12 row (`fix_map_level`): Ruins of Viz-Jun `t11` 143 -> 144, Descent from Mount Arreat `t2a` 210 -> 201. A left-out
  level (PvP, PD2's Uber Ancients) leaves it blank.
- The uber keys, the PvP maps and the Monastery map keep a blank pSpell (`merge/standins.tsv`) and their no-drop lines:
  [UB2](https://github.com/synpoox/pd2-resurrected/issues/227) opens the keys, PvP is cut, and PD2 refuses the Monastery map's level 152.
- The other maps and the two map orbs (`upmp`, `fort`, used by [MS5](https://github.com/synpoox/pd2-resurrected/issues/220)'s recipes) drop again.

## D2R's item spells

The generic D2R facts (the use-item handler and its check/use dispatchers, spell entry 16 running nothing and the item
being freed, the cube "Red Portal" output with its per-game counter, the quest-record test) are in the toolkit:
`knowledge/d2r-93847-addresses.md`, *Maps, regions, portals and item spells (build 93847)*.

What core-maps decides on top of them (PD2's gates, kept here):

- **Check:** an item whose record has pSpell 16 never reaches D2R's code. The level is `len` through D2R's item calc.
  Then PD2's gates: Hell, character level 80+, Rite of Passage done in Hell (quest 39, bit 0), standing in Harrogath,
  and no opening yet in this game. Last, a free spot for the portal, so a map is never used up for no portal (PD2
  places it unchecked). A refusal sends client message 0x15 and keeps the map; D2R's handler sends nothing on a false
  check, so the player sees one message.
- **Use:** D2R's cube "Red Portal" output makes the portal to the level, and its game counter for the key `'MAPS'` with
  max 1 is the one-per-game gate, which [UB2](https://github.com/synpoox/pd2-resurrected/issues/227)'s uber areas share.

## The core-rules lists

**Desecrate.** PD2's raise list skips the monsters in a set at `0x105de9e8` (read by `0x1036d600`). `0x10190990`
fills it with 40 hcIdx built on its stack from constants at `0x10450700`-`0x10450810` and four immediates: 247-249,
540-542, 824, 845-852, 916, 917, 940-943, 946, 959, 970-986. With the name patterns of
[necromancer-w13.md](../skills/necromancer-w13.md) (frogdemonThrone, firetowerMarket, `*Maus`, `*Rat`, `*Rat2`, `void*`, case as
written) that is 51 monsters, as merged monstats ids in `NeverRaised`.

**Find Potion and Find Item.** The 46 hcIdx of [w10-barbarian.md](../skills/w10-barbarian.md), as merged monstats ids in
`Unsearchable`; the server and client starts refuse their corpses.

## Game check

`map-open`, on level 95 characters with every quest done, a magic Ruins of Viz-Jun map (`cheat spawn t11,typ m,inv 0`):

- Hell, Harrogath: a red portal to level 144 by the player; the map is gone, and the portal leads into Ruins of
  Viz-Jun (Hell) with a portal back.
- A second map in the same game: refused ("already open"), the map stays, the client shows its refusal.
- Hell, the Pandemonium Fortress: refused ("in level 103, not Harrogath").
- A new game: the count starts again, and the map opens.
- With the free-spot check: Hell, Harrogath, level 90 Paladin: the portal opens to level 144.
- Normal, the Rogue Encampment: refused ("not Hell").

Not run: the no-free-spot refusal (no crowded spot was set up), the Rite of Passage refusal (every test character has it done), Find Item and Desecrate on map monsters.
