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

A count over 25 reads `Monsters Remaining: 25+`. A count of 25 or less reads the plain `Monsters Remaining: N`, with
no `+`. That number can still grow while rooms of the level are unvisited. PD2 hides this with its `100+` until 90% of
the rooms have activated; the owner's rule does not.

## Towns

D2R's ledger is not empty in town: the Rogue Encampment's region read `spawned 24` in game. So the ledger alone cannot
hide the row there. The row is hidden when the level's region has MonDen 0 for the difficulty (region +0x2b8, from
levels.txt). Every town has MonDen 0, and so do preset-only areas such as boss lairs. No town data flag was found in
93847: D2R has no `{1, 40, 75, 103, 109}` table, and no function compares a level against those ids.

## Placement (D2R 93847)

The zone info is one newline-separated text block that D2R rebuilds in `0x14751e0` (panel). It holds area, difficulty,
game type and the other rows at panel +0x198, and the terror-zone lines at +0x1e8. The panel update `0x1476290` passes
the block to the panel's text box (+0x170) through TextBox::SetText `0x86dd50` (box, {text, length}*), which copies
the text. The loader adds the mod name row after the builder returns, so a row appended inside the builder lands above
`PD2Resurrected`; that was checked in game.

core-ui hooks the builder only to learn the panel. It hooks SetText to add the row to that box's text, before the
block's trailing newline (the gap above the terror-zone lines). D2R draws the row as part of the block: same font and
alignment, TAB hides it with the automap, and it follows the column at 21:9.
