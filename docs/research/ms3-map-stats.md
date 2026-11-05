# MS3: monster and player map stats in D2R

Resolves MS3 ([#218](https://github.com/synpoox/pd2-resurrected/issues/218)). PD2's side is
[pd2-map-system-dll.md](pd2-map-system-dll.md), section 2. D2R addresses are RVAs in the 93847 dump
(`ImageStamp 0x6ab3782c`), read in Ghidra (`D2R.gpr`). PD2's are VAs in `ProjectDiablo.dll` (`PD2.gpr`).

## The level life regen cap is PvP only

PD2's wrapper `0x102fc000` around the player regen tick's `hpregen` read returns at most 30 (or the `bloodwarp` list's
value) when `0x1037c820` is true. That function follows the player's path, room and level to the level id
(`pLevel+0x1d0`) and jumps to `0x1037c7b0`, which is true only for levels 157, 159 and 166: the PvP arenas (the same
test the map handler uses to send PvP maps to the Rogue Encampment). PvP is cut (#151), so the cap is cut with it.
[plrmodes.md](plrmodes.md) guessed it was for maps.

## The routing table

D2R's `itemstatcost` has no `Divide` column, so core-maps carries PD2's values as `Routes` in
`plugin/core-maps/map_stats.cpp`, in the port's stat ids (`merge/ids-itemstatcost.tsv`). The port's ids keep vanilla's
0-358, so the targets are PD2's `Divide` values, except `map_mon_splash`: PD2 359 `item_splashonhit` is port 412. The
table was checked against PD2's `itemstatcost.txt` and the id list with a scratch script (no entry missing, extra or
different). A stat that is not in the table goes to monsters as itself, which is PD2's default `Divide` 1024.

## PD2's list builder (`0x1038aa00`)

- It makes a stat list (flags 8) with state 198 (port 244, `map`) unless the unit has one, attaches it and sets the
  state. The port uses flags 0, as D2R's own permanent lists do (`0x49bef0`, `0x48e600`).
- The list stat setter (D2Common #10188 -> #10261) sets and does not add, so two map stats with one target (for example
  `map_mon_att` and `map_mon_tohit`, both to-hit %) keep the later one, in stat order. D2R's `0x2f7c00` sets as well.
- A value of 0 is stored as 1. The stat keeps its layer.
- `item_maxhp_percent` on a monster multiplies its `maxhp` directly: `maxhp + maxhp / 100 * v` (unsigned, capped at
  0x7fffffff), and the list gets stat 432 (port 485) = 1 as a mark. Current life is not raised.
- Phys-as-extra (PD2 432-436, port 485-489): `(int)(v / 100.0f * damage)` of the monster's `mindamage` and
  `maxdamage`, as lightning 50/51, cold 54/55, fire 48/49, poison 57/58 and magic 52/53. The min goes through the 0 -> 1
  rule, the max does not. Cold length 56 and poison length 59 take the stat's layer, or 25 and 125 when it is 0.
- PvP arena stats and Horazon's resist rewrite (an area entry, MS9) are not part of MS3. The area slots' extra monster
  stats (slots 4, 12, 14) are MS4.

## Where the port hooks D2R

- **Opening:** `RouteMapStats` runs after the red portal opens (`map_open.cpp`). It reads the item's magic list
  (`0x2f5b80(item, 0, 0x40)`; entries `{layer, stat, value, extra}` at +0x30, count at +0x38) and keeps the monster
  and player lists in D2R's per-game counters (`0x5d88b0` on game `+0x1aac8`), so a new game starts without them.
- **Monsters:** `0x494a60` (spawn params, monster) is D2R's per-class setup of a new monster. Its only caller is the
  spawn core `0x493cc0` (`D2Game\src\Monster\MonsterSpawn.cpp`), which every spawner reaches (`0x492cf0` and the
  others), after the unit's stats and its alignment (`0x48e600`, from monstats `Align` at record `+0x87`) are set. A
  hostile monster in a map area (port levels 138-201) gets the list. PD2 gives a monster with an `Align` an empty list
  under the state; the port gives it none.
- **Players:** PD2 checks on a level change (`0x1038b000`, its hook of 1.13c D2Game `0x1cae0`). The port checks every
  frame in the player's life regen event `0x42e600` (`D2Game\src\Player\PlrModes.cpp`, unit event 3, which adds itself
  again for the next frame): in a map area the player gets the list unless it has one, elsewhere it loses it (state
  cleared, list detached and freed).

Not ported here: PD2's client packet 0x55 with up to 24 of the map's stats (the map's stat panel in the HUD).

## Game check

Slot 7, Hell, the level 95 Paladin, `cheat spawn t11,typ r,inv 0` in Harrogath:

- Opening logged each stat and its route. First map: magic and gold find 22 and experience 22 to players, density and
  rarity elsewhere (MS4), `lifedrainmindam` 7, `item_maxhp_percent` 17, `item_openwounds` 8, `damagepercent` 14 and
  `map_mon_dropweapons` 4 to monsters.
- Entering Ruins of Viz-Jun (level 144): "player list on"; the first monster's maxhp went 10002 -> 11702 (+17%).
- Back through the portal to Harrogath: "player list off".
- Second map (new process): the player's totals in the map were exactly the map's values (magic find 80, gold find 80,
  experience 7, block -13, life regen -37); a monster's were poison mastery 7, armor % 63, `map_mon_droparmor` 3.
  24 monsters got the list.
- A new game, then `cheat goto 144` without a map: no list on the player or the monsters.

Not run: a phys-as-extra map (no rolled map carried one; the magic prefixes with `map-mon-extra-mag` also fail
modkit's validation, [modkit-validation.md](../modkit-validation.md)), and whether the client shows the player's new
values (the Advanced Stats panel is empty in this build).
