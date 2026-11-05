# IR6: cube rules and stacking in ProjectDiablo.dll

Work item IR6 ([#241](https://github.com/synpoox/pd2-resurrected/issues/241), [item-rules.md](../item-rules.md)): the
demonic cube reroll (op 31), corruption in one press, and the stack split (`HOTKEY_ItemQuantity`). Cube recipes that
count a stack by its quantity (a stack of 3 El runes making Eld) are out of IR6: [#315](https://github.com/synpoox/pd2-resurrected/issues/315).
Findings only: no PD2 or Blizzard file content is in this repo.

## Method

Ghidra 12.2 headless, read-only: PD2's cube walk (`0x366f40`), found from op 30's research
([pd2-map-system-dll.md](pd2-map-system-dll.md), section 3), and its callees; the `HOTKEY_ItemQuantity` string's one
code use. On the D2R 93847 dump: the cubemain output parser (`0x3e5e60`, from the string `usetype`), the executor
(`0x5269c0`) and the `uniques` cheat (table entry at `0x238b600`, handler `0x411340`).

## What PD2 does

**The cube walk (`0x366f40`, player, cube owner).** It collects the candidate rows (`0x365fe0`), then tries each in
order with the input matcher (`0x364e30`). On the first match: op 30 stops (nothing made), op 31 calls the reroll
(`0x3669b0`), any other op runs the row (`0x3653d0`). After a row has run, the row's `ladder` byte (record +1) decides
what happens next:

- `2` (a corruption roll row): if `0x364960` holds (the cube has no Worldstone Shard, `wss` or `cwss`, still carrying
  stat 360), the walk resets the player's +0x25fc frame stamp and runs again: the outcome row fires in the same press.
- `3` (a "destroyed" outcome): each item in the cube that fails `0x10309c60(item, 0x68)`, carried the `0x1030cc40`
  flag before the row and has neither stat 360 nor that flag now gets `0x1030cca0`. Which flag that is was not traced
  (op 31 reads it as a choice between two item-make modes); P9's merged rows already mark these items.
- Outcome rows (`ladder` 1 and 3) are only candidates while `0x364960` holds.

Last, every stackable in the cube (a fixed list of item types) whose quantity (stat 70) is outside its `minstack` to
`maxstack` is deleted, except arrows and bolts (`aqv`, `cqv`). `0x365b50` limits presses to one per 10 frames.

**Op 31, the reroll (`0x3669b0`, `CubeFunctions.cpp`).** For each item in the cube:

- the Demonic Cube (`imrn`): one is taken off the stack (stat 70 minus 1, or the item deleted at 1);
- a unique (quality 7) or set (5) item: a new item of the same row is made (`0x103841d0` / `0x103840c0`, the row and
  item level from the old item, a new `rand()` seed, mode 4 or 2 by the `0x1030cc40` flag). The new item takes the old one's base code (it
  follows an exceptional or elite upgrade), its socketed items, durability and max durability (stat 72/73),
  quantity (70), stat 152 (indestructible), stat 91 and stat 31, and stat 270 becomes stat 437 on it. The old item is
  deleted and the new one placed in the cube and sent to the client.

**`HOTKEY_ItemQuantity`.** Its one code use is the key's handler at `0x102bc140`: in game, it switches D2Client UI
state 6 on or off (through the resolved D2Client functions at `0x105e32a4` and `0x105e32b4`); otherwise it prints the
key's help line. What UI state 6 shows was not traced: it is not a stack split by itself.

## In D2R

- The cubemain output parser (`0x3e5e60`) knows `qty`, `pre`, `suf`, `nor`, `hiq`, `mag`, `set`, `rar`, `uni`, `crf`,
  `tmp`, `eth`, `sock`, `mod`, `uns`, `rem`, `reg` (flag 0x40, keeps quality), `exc`, `eli`, `rep`, `rch`, `lvl`.
  What the executor does with `reg` was not traced.
- The `uniques` cheat (`0x411340`) makes an item through `0x43cd50(game, &params, 0)`: a 0xb8-byte block with the
  player (+0), the game (+0x10), the item level (+0x18), the item id (+0x20), the room (+0x30), the quality (+0x40, 7)
  and the unique row (+0x50, zero-based), two seeds (+0x58, +0x5c). It drops the item on the ground.
- The executor removes an input item with `0x48fa40(game, item)`: a stack is one item to D2R's cube, so a stack of
  Demonic Cubes would go whole (#315's problem).
</content>
</invoke>
