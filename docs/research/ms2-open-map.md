# MS2: opening a map in D2R

Resolves the D2R side of MS2 ([#217](https://github.com/synpoox/pd2-resurrected/issues/217); PD2's side is
[pd2-map-system-dll.md](pd2-map-system-dll.md), section 1). D2R addresses are RVAs in the 93847 dump
(`ImageStamp 0x6ab3782c`), read in Ghidra (`D2R.gpr`); PD2's are VAs in `ProjectDiablo.dll` (`PD2.gpr`).

## The data

- PD2's map spell `pSpell` 12 is D2R's Resist Scroll, so the merge moves it to 16 (`tools/pd2_ids.py` FUNCS), the
  first empty entry of D2R's 20-entry item spell table (`0x236fe30`).
- PD2 evaluates misc `len` as the level the map opens. The merge writes it in the port's level ids on every PD2 pSpell
  12 row (`fix_map_level`): Ruins of Viz-Jun `t11` 143 -> 144, Descent from Mount Arreat `t2a` 210 -> 201. A left-out
  level (PvP, PD2's Uber Ancients) leaves it blank.
- The uber keys, the PvP maps and the Monastery map keep a blank pSpell (`merge/standins.tsv`) and their no-drop lines:
  UB2 opens the keys, PvP is cut, and PD2 refuses the Monastery map's level 152.
- The other maps and the two map orbs (`upmp`, `fort`, used by MS5's recipes) drop again.

## D2R's item spells (`D2Game\src\Skills\SkillItem.cpp`)

The server's use-item handler `0x4f40c0` calls the check dispatcher `0x5817f0` (game, player, item, target) -> bool,
then the use dispatcher `0x581680`. Both look up the item's spell entry (`0x300ae0`); the entry holds handler indexes
(check below 11, use below 16), so entry 16 runs nothing. After the use, the caller frees the item when the entry's
flag is 0, as entry 16's is: a used map is gone.

core-maps hooks both dispatchers. An item whose record (`0x314110`) has pSpell (`+0x94`) 16 never reaches D2R's code:

- **Check:** the level is `len` (`+0xb0`) through D2R's item calc `0x3b4880` (dataFlag, player, item, calc), as the
  timed potion handler `0x57ecb0` evaluates it. Then PD2's gates: Hell (game `+0x104`), character level 80+ (stat 12),
  Rite of Passage done in Hell, standing in Harrogath (109), and no opening yet in this game. A refusal sends client
  message 0x15 and keeps the map.
- **Use:** D2R's cube "Red Portal" output `0x5da800` (game, player, key, level, max). It needs town, finds a free spot,
  makes object 60 to the level through the portal maker `0x432ce0` (with its last argument 1, which lifts the town
  portal's level whitelist), and adds 1 to the game counter of `key` (`0x5d88b0` on game `+0x1aac8`). With max 1 it
  refuses once the counter is 1: core-maps' key `'MAPS'` is the one-per-game gate, which UB2's uber areas share.

**Quest record.** `0x325c50` (record, quest, bit) tests a quest bit (`D2Common\src\QuestRecord\QuestRecord.cpp`). The
item-use quest checks in `0x4734f0` read a player's record for a difficulty at player data (`0x34b240`) `+0x40 +
difficulty * 8`. Rite of Passage is quest 39; bit 0 is "done".

## The core-rules lists

**Desecrate.** PD2's raise list skips the monsters in a set at `0x105de9e8` (read by `0x1036d600`). `0x10190990`
fills it with 40 hcIdx built on its stack from constants at `0x10450700`-`0x10450810` and four immediates: 247-249,
540-542, 824, 845-852, 916, 917, 940-943, 946, 959, 970-986. With the name patterns of
[necromancer-w13.md](necromancer-w13.md) (frogdemonThrone, firetowerMarket, `*Maus`, `*Rat`, `*Rat2`, `void*`, case as
written) that is 51 monsters, as merged monstats ids in `NeverRaised`.

**Find Potion and Find Item.** The 46 hcIdx of [w10-barbarian.md](w10-barbarian.md), as merged monstats ids in
`Unsearchable`; the server and client starts refuse their corpses.

## Game check

`map-open`, on level 95 characters with every quest done, a magic Ruins of Viz-Jun map (`cheat spawn t11,typ m,inv 0`):

- Hell, Harrogath: a red portal to level 144 by the player; the map is gone, and the portal leads into Ruins of
  Viz-Jun (Hell) with a portal back.
- A second map in the same game: refused ("already open"), the map stays, the client shows its refusal.
- Hell, the Pandemonium Fortress: refused ("in level 103, not Harrogath").
- A new game: the count starts again, and the map opens.
- Normal, the Rogue Encampment: refused ("not Hell").

Not run: the Rite of Passage refusal (every test character has it done), Find Item and Desecrate on map monsters.
