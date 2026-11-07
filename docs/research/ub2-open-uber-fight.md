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
  distance under 0x96, is handled in turn: pets removed, 37 states stripped, warped in. The others are picked before
  anyone moves. If the user does not arrive, nobody else goes and the opening fails, as in PD2. The tier (the key's
  stat 370) and the number who arrived go into the game's counters (keys `UBRT` and `UBRE`, PD2's game `+0x2629` and
  `+0x2628`), and the opening counts on `MAPS`, the one-per-game gate the maps use.

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

## Re-entry portals and the area rules on entry ([#346](https://github.com/synpoox/pd2-resurrected/issues/346))

- **Tier 0:** before the warp, while the user still stands by the fountain, the opening makes PD2's three grey portals (object 595 `xactportal_grey`, the
  port's 598) at (5076, 5119), (5068, 5108) and (5079, 5110), each leading to the uber level. If nobody arrives they
  are removed again. Tiers 1 and 2 make none.
- **PD2's portal operate:** PD2 gives its `xactportal` objects operate function 77. The merge moves it to D2R's operate
  slot 60, which is empty (`pd2_ids.FUNCS`), and core-maps writes its function there. That function runs the area
  rules when the portal leads into an uber level: the entry count (`UBRE`) goes up by one, the fight's bosses heal 20%
  of their max life, pets are removed and the 37 states stripped. Then the player is warped in, and a grey portal is
  removed. The other `xactportal` objects (the fights' own portals, later parts) take the same function, without
  the single use.
- **Waypoints:** a player who arrives in level 138 or 160-162 (PD2's 137 and 161-163), by the opening or a portal,
  has the `Waypoint` objects (operate 23) in the rooms around them removed.

### D2R functions

- **Operate table** `0x23a51c0`: 75 slots of `int fn(args*)`, args = {game, object, player, seed, class id}, indexed
  by objects.txt `OperateFn` (record `+0x15e`). The dispatcher `0x592c40` skips a null slot, so with core-maps off a
  grey portal does nothing. D2R's portal operate 15 (`0x58f680`) moves the player next to the linked partner portal,
  and fails without one: PD2's grey portals have no partner, so the slot-60 function warps with `0x488180`.
- **Making a portal:** D2R's portal maker `0x432ce0` spawns the object with `0x490410` (2, row, x, y, game, room, 1, 1,
  0, 0) in the room `0x38dfb0` finds for the spot, sets mode 1 and adds event 1 (the opening) after the row's
  `+0x9c >> 8` frames. Then `0x435dd0` makes a linked return portal in the target level. core-maps does the first part
  only and sets the destination with `0x34e9d0`.
- **Removing an object:** as operate 15 removes a used town portal: `0x2ef4d0` (room, 2, unit id) tells the room's
  clients, `0x48faa0` (game, unit) frees the unit, `0x2f0530` (room) marks the room changed.
- **Rooms and units:** `0x2efde0` gives a room and its neighbours, a room's units start at `+0xa8` and go on at unit
  `+0x160` (as `0x593630` walks them). The game's monsters are 128 lists at game `+0x2630`, by unit id & 0x7f, the next
  at `+0x158` (`0x48fe80`, `0x4925b0`).

## Not yet (later parts of [#227](https://github.com/synpoox/pd2-resurrected/issues/227))

- Rathma's tier-0 re-entry at the current stage's spot. It reads the Rathma fight's stage byte, which comes with that
  fight's phase script.
- The boss heal finds the bosses by monster class in the level. PD2 heals the bosses its phase scripts registered.
- A key used up when nobody arrives: PD2 keeps it (`0x10383060` returns 0); the port's check refuses every case it can
  foresee, but a failed warp in the use still spends the key.
- The tier scaling (state `uberscaling`, section 4).
- The key drop rule (section 7).

## Game check

`uber-open`, in Hell on a level 90+ character with every quest done, a Diablo Clone key from the console
(`cheat spawn dcma,inv 0`):

- Away from the fountain in Harrogath: refused ("not at the Blood Fountain"), and the key stays.
- At the fountain with a mercenary and Battle Orders: the player arrives in level 138 without the mercenary or the
  state, and the key is gone.
- Back in town, a map in the same game: refused ("already open").

`uber-portals` ([#346](https://github.com/synpoox/pd2-resurrected/issues/346)), the same setup:

- A tier-0 Diablo Clone key: three grey portals stand by the fountain, and the level shows no waypoint on arrival.
- Back in town (town portal), with a mercenary and Battle Orders: a grey portal takes the player into level 138
  without them, the portal is gone and the log counts entry 2.
- A tier-1 key (`cheat spawn dcma,inv 0`, then the cube's tier row) in a new game: no portals.
