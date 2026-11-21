# Stash affinity

Part of the Map stash tab ([#1553](https://github.com/synpoox/pd2-resurrected/issues/1553)): with the stash open, a quick-move
(Ctrl+click, or the pad's quick-move) of a map, a bossing item or a map material from the inventory files it on its reserved
shared stash tab, whichever tab is on show. Hand moves (through the cursor) and withdrawals stay as D2R does them.

## Pages

Fixed indices after 10 general Loader tabs, so a bigger `add_shared_tabs` adds general tabs after them and never shifts
them. The mod sets `[d2rcore.stash] add_shared_tabs = 15` (the Pyre Stash Search entry's `loader_settings` in
`companions/manifest.json`; a player's own value is kept).

| Shared tab (zero-based) | Holds |
|---|---|
| 0-4 | D2R's shared tabs |
| 5-14 | general Loader tabs |
| 15, 16, 17 | Maps tier 1, 2, 3: one row per base (the order in `plugin/common/stash_home.h`), 10 maps per row |
| 18 | Bossing: PD2's uber maps, keys and ingredients the RotW Materials tab does not take, the Torch Fragment |
| 19 | Map Materials: map orbs and ready orbs, Fortify, Dungeon Scarab, Force Mapevent Shard, invader ears |

Precedence: Maps, Map Materials, Bossing; items the RotW Materials tab takes (Pandemonium keys, organs, essences) stay there.
`tools/tests/test_stash_affinity.py` checks the lists against `misc.txt`.

## How it works

core-ui's `affinity` part, SDK only: a game-thread watch notes those items in the inventory each update; one that is gone from
the inventory and in a stash on the next update was quick-moved, and `executeExistingItemTransaction` moves the item itself
(rolls, sockets and quantity stay) to its page, a map to the first free cell of its base's row. A full row, a full page or a
missing page (fewer extra tabs) moves it back to the inventory; the log says why. Status: `core-ui` console command,
`affinity:` counters.

Limits: tabs 15-19 are taken as they are, so items a player already keeps there stay and share the space (a migration
for saves with more extra tabs is a later slice); a reserved page's free cells are found from item corners only, so a bigger
hand-placed item there can send a map back to the inventory. A hand move whose pick-up and put-down land in one game update
(a lag spike), or another plugin's inventory-to-stash move, is routed like a quick-move. `getLocalPlayer` works on the game
thread here (Slot2, 2026-11-21).

Not yet: the Maps view (tier and base selectors with counts, after the PoE map stash), tab names, a chat line when a page is
full, TCP/IP clients (routing runs in single player and on the host).
