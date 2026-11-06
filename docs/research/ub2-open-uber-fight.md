# [UB2](https://github.com/synpoox/pd2-resurrected/issues/227): opening an uber fight in D2R

The D2R side of the uber key opening. PD2's side is [pd2-uber-bosses-dll.md](pd2-uber-bosses-dll.md), sections 2
and 3. D2R addresses are RVAs in the 93847 dump (`ImageStamp 0x6ab3782c`), read in Ghidra (`D2R.gpr`); PD2's are VAs
in `ProjectDiablo.dll` (`PD2.gpr`).

## The data

- The keys `dcma`, `rtma` and `luca` lose their blank-pSpell stand-ins, so they take PD2's map spell 12 as item spell
  16, like the maps ([ms2-open-map.md](ms2-open-map.md)). Their `len` becomes the port's level: 138, 160 and 184
  (PD2 137, 161, 188).
- They lose their no-drop lines too. PD2 makes them in the cube (rows "Uber Diablo Map", "rathma ingredients",
  "lucion ingredients"), and its tier rows (op 18 on stat 185, the port's 370) cycle the tier.
- `ubtm` (Uber Tristram, [UB3](https://github.com/synpoox/pd2-resurrected/issues/228)) keeps both, and `uba` (PD2's
  Uber Ancients) is cut.

## The opening (core-maps, `plugin/core-maps/map_open.cpp`)

The map spell's check and use hooks handle the keys too. A key's `len` in the uber levels takes this path:

- **Check:** the map gates, in PD2's order, then the Blood Fountain. PD2's `0x1037d840` returns the squared distance,
  so the user must stand with `dx² + dy² <= 0x95` from (5074, 5114): about 12 subtiles. A refusal keeps the key.
- **Use:** PD2's `0x1038a680`. The user, then every other player in Harrogath who is level 80 or more with a squared
  distance under 0x96, is handled in turn: pets removed, 37 states stripped, warped in. The opening fails if nobody
  arrives. The tier (the key's stat 370) and the number who arrived go into the game's counters (keys `UBRT` and
  `UBRE`, PD2's game `+0x2629` and `+0x2628`), and the opening counts on `MAPS`, the one-per-game gate the maps use.

### D2R functions

- **Warp:** `0x488180` (game, player, level, 0). In the same act it finds the level's arrival spot through
  `0x2ef8b0` (the level's warp search in `D2Common\src\Drlg\DrlgWarp.cpp`) and teleports there with `0x491ca0`; else
  it changes act. D2R's debug "Warping to level..." handler `0x402820` calls it the same way. PD2 instead places an
  invisible portal object (587) and runs each player through its portal-entry code.
- **Players:** the game's clients are a list at game `+0x138`, each client's next at `+0x628` (`0x484d90` walks it to
  find a client by id). `0x484c90` (client, 0) is the client's player.
- **Pets:** PD2's `0x1037fb10` (flag 0) walks every pet type but 3, `golem`, and kills the hireling (type 7). core-maps
  walks D2R's pet table (`PetEntry`, `FirstPet`) the same way and removes each pet with `RemovePet`, which kills it.
  PD2 removes the summons without a death. The merged pettype.txt has 31 rows.
- **States:** PD2's `0x1037f4a0` with flag 1. Each state that has a stat list is turned off, and the list is detached
  and freed, as D2R's `RemoveSkillState` does per state.

## Not yet (later parts of [#227](https://github.com/synpoox/pd2-resurrected/issues/227))

- The tier-0 re-entry portals (PD2's three object-595 portals, single use) and the area rules on re-entry: the 20%
  boss heal, the entry count and the Rathma re-entry spots.
- The tier scaling (state `uberscaling`, section 4).
- The key drop rule (section 7).

## Game check

`uber-open`, in Hell on a level 90+ character with every quest done, a Diablo Clone key from the console
(`cheat spawn dcma,inv 0`):

- Away from the fountain in Harrogath: refused ("not at the Blood Fountain"), and the key stays.
- At the fountain with a mercenary and Battle Orders: the player arrives in level 138 without the mercenary or the
  state, and the key is gone.
- Back in town, a map in the same game: refused ("already open").
