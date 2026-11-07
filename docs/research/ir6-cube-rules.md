# [IR6](https://github.com/synpoox/pd2-resurrected/issues/241): cube rules and stacking in ProjectDiablo.dll

Work item [IR6](https://github.com/synpoox/pd2-resurrected/issues/241) ([#241](https://github.com/synpoox/pd2-resurrected/issues/241), [item-rules.md](../item-rules.md)): the
demonic cube reroll (op 31) and corruption in one press. The stack split left [IR6](https://github.com/synpoox/pd2-resurrected/issues/241) (below). Cube recipes that
count a stack by its quantity (a stack of 3 El runes making Eld) are out of [IR6](https://github.com/synpoox/pd2-resurrected/issues/241): [#315](https://github.com/synpoox/pd2-resurrected/issues/315).
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
  (op 31 reads it as a choice between two item-make modes); [P9](https://github.com/synpoox/pd2-resurrected/issues/56)'s merged rows already mark these items.
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
key's help line. The key's string is "Show Item Quantity" (`patchstring.tbl`): a display toggle, not a stack split.
PD2's split is Ctrl+left-click with a stack held, one item per click (owner); it moved to
[#316](https://github.com/synpoox/pd2-resurrected/issues/316).

## In D2R

- The cubemain output parser (`0x3e5e60`) knows `qty`, `pre`, `suf`, `nor`, `hiq`, `mag`, `set`, `rar`, `uni`, `crf`,
  `tmp`, `eth`, `sock`, `mod`, `uns`, `rem`, `reg` (flag 0x40, keeps quality), `exc`, `eli`, `rep`, `rch`, `lvl`.
  `reg` is the remake (below).
- The `uniques` cheat (`0x411340`) makes an item through `0x43cd50(game, &params, 0)`: a 0xb8-byte block with the
  player (+0), the game (+0x10), the item level (+0x18), the item id (+0x20), the room (+0x30), the quality (+0x40, 7)
  and the unique row (+0x50, zero-based), two seeds (+0x58, +0x5c). It drops the item on the ground.
- The `uniques` cheat path is not the cube's: in the executor (`0x5269c0`, at `0x52748e`) a `usetype` output with
  `reg` (flag 0x40) takes the input's quality (`0x36cf60`), unique or set row plus 1 (`0x36ce70`, into +0x50) and item
  level (`0x36ccf0`). The unique (`0x443910`) and expansion set (`0x58c2f0`) makers honour that 1-based row when the
  base code matches. The unique maker skips rows already made this game (bits at game +0x3638); PD2's op 31 maker
  (`0x103841d0`) clears that bit for the remake and restores it after (`0x1037ff00` / `0x1037ff30`, game +0x1b24).
- Output n's block is 0x5b bytes from record +0x5a. An output's `qty` on a stackable item sets its quantity; with flag 2
  (`sock`) it sets that many sockets (`0x375560`); flag 4 (`eth`) makes it ethereal.
- `cheat spawn` reads at most 3 characters of the code, so PD2's 4-character codes (`imrn`, `cwss`) cannot be spawned
  ([#323](https://github.com/synpoox/pd2-resurrected/issues/323) patches it). It takes `qty <n>` for a stack.
- The PluginSDK item services do the reroll without these internals (core-rules): `executeTransaction` takes one
  Demonic Cube and the old item and makes the new one with `AllowDuplicateUnique`. It returns `Busy` when called from
  the cube executor, so the work is queued with `ThreadService::runOnGameThread`. It cannot move socketed items
  (`SocketedItemPolicy` is reject or destroy), so core-rules moves them natively ([#325](https://github.com/synpoox/pd2-resurrected/issues/325)):
  D2R's socket insert `0x46eb00` places a socketable with `0x3856e0` and links its stat list (`0x2f81a0`), but also
  applies its gems.txt mods (`0x475140`), which a moved gem already has; PD2's `0x3669b0` only places them (mode 6)
  and flags them for sending. `InventoryService::getLocalPlayer` is for the UI thread, and no service
  names the player who pressed, so the executor passes the pressing unit's id to `runOnUiThread`, which checks it
  against the client's own player (`ClientPlayer(LocalPlayerSlot())`) before queueing the transaction.
- A cubemain record keeps its `enabled` byte at +0 and the search skips a row with 0, so the merge ships op-31 rows
  off and core-rules sets the byte after each `DataTablesLoadedEvent` (the SDK's table views are read-only). The
  data-table getter `0x300a90` takes data flags 0-3 and returns null for a bank that is not loaded.
- `executeTransaction` rejects an output `quantity` on an item that does not stack (`InvalidArgument`, nothing
  taken), and `ItemInfo.quantity` is 1 for such an item, the same as a stack of one. core-rules passes the quantity
  only when `GameRuleService::getTotalMaxStack` reports more than 1 (game check, Goblin Toe, 2026-11-05).
- The executor removes an input item with `0x48fa40(game, item)`: a stack is one item to D2R's cube, so a stack of
  Demonic Cubes would go whole ([#315](https://github.com/synpoox/pd2-resurrected/issues/315)'s problem).
</content>
</invoke>
