# Desecrate ([W13](https://github.com/synpoox/pd2-resurrected/issues/88)): srvdo 161, cltdo 101

Answers work item [W13](https://github.com/synpoox/pd2-resurrected/issues/88) ([#88](https://github.com/synpoox/pd2-resurrected/issues/88)). PD2 side from the baseline install `C:\Games\PD2\ProjectD2` (`ProjectDiablo.dll`, base
0x10000000; its table fill at `0x1036ba36` writes slot n at table + n*4), D2R side from the 93847 dump
(`ImageStamp 0x6ab3782c`). PD2's Desecrate row merges onto D2R's Poison Explosion (skill 83).

PD2's row: srvdofunc 161, cltdofunc 101, calc1 `5` ("# summons per cast"), petmax 50, summode DD, pettype desecrate,
srvmissilea / cltmissilea poisonexplosioncloud, cltmissileb corpseexplosion, cltmissilec greenlightmissile.

## srvdo 161 (`0x103b1fa0`)

1. The skill's target point; the room there (return 0 for none) and its level's levels.txt record.
2. A list of the level's 25 monsters, mon1-25 in normal, nmon1-25 otherwise. An entry is skipped when it has no
   monstats record, PartyMin or PartyMax above 0, a `spawn` monster, or is on PD2's list: 40 hcIdx values
   (frogdemon1-3 247-249, ancientbarb1-3 540-542, map, uber and Rathma monsters) plus, by name, frogdemonThrone,
   firetowerMarket and every `*Maus`, `*Rat`, `*Rat2` and `void*` monster.
3. No entry left: rebuild from a random level of {6, 66, 76, 106, 132} until one has an entry (no bound).
4. calc1 corpses (petmax is evaluated and thrown away): a random list entry each, the first at the point, then (+2, +2),
   (-2, +2), then `rand() % 2 - 2` on x and y; the offsets add up. Each spawns in mode summode in the point's room
   (a failed spawn returns 0 before the cloud), gets owner type 0 / owner id = the caster, comes off the level's monster
   region count (+0x2cc), and gets a timer at frame + 400 whose callback `0x1036c6d0` sets state 104 (corpse_nodraw).
5. srvmissilea at the point, the caster's skill flag (0x40), return 1.

## cltdo 101 (`0x103ab630`)

The target point (the target unit's position, else the path's point); cltmissilea, then cltmissileb and cltmissilec
there. Returns 1.

## D2R pieces

| Need | D2R 93847 |
|---|---|
| room at x/y | `0x38dfb0 (room, x, y)` |
| level of a room | `0x2efc10 (room)`; region counter `[game+0x1d0][level]+0x2cc`, raised by spawn via `0x509520` |
| spawn | `0x492cf0 (game, room, x, y, monId, mode, -1, 0x42)`, as the summon helper `0x4350e0` calls it |
| levels.txt | DataTables `+0x1460` records (0x18c bytes), `+0x1468` count; mon1 `+0x3e`, nmon1 `+0x70` (int16) |
| monstats | `0x976e0`; spawn int16 `+0x54`, PartyMin `+0x62`, PartyMax `+0x63` |
| skills.txt | summode byte `+0x113` (pettype `+0x112`, petmax calc `+0x114`, read by Raise Skeleton's srvdo `0x55d1c0`) |
| difficulty | game `+0x104` |

PD2's timer is a game event; the port puts a 400-frame state list (tag 104) on each corpse whose expiry callback calls
D2R's `0x5551f0` and then sets corpse_nodraw. The `desecrate-corpses-hide` game check passed: all 5 corpses hid.

Game run (2026-11-03): the cast was made in the Rogue Encampment, with Desecrate's InTown set only on the test slot. Level 1
has no monsters, so the run also used the fallback levels: 5 of 5 corpses and 3 client missiles. A cast outside town is
not checked yet.

Monster ids: the merged monstats keeps PD2's runtime ids for the six on PD2's list that ported levels name
(frogdemon1-3 in Great Marsh, Flayer Jungle and Sewers 2; ancientbarb1-3 in Arreat Summit). No merged level names any
of the other 45, so the plugin lists only the six.
