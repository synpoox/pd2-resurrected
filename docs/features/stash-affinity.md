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
PD2's stacks, so there a second stack goes back to the inventory instead); a bossing item to its cell. While
Maps or Bossing is up, a tidy pass (every 25 updates) also routes items put onto those two pages by hand, and moves an item
no rule takes off them to the inventory. A full cell or page, or a missing page (fewer extra tabs), moves it back to the inventory; the log
says why. Status: `core-ui` console command, `affinity:` and `stash tabs:` counters.

Limits: tabs 15-19 are taken as they are. Items a player already keeps on 16-18 stay and share the space; on 15 and 19 the
tidy pass moves them to the inventory (a migration for saves with more extra tabs is a later slice); a reserved page's free cells are found from item corners only, so a bigger
hand-placed item there can send a map back. A hand move whose pick-up and put-down land in one game update (a lag spike), or
another plugin's inventory-to-stash move, is routed like a quick-move. The base boxes show no map count: that needs a loader
widget service with `setText` (D2RLoader 1.4.0).

Not yet: a chat line when a page is full, TCP/IP clients (routing and the base swap run in single player and on the host).
