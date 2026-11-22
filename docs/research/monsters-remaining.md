# Monsters Remaining: what PD2 counts (#1656)

Question: does PD2 count every monster of the level, and can we?

## PD2 (ProjectDiablo.dll, `C:\ghidra-work\pd2-project\PD2.gpr`)

- **Client.** Packet 0xAD, 6 bytes (`id, u8 flag, u32 count`): the client table entry is set at `0x1036c148`
  (`FUN_1036be80`: handler `0x10393ff0` at table +0x81c, size 6 at +0x820). The handler passes `(flag, count)` to
  BH.dll's `BHSetMonstersRemaining`. BH.dll's strings are `%d Monsters Remaining`, `0 Monsters Remaining` and
  `100+ Monsters Remaining`.
- **Server.** `FUN_103453f0`, a per-player tick. Every 12 frames (`frame % 0xc`) it builds the packet at
  `0x10345849` / `0x10345856` (`MOV word ptr [ESP+0x14],0x1ad` / `0xad`) and sends it with `FUN_103a1800(client, buf, 6)`:
  - flag 0, count 0 (hidden): town levels 1, 40, 75, 103 and 109, plus `FUN_1037c770`'s list (`FUN_1037c740`), and levels
    157, 159 and 166 (`FUN_1037c7b0`).
  - flag 1, count 0: no monster region for the level.
  - flag 1, count 101 (BH shows `100+`): `rooms activated x 100 / rooms < 90`. Rooms activated is the region's +0x04. Rooms
    is the region's +0x0c, or for PD2 maps (`FUN_1037c510` levels 137-210 minus 202-206, or `FUN_1037c640`) a hard-coded
    total (`FUN_1038b1d0`, 9-280 per level, 200 by default).
  - otherwise flag 1, count `region+0x2cc - region+0x2d0` (spawned minus killed, never below 0).

So PD2 counts the same per-level ledger as D2R and as our line: only monsters in rooms that have populated. It forces
no population and reads no preset or spawn table. It shows `100+` until 90% of the level's rooms have activated.

## D2R: counting every monster

- **Forcing every room of the level to populate on entry.** Rejected. D2R builds a room and its monsters only near a
  player and frees them once they are left. Forcing it would keep the whole level's units alive and thinking (AI, CPU,
  memory). It would also change spawns for everyone in a multiplayer game, and PD2 does not do it.
- **Predicting the total** (levels.txt MonDen, room sizes, presets). Rejected. Density rolls are random per room, so
  this gives an estimate, not a count.
- **Kept: the ledger.** It counts the monsters of the rooms met so far, the same as PD2.

## Text (owner's rule)

PD2's rule, with `50+` for PD2's `100+` (`25+` before #1729): while rooms made x 100 / rooms < 90 (integer division), the row reads
`Monsters Remaining: 50+`. From 90% on, a count over 50 reads `50+` and 50 or less the plain number, with no `+`.

PD2 reads the two room counts from its region (+0x04 rooms activated, +0x0c rooms; PD2 maps use a hard-coded total,
`FUN_1038b1d0`). D2R 93847 keeps both at the same offsets (the region is allocated in `0x508750`, +0x0c set to -1
until counted), and core-ui reads them the same way (#1729). Read in game: Blood Moor +0x04 9, 12, 15, 18 as rooms
populated (kept across a trip out and back), +0x0c 47; Cold Plains 70; Den of Evil 26.

Cartographer maps (levels 138-151) need no fallback total (#1736): in Tunneling Depths (level 138, Hell, slot run 2026-11-22) the region
read `rooms 9 of 90 made (10%)`, so D2R sets +0x0c on map levels and the row reads `50+` only until 90% of the rooms are made. Not
observed: the plain number past 90% (the check explored 10% of the map).

The first version walked the built level's drlg room list instead (DrlgLevel +0x10, next +0x48). That list holds every
room of the level: Blood Moor 84, Cold Plains 96, Den of Evil 27. In a dungeon the two totals nearly agree, but an
outdoor level lists many rooms that never make monsters, so 90% of the list was never reached and outdoor levels
read `25+` forever (#1729).

## Towns

D2R's ledger is not empty in town: the Rogue Encampment's region read `spawned 24` in game. So the ledger alone cannot
hide the row there. The row is hidden in PD2's five towns (levels 1, 40, 75, 103, 109), the same hard-coded list
PD2 uses; D2R 93847 has no town flag or table. MonDen 0 was tried first and dropped: it also hid the Ancients (120),
the Worldstone Chamber (132) and Uber Tristram (136).

## Placement (D2R 93847)

The zone info is one newline-separated text block that D2R rebuilds in `0x14751e0` (panel). It holds area, difficulty,
game type and the other rows at panel +0x198, and the terror-zone lines at +0x1e8. The panel update `0x1476290` passes
the block to the panel's text box (+0x170) through TextBox::SetText `0x86dd50` (box, {text, length}*), which copies
the text. The loader adds the mod name row after the builder returns, so a row appended inside the builder lands above
`PD2Resurrected`; that was checked in game.

core-ui hooks the builder only to learn the panel. It hooks SetText to add the row to that box's text, before the
block's trailing newline (the gap above the terror-zone lines). D2R draws the row as part of the block: same font and
alignment, TAB hides it with the automap, and it follows the column at 21:9.

## Legacy view (#1634)

The legacy view (key g) draws the same zone info panel and text box, so the SetText hook adds the row there with
no extra code (same loader lines, `found TextBoxSetText`, in both views). Checked in game: `25+` on entry to the
Burial Grounds, `2` at 29 of 30 rooms, no row in the Rogue Encampment, TAB hides and shows it with the block, and
`g` back to HD keeps it.
