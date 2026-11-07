# [#342](https://github.com/synpoox/pd2-resurrected/issues/342): PD2's states column `immed`

Findings only: no PD2 or Blizzard file content is in this repo.

## Method

Ghidra 12.2 headless, read-only, on PD2's 1.13c `D2Common.dll`, plus a byte search of every DLL and EXE in the PD2
install (`C:\Games\PD2\ProjectD2`) for the column name, ASCII and UTF-16.

## What `immed` does in PD2

Nothing: no PD2 code reads it.

- **The loader skips it.** D2Common's states loader `0x6fdaf6e0` (DataTbls.cpp) builds its column table on the stack
  and passes it to the txt reader (`0x6fdaef40`, table `"states"`). The table has 40 flag columns (type `0x1a`, flag
  bits 0-`0x27`: `nosend` bit 0 to `notondead` bit `0x27`, every bit used), then
  the overlays, `stat`, the functions, colours, sounds, item and client columns, and its `end` row. `immed` is not
  among them. A column the table does not name is never read, so `immed` reaches no record field.
- **No other code names it.** The only `immed` bytes in `D2Common.dll` are in `immediate` (skills' column, `0x6fde0f48`).
  `D2Game.dll`, `D2Client.dll`, `ProjectDiablo.dll` and the other PD2 DLLs have no `immed` string at all, so none of
  them looks the column up by name.

`immed` is a vanilla 1.13c column (set on vanilla's aura states) that 1.13c already ignored; PD2 set it on its own
`immune_give`, `immune_get` and `phasing` too, with no effect. D2R dropped the column.

## Decision

**Cut.** It changes no play in PD2, so there is nothing to build or fold. Deviation row "PD2 state column immed" is
now cut.
