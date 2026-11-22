# Stash affinity and the Maps tab

The Map stash tab ([#1553](https://github.com/synpoox/pd2-resurrected/issues/1553)), after Path of Exile's map tab.

## Tabs

The **Maps** and **Bossing** tabs sit on a row above D2R's five stash tabs; one tab is lit at a time.

- **Maps** shows three tier buttons (Tier I-III), centred, then the selected tier's bases as a centred row of boxes (map
  icons, the name on hover, a gold frame on the selected one). The selected base's maps sit in rows 2-3 above the Horadric
  Cube, which stays usable at its RotW-tab spot; the map currency fills fixed cells in two columns each side of the cube.
  On the left each plain orb has its ready orb beside it, a pair per row, then Scour and Upgrade Map; on the right
  Fortify, Dungeon Scarab and Force Mapevent Shard, then the invader ears (#1776).
  Selecting a base swaps the last base's maps back to storage and this base's onto the tab.
- **Bossing** looks like the RotW Materials tab: a fixed, spaced cell per boss item round the cube, stone beside the cube
  (no spare cells, #1699): a second uber map, or a part past a full stack, goes back to the inventory. An item already
  in one of the dropped cells (here, or the Maps tab's two bottom-right cells) is moved by the tidy pass to its own cell,
  or to the inventory; with a full inventory it stays where it is, still shown and still takeable. PD2's Uber Ancients
  map and keys (`uba`, `ubaa`-`ubac`) have no cell since #1776: they go anywhere like any other item, and one left on
  the Bossing page goes back to the inventory.
- While Maps or Bossing is up, the stock page arrows are hidden, so the page stays put; a stock tab returns to D2R's tabs.

core-ui's `tabs` part (`stash_tabs.cpp`) runs the buttons; the widgets are in `bankexpansionlayouthd.json`, all hidden until it
shows them. A tab click switches the stash to Shared through D2R's tab-bar select (found by signature; no SDK service does
it), then the stash's page arrows open the page.
The tab's page is published to the drop filter (`ViewPage`) only once every arrow was sent (log `stash tabs: shared page <n> on show`);
an arrow that fails is retried for 30 frames, then the page stays unknown (no filter) and a warning is logged. The stash's page count
is read from `add_shared_tabs` in `[d2rcore.stash]` of the mod's `d2rloader.toml`, else of the game-wide `<game>/d2rloader/config/d2rloader.toml`, else the loader default 0 (5 + n; only when no file can be read is there no limit): with fewer than 20 pages the Bossing tab is hidden (fewer than 16: Maps too) and the log says so.
The SDK has no read of the shown page, so a page change that sends no arrow is still not seen.

## Pages

Fixed indices after 10 general Loader tabs, so a bigger `add_shared_tabs` adds general tabs after them and never shifts
them. The mod sets `[d2rcore.stash] add_shared_tabs = 15` (the Pyre Stash Search entry's `loader_settings` in
`companions/manifest.json`; a player's own value is kept).

| Shared tab (zero-based) | Holds |
|---|---|
| 0-4 | D2R's shared tabs |
| 5-14 | general Loader tabs |
| 15 | the Maps tab: rows 0-1 under the buttons stay empty; the shown base's maps in rows 2-3; the map currency in columns 0-1 of rows 4-9 and 8-9 of rows 4-8, a fixed cell per item, stacked to 255 (#1763) |
| 16, 17, 18 | stored maps of tier 1, 2, 3: one row per base (the order in `plugin/common/stash_home.h`) |
| 19 | Bossing: PD2's uber maps, keys and ingredients the RotW Materials tab does not take (but the Uber Ancients ones), the Torch Fragment; a fixed cell each (`BossingCell` in `stash_home.h`), stackables merged; a second uber map goes back to the inventory |

Map currency: map orbs and ready orbs, Fortify, Dungeon Scarab, Force Mapevent Shard, invader ears. Precedence: Maps, map
currency, Bossing; items the RotW Materials tab takes (Pandemonium keys, organs, essences) stay there.
`tools/tests/test_stash_affinity.py` checks the lists against `misc.txt`.

## Routing

core-ui's `affinity` part, SDK only: a game-thread watch notes those items in the inventory each update; one that is gone from
the inventory and in a stash on the next update was quick-moved (Ctrl+click or the pad), and `executeExistingItemTransaction`
moves the item itself (rolls, sockets and quantity stay; the Shift+Right Click stash hotkey sends a homed item straight to its page, #1724, and a map of the shown base onto the Maps page, #1773: on its storage row it counted as home and stayed off the Maps view): a map merges into a stack below 50 on its page first when both are normal quality with equal stats (#1759, the #1326 rule), else goes to the Maps tab if its base is shown, else to its base's row
(or anywhere on its tier's storage page when the row is full); a currency to its cell, merged into the stack there (the
merge runs in the inventory: the loader refuses to edit or destroy a shared-stash item; loader 1.3.1 reads quantity 0 for
PD2's stacks, so the counts are stat 70 read off the native items, #1744); a bossing item to its cell, merged the same way. A
sum past 255 tops the stack up to 255 and the rest comes back to the inventory ([#1771](https://github.com/synpoox/pd2-resurrected/issues/1771)); a map sum past 50 goes back whole. A hand drop onto the cell's stack is merged the same way; see Hand drops. While
Maps or Bossing is up, a tidy pass (every 25 updates) also routes items put onto those two pages by hand, and moves an item
no rule takes off them to the inventory. A full cell or page, or a missing page (fewer extra tabs), moves it back to the inventory; the log
says why. Status: `core-ui` console command, `affinity:` and `stash tabs:` counters.

Hand drops ([#1726](https://github.com/synpoox/pd2-resurrected/issues/1726)): while the Maps or Bossing tab is open, the
inventory padlock's message hook (`inventory_lock.cpp`, by signature) refuses the local player's 0x18 (put the cursor item on a
stash cell, page field 4) and 0x1f (swap it with a stored item on the stash, page field 4; the cursor item goes to the clicked cell at +0x11, packed `y << 16 | x` like the stored item's cell at +0x0d; read as two plain ints until #1744, which refused a same-item drop on its own cell) unless `stash_home.h`'s `AtHome` says the
item belongs on that page and cell (a map of the shown base also in the map cells). Any other item, or a currency or boss item in
another's cell, stays on the cursor. These messages hold no shared page number, so the open tab (`stash_affinity::ViewPage`) names
it (the shared-page messages below name it themselves). The stack message 0x21 (cursor item onto a stored one; ids +1 and +5, grid +0x0d, packed cell +0x11; the client
sends it whenever the two items stack, 0x375960) does not work natively on these pages: D2R's handler 0x4c1940 looks the
target up by cell (0x472590, 0x474220) in the player's current inventory (0x34a360), which does not hold the Maps or
Bossing page's stack, and logs `No item found at page: 4` (#1744). So while either tab is open the hook consumes a 0x21
onto the stash (grid 4) and queues it for the game thread, which merges the cursor item into the stack through the same
SDK merge when it is the same stackable item on its own cell of the open page. Log: `stash tabs: stack message 0x21 onto
page .. cell x,y: the plugin stacks it`, then `affinity: .. merged with the stack ..`. Otherwise, or when a step fails (a
full inventory), the item stays on the cursor: it is destroyed only after the stack holds the sum. A sum past 255 tops the stack up to 255 and leaves the rest on the cursor item, as D2R's own 0x4c1940 does ([#1758](https://github.com/synpoox/pd2-resurrected/issues/1758)).
The counts are read first (stat 70 through `editNativeItem`, which also reads a shared-stash item): a stack already at 255 refuses with no move, as D2R's
handler refuses that message (0x4f2a80 returns 2 only below the item's maxstack; the refusal only writes telemetry). Before #1758 the
stack went to the inventory and back first, and the cursor item was left invisible on the client while the server still
held it (`Insert overlaps with item id .. mode: 4` on the next pick-up). The client sends 0x21 whenever its stack test
(0x375960) passes, full or not (+9 is 1 when the sum fits), and marks the cursor item pending (0x1c6ac0, state 0x36),
cleared by an item packet (0x1c6a40); while pending, the client ignores clicks with it (0x2c81d0 returns on 0x1c7360). A merge's stack move also takes over the client's cursor: the stack's 0x9d action 5 (0x1c60a0) makes it the cursor item (0x389990), so after a top-up the rest was not drawn until a click in the inventory ([#1762](https://github.com/synpoox/pd2-resurrected/issues/1762)). So every consumed 0x21 ends, after the last move, with `ResendCursor`: an `editItem` of the cursor item to its own count, which sends it again (0x9d action 0x15, 0x1c6540: the client rebuilds it as the cursor item). Log: `affinity: cursor item .. sent to the client again: result 0`. Maps stack only when both are normal quality with equal non-quantity stats
(`StackAllowed`, which shares `MapStatsRefusal` with the tidy pass's `MapRefusal`), so a rare map is never merged into a normal stack. The tidy pass stays as the net for what skips the message (the list forms 0x22 and 0x60 are not checked). Log: `stash tabs:
refused message 0x.., not the item's own cell`.

Stacks of 255 ([#1763](https://github.com/synpoox/pd2-resurrected/issues/1763)): map currency and boss parts stack to 255 on their cell (`StashMax` in `stash_home.h`); maps keep 50, since `splitStack` remakes a map's stats on the piece it takes out. D2R's GetMaxStack (93847 `0x3719e0`: misc.txt `maxstack` + stat 254, clamped to 511, the 9-bit saved quantity) still caps inventory stacks at 50: pick-up, vendor and merge stacking all test it. Taking a stack out gives 50: a stack past 50 that is new in the inventory (Ctrl+click out, or a hand pick-up put down) has 50 split onto the cursor and the rest is routed home, topping the cell's stack up (the SDK moves no cursor item, so they stay there). `splitStack` refuses a source past its maxstack (result 3), so the stack is set to 2, 1 is split off, and the counts are set to 50 and the rest. `editItem` refuses a count past maxstack too, so a count past 50 is written natively (`SetUnitStat` by signature through `editNativeItem`; the following SDK move publishes it). It runs only while the Maps or Bossing tab is open (a 50 on the cursor out of a panel is dropped by the next click). After the split the source is written first (to the rest), then the piece (to 50). A failed step puts the counts back from what it reads (never an assumed count), warns, and keeps the stack in the inventory; then the two items are read back and must add up to the stack, else an ERROR line names both counts. A busy cursor sends it all home; a failed split, or a rest that cannot go home, stays in the inventory. Display only: a count past 50 written natively reaches the client on the item's next move, so a merge's cursor rest or a top-up rest left in the inventory past 50 shows its old count until it is picked up or moved. Log: `affinity: .. taken out of the stash: 50 to the cursor (split result 0, counts add up)`. A click on such a stack is the plugin's while the tab is open: a take off a shared page is 0x46 (0x0d bytes, the item +1 and the page owner +5; client `0x2c7080`, server `0x4c6480`), and the owner must be the Maps or Bossing page ([#1772](https://github.com/synpoox/pd2-resurrected/issues/1772)): the client cannot draw more than 50 on the cursor, so `stash_affinity::TakeOut` moves the stack to the inventory and the next pass splits it as above (log `stash tabs: take message 0x46 of a stack past 50`). The source's move home made it the client's cursor item and put it down (see Hand drops), so the piece was held but not drawn; the cursor item is sent again after that move (`ResendCursor`). A full inventory leaves the stack in its cell: it is moved onto its own cell (else anywhere on its page, and the tidy pass routes it home), so the client gets an item packet for the stack the consumed take left pending. Ctrl+click out of a RotW tab (Gems, Materials, Runes) takes one as a new single into a free inventory cell (D2R's own take); the click is caught in the stack tab's Activate hook (pad_binds.cpp), and over the next 50 game updates a new inventory item of the slot's class is merged onto the first inventory stack of it that `editItem` lets take it (maxstack 50), then destroyed; the stack's count is read back before the destroy, and a failed destroy puts it back ([#1775](https://github.com/synpoox/pd2-resurrected/issues/1775)). Log: `padbinds: .. taken out of the stack tab topped up the inventory stack`. The RotW tabs (Gems, Materials, Runes) hold 255: the mod's `d2rloader.toml` sets D2RCore's `[d2rcore.stash] set_materials_limit = 255` (the Pyre Stash Search entry's `loader_settings`, and `required_settings` in `release.toml`); core-rules reads that limit from `[d2rcore.stash]` of the mod's `d2rloader.toml`, else the game-wide one (default 99, clamped 1-255; log `stash stacks: set_materials_limit = N from ...`) and refuses a deposit past it, since D2RCore's own add drops the items past its limit; 255 is the 8-bit count's ceiling ([#1767](https://github.com/synpoox/pd2-resurrected/issues/1767)).

Shared pages ([#1749](https://github.com/synpoox/pd2-resurrected/issues/1749)): the Maps and Bossing tabs are shared pages,
whose grid belongs to another unit, so the client does not send 0x18 or 0x1f there. A swap is 0x14 (client `0x2c81d0`, server
`0x4b4de0`; 0x15 bytes: the cursor item +1, the stored item +5, the page owner's unit id +9, the cells +0x0d and +0x11 as in
0x1f) and a put is 0x45 (client `0x15f8b0`, server `0x4c5570`; 0x0d bytes: the cursor item +1, the owner +5, the cell +9
packed). Before #1749 neither was filtered: a wrong put landed and the tidy pass moved it back, and a wrong swap put the stored
item on the server's cursor, which the client never drew (it showed after a rejoin). The filter now refuses both by the same
rule. The swap test needs only the cursor item, so a stored item the lookup cannot find does not let the swap through.

Page from the message ([#1756](https://github.com/synpoox/pd2-resurrected/issues/1756)): 0x14 (+9), 0x45 (+5) and 0x55 (+5)
name the page's owner unit, so the filter judges the page that unit is, not the tab open when the server handles the message
(a tab switch or a closed stash before the tick no longer lets a wrong move through, and a reserved page reached by the stock
arrows is filtered too). D2R keeps each shared page as a unit (state 186 `sharedstash`), listed on the player's inventory at
+0x68 in tab order (`0x385860` appends `{owner id +0, remove-only +8, active +9, advanced stash +0xa, next +0x10}`; the
handlers find the owner with `0x388e10`). The plugin counts the nodes that are not the RotW advanced stash (state 209,
`0x52f8d0`), the SDK's normal tab number; no SDK call maps a unit to its tab. An owner not in the list, or 0x18 and 0x1f
(no owner), take the open tab. The refusal log names both (`page 15, open tab 15`).

Quick-moves ([#1757](https://github.com/synpoox/pd2-resurrected/issues/1757)): a Ctrl+click or the pad's quick-move between
the inventory and a shared page is 0x55 (client `0x15f8b0`, server `0x4c7690`; 0x1d bytes: the item +1, the owner +5, from
grid +9 and packed cell +0x0d, to grid +0x11 and packed cell +0x15, +0x19 0 into the owner's grid, 1 out of it). The client
picks the first free cell (`0x3865b0`, not the red cells' tests), so a map, currency or boss item lands anywhere and the
watch routes it as before; only an item with no home is refused (it used to show for a moment, then the tidy pass moved it
back, #1737). The plugin's own moves (the watch, the Shift+Right Click hotkey) are SDK transactions, not client messages, so the
filter never sees them.

Refusals resend the item ([#1766](https://github.com/synpoox/pd2-resurrected/issues/1766)): the client moves nothing before
the server answers, but it marks the item pending (`0x2c81d0` or `0x15f8b0` call `0x1c6ac0`, state 0x36) and ignores stack
and swap clicks with it until an item packet for it arrives (`0x1c6a40`). D2R's own failure branches send none, so every
refusal queues `stash_affinity::Resend` for the game thread: the same `ResendCursor` as a consumed 0x21, which also finds a
quick-moved item still in the inventory or Cube, and edits an item that does not stack to its own identified state. Log:
`stash tabs: refused message 0x.. .. ; the item is sent again`, then `affinity: cursor item .. sent to the client again: result 0`.

Red cells ([#1745](https://github.com/synpoox/pd2-resurrected/issues/1745)): the client asks the same rule first. Two
D2Common cell tests are hooked by signature (`inventory_lock.cpp`): 93847 `0x38a250` (the cursor item's cells are free: green
tint, the click sends a put: 0x18, or 0x45 on a shared page) and `0x38a5d0` (the one stored item under it: the swap
highlight, the click sends a swap: 0x1f, or 0x14 on a shared page). While the
Maps or Bossing tab is open, both answer "no" for the cursor item over a stash cell that is not its own, so the cell shows red
and the click sends nothing. The message filter above stays as the net. Status: `red cell tests` in the inventory lock counters.

Limits: tabs 15-19 are taken as they are. Items a player already keeps on 16-18 stay and share the space; on 15 and 19 the
tidy pass moves them to the inventory (a migration for saves with more extra tabs is a later slice); a reserved page's free cells are found from item corners only, so a bigger
hand-placed item there can send a map back. A hand move whose pick-up and put-down land in one game update (a lag spike), or
another plugin's inventory-to-stash move, is routed like a quick-move. The base boxes show no map count: that needs a loader
widget service with `setText` (D2RLoader 1.4.0).

Not yet: a chat line when a page is full, TCP/IP clients (routing and the base swap run in single player and on the host).
