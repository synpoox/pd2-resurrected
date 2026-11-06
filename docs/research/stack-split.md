# [#316](https://github.com/synpoox/pd2-resurrected/issues/316): PD2's stack split

With a stack of N > 1 held on the cursor, Ctrl+left-click puts one item down and leaves N-1 on the cursor. This note
traces PD2's version and describes the port (`plugin/core-ui/stack_split.cpp`).
Findings only: no PD2 or Blizzard file content is in this repo.

## Method

Ghidra 12.2 headless, read-only, on the PD2 project (`ProjectDiablo.dll`, 1.13c `D2Game.dll`, `D2Client.dll`,
`D2Common.dll`). The Ghidra copy of `ProjectDiablo.dll` is not byte-identical to the one on disk (TimeDateStamp
0x6aeb779e): around 0x103xxxxx, Ghidra address = disk address - 0x70. Below are disk VAs, with the Ghidra VA in
brackets where it differs. `GetKeyState`/`GetAsyncKeyState` callers and the strings "split", "stack" and "quantity" lead
nowhere; `HOTKEY_ItemQuantity` is the display toggle ([ir6-cube-rules](ir6-cube-rules.md)).

## 1. Client: PD2's own WndProc, packet 0x22

- `0x102eb900`: PD2's subclassed game WndProc. On WM_LBUTTONDOWN it calls the click handler, then `CallWindowProcA`.
- `0x102bff00` [`0x102bfef0`]: the left-click handler. It takes the split path when an item is on the cursor
  (`0x10324f80`, D2Client's cursor-item getter), the message's key flags hold MK_CONTROL (`& 8`), the item is not a
  tome (`tbk `, `ibk `), and it passes a stack check (`0x1037def0`: item type 0x14 or 0x4a, or quantity >= 2 and
  `0x1037dea0`).
- It sends an 11-byte packet 0x22 (`0x102c02bc`-`0x102c02ef`): cursor item unit id (dword), cell x and y (words, from
  `0x102ebaf0`, which tracks the hovered cell and page on each mouse move), page (byte). It then plays the item's drop
  sound. `0x103a1d40` sends through D2Client's 1.13c send function.
- Pages from `0x102ebaf0`: 0 inventory, 3 cube, 4 stash, 6 another PD2 grid (not identified). The client skips the split
  when no cell is hovered, or when another panel is open on a page other than 0, except a cube case (not decoded).

## 2. Server: D2Game's packet table, slot 0x22

- PD2 replaces D2Game's client packet table entry for 0x22 (table at D2Game 0x6fd1a078, slot RVA 0xfa188) through its
  patch-record table (record `0x1049e784`).
- `0x10394f60` [`0x10394ef0`]: the 0x22 handler. It validates (`0x103a2bd0` -> D2Game+0xcce20), finds the item by id,
  reads its quantity (stat 70), maps the page to a grid (4 -> 0xc, 3 -> 9, else 0) and checks the cell is free.
- Free cell, quantity > 1: it makes a new item of the same base through D2Game+0x11490 (1.13c item creation from
  parameters: class, item level, seed, quality, difficulty), places it at x, y on that page, adds it to the game, sets
  its quantity to 1 and the held stack's to N-1, and updates the client.
- Cell holding a same-class stack below its max: one item moves onto that stack.

## 3. The port

No D2R hook. The PluginSDK has no mouse event, and `ItemInteractionService` skips cursor paths, so:

1. A UI-thread watch reads the cursor each root-widget update (`InventoryService::getCursorItem`). It starts on the
   first `LocalPlayerReady`, because the SDK refuses UI work queued during plugin load.
2. When a stack of N > 1 leaves the cursor with Ctrl held (`GetAsyncKeyState`), it queues a game-thread job.
3. The job finds the stack again in a stored grid (inventory, cube, personal or shared stash, custom page) by code and
   unit id. The UI thread's handle is the client's unit: on the game thread it is stale, and its seeds read 0. If the
   stack is there whole, `ItemService::splitStack` takes N-1 back to the cursor.

D2R places the stack, so the cell and panel are D2R's own. Differences from PD2: the item left down is the original
(PD2 makes a new one); a Ctrl+click on a same-type stack merges the whole held stack (D2R's put-down), where PD2 adds
one; a TCP/IP client gets no split (no game thread).

## 4. Game check

Slot 8, Hell, level 90 Paladin, inventory:

- El Rune stack of 8: Ctrl+left-click on an empty cell left 1 there and 7 on the cursor; a second one left 1 and 6; a
  plain click put the 6 down. Tooltips: 1, 1, 6.
- Perfect Amethyst stack of 3: Ctrl+left-click left 1 and 2 on the cursor; a plain click put the 2 down. Tooltips: 1, 2.
- After Save and Exit and a reload: amethysts 1 and 2, El runes 8 (the three stacks merged by hand before saving).
- Not checked: orbs, keys, organs, maps, the stash and cube, D2R's rune stash tab.

The test harness answers the game's key-state reads only in the game image and `D2RCore.dll`. Ctrl reached this
plugin only after the harness also swapped imports in the other plugin DLLs (d2r-test-harness, local change).
