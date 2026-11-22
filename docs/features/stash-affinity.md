# Stash affinity and the Maps tab

The Map stash tab ([#1553](https://github.com/synpoox/pd2-resurrected/issues/1553)), after Path of Exile's map tab.

## Tabs

The **Maps** and **Bossing** tabs sit on a row above D2R's five stash tabs; one tab is lit at a time.

- **Maps** shows three tier buttons (Tier I-III), centred, then the selected tier's bases as a centred row of boxes (map
  icons, the name on hover, a gold frame on the selected one). The selected base's maps sit in rows 2-3 above the Horadric
  Cube, which stays usable at its RotW-tab spot; the map currency fills fixed cells in two columns each side of the cube.
  Selecting a base swaps the last base's maps back to storage and this base's onto the tab.
- **Bossing** looks like the RotW Materials tab: a fixed, spaced cell per boss item round the cube, stone beside the cube
  (no spare cells, #1699): a second uber map, or a part past a full stack, goes back to the inventory. An item already
  in one of the dropped cells (here, or the Maps tab's two bottom-right cells) is moved by the tidy pass to its own cell,
  or to the inventory; with a full inventory it stays where it is, still shown and still takeable.
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
| 15 | the Maps tab: rows 0-1 under the buttons stay empty; the shown base's maps in rows 2-3; the map currency in columns 0-1 of rows 4-9 and 8-9 of rows 4-8, a fixed cell per item, stacked to 50 |
| 16, 17, 18 | stored maps of tier 1, 2, 3: one row per base (the order in `plugin/common/stash_home.h`) |
| 19 | Bossing: PD2's uber maps, keys and ingredients the RotW Materials tab does not take, the Torch Fragment; a fixed cell each (`BossingCell` in `stash_home.h`), stackables merged; a second uber map goes back to the inventory |

Map currency: map orbs and ready orbs, Fortify, Dungeon Scarab, Force Mapevent Shard, invader ears. Precedence: Maps, map
currency, Bossing; items the RotW Materials tab takes (Pandemonium keys, organs, essences) stay there.
`tools/tests/test_stash_affinity.py` checks the lists against `misc.txt`.

## Routing

core-ui's `affinity` part, SDK only: a game-thread watch notes those items in the inventory each update; one that is gone from
the inventory and in a stash on the next update was quick-moved (Ctrl+click or the pad), and `executeExistingItemTransaction`
moves the item itself (rolls, sockets and quantity stay; the Shift+Right Click stash hotkey sends a homed item straight to its page, #1724): a map to the Maps tab if its base is shown, else to its base's row
(or anywhere on its tier's storage page when the row is full); a currency to its cell, merged into the stack there (the
merge runs in the inventory: the loader refuses to edit or destroy a shared-stash item; loader 1.3.1 reads quantity 0 for
PD2's stacks, so the counts are stat 70 read off the native items, #1744); a bossing item to its cell, merged the same way. A
sum past 50 goes back to the inventory whole. A hand drop onto the cell's stack is merged the same way; see Hand drops. While
Maps or Bossing is up, a tidy pass (every 25 updates) also routes items put onto those two pages by hand, and moves an item
no rule takes off them to the inventory. A full cell or page, or a missing page (fewer extra tabs), moves it back to the inventory; the log
says why. Status: `core-ui` console command, `affinity:` and `stash tabs:` counters.

Hand drops ([#1726](https://github.com/synpoox/pd2-resurrected/issues/1726)): while the Maps or Bossing tab is open, the
inventory padlock's message hook (`inventory_lock.cpp`, by signature) refuses the local player's 0x18 (put the cursor item on a
stash cell, page field 4) and 0x1f (swap it with a stored item on the stash, page field 4; the cursor item goes to the clicked cell at +0x11, packed `y << 16 | x` like the stored item's cell at +0x0d; read as two plain ints until #1744, which refused a same-item drop on its own cell) unless `stash_home.h`'s `AtHome` says the
item belongs on that page and cell (a map of the shown base also in the map cells). Any other item, or a currency or boss item in
another's cell, stays on the cursor. The message holds no shared page number, so the open tab (`stash_affinity::ViewPage`) names
it. The stack message 0x21 (cursor item onto a stored one; ids +1 and +5, grid +0x0d, packed cell +0x11; the client
sends it whenever the two items stack, 0x375960) does not work natively on these pages: D2R's handler 0x4c1940 looks the
target up by cell (0x472590, 0x474220) in the player's current inventory (0x34a360), which does not hold the Maps or
Bossing page's stack, and logs `No item found at page: 4` (#1744). So while either tab is open the hook consumes a 0x21
onto the stash (grid 4) and queues it for the game thread, which merges the cursor item into the stack through the same
SDK merge when it is the same stackable item on its own cell of the open page. Log: `stash tabs: stack message 0x21 onto
page .. cell x,y: the plugin stacks it`, then `affinity: .. merged with the stack ..`. Otherwise, or when a step fails (a
full inventory, a sum past 50), the item stays on the cursor: it is destroyed only after the stack holds the sum. The tidy pass stays as the net for what skips the message (the list forms 0x22 and 0x60 are not checked). Log: `stash tabs:
refused message 0x.., not the item's own cell`.

Red cells ([#1745](https://github.com/synpoox/pd2-resurrected/issues/1745)): the client asks the same rule first. Two
D2Common cell tests are hooked by signature (`inventory_lock.cpp`): 93847 `0x38a250` (the cursor item's cells are free: green
tint, the click sends 0x18) and `0x38a5d0` (the one stored item under it: the swap highlight, the click sends 0x1f). While the
Maps or Bossing tab is open, both answer "no" for the cursor item over a stash cell that is not its own, so the cell shows red
and the click sends nothing. The message filter above stays as the net. Status: `red cell tests` in the inventory lock counters.

Limits: tabs 15-19 are taken as they are. Items a player already keeps on 16-18 stay and share the space; on 15 and 19 the
tidy pass moves them to the inventory (a migration for saves with more extra tabs is a later slice); a reserved page's free cells are found from item corners only, so a bigger
hand-placed item there can send a map back. A hand move whose pick-up and put-down land in one game update (a lag spike), or
another plugin's inventory-to-stash move, is routed like a quick-move. The base boxes show no map count: that needs a loader
widget service with `setText` (D2RLoader 1.4.0).

Not yet: a chat line when a page is full, TCP/IP clients (routing and the base swap run in single player and on the host).
