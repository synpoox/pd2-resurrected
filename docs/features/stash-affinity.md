# Stash affinity and the Maps tab

The Map stash tab ([#1553](https://github.com/synpoox/pd2-resurrected/issues/1553)), after Path of Exile's map tab.

## Tabs

Arrows at both ends of the stash's tab row page it: page 1 holds D2R's five tabs, page 2 the **Maps** and **Bossing** tabs.

- **Maps** shows three tier buttons (Tier I-III) over the grid's top row, then the selected tier's base buttons (map icons,
  the name on hover) over the second row. The selected base's maps sit above the Horadric Cube, which stays usable at its
  RotW-tab spot; the map currency fills fixed cells left and right of the cube. Selecting a base swaps the last base's maps
  back to storage and this base's onto the tab.
- **Bossing** shows its shared page.
- While Maps or Bossing is up, the stock page arrows are hidden, so the page stays put; a stock tab returns to D2R's tabs.

core-ui's `tabs` part (`stash_tabs.cpp`) runs the buttons; the widgets are in `bankexpansionlayouthd.json`, all hidden until it
shows them. It opens a page with the stash's own messages (Shared tab, the left arrow to page 1, the right arrow to the page).

## Pages

Fixed indices after 10 general Loader tabs, so a bigger `add_shared_tabs` adds general tabs after them and never shifts
them. The mod sets `[d2rcore.stash] add_shared_tabs = 15` (the Pyre Stash Search entry's `loader_settings` in
`companions/manifest.json`; a player's own value is kept).

| Shared tab (zero-based) | Holds |
|---|---|
| 0-4 | D2R's shared tabs |
| 5-14 | general Loader tabs |
| 15 | the Maps tab: rows 0-1 under the buttons stay empty; the shown base's maps in rows 2-4 and row 5 beside the cube; the map currency in columns 0-2 and 7-9 of rows 6-9, a fixed cell per item, stacked to 50 |
| 16, 17, 18 | stored maps of tier 1, 2, 3: one row per base (the order in `plugin/common/stash_home.h`) |
| 19 | Bossing: PD2's uber maps, keys and ingredients the RotW Materials tab does not take, the Torch Fragment |

Map currency: map orbs and ready orbs, Fortify, Dungeon Scarab, Force Mapevent Shard, invader ears. Precedence: Maps, map
currency, Bossing; items the RotW Materials tab takes (Pandemonium keys, organs, essences) stay there.
`tools/tests/test_stash_affinity.py` checks the lists against `misc.txt`.

## Routing

core-ui's `affinity` part, SDK only: a game-thread watch notes those items in the inventory each update; one that is gone from
the inventory and in a stash on the next update was quick-moved (Ctrl+click or the pad), and `executeExistingItemTransaction`
moves the item itself (rolls, sockets and quantity stay): a map to the Maps tab if its base is shown, else to its base's row
(or anywhere on its tier's storage page when the row is full); a currency to its cell, merged into the stack there; a bossing
item to the Bossing page. A full cell or page, or a missing page (fewer extra tabs), moves it back to the inventory; the log
says why. Status: `core-ui` console command, `affinity:` and `stash tabs:` counters.

Limits: tabs 15-19 are taken as they are, so items a player already keeps there stay and share the space (a migration
for saves with more extra tabs is a later slice); a reserved page's free cells are found from item corners only, so a bigger
hand-placed item there can send a map back. A hand move whose pick-up and put-down land in one game update (a lag spike), or
another plugin's inventory-to-stash move, is routed like a quick-move. Counts on the base buttons need a loader widget service
with `setText` (D2RLoader 1.4.0); 1.3.1 shows none.

Not yet: a chat line when a page is full, TCP/IP clients (routing and the base swap run in single player and on the host).
