# Inventory hotkeys

core-ui's `hotkeys` part gives the inventory, stash and Cube PD2's mouse binds (#1567):

| Action | Default | What it does |
|---|---|---|
| `drop` | Ctrl + right click | Drops the item (D2R's own drop path), with the stash, Cube, trade or any other panel open |
| `stash` | Shift + right click | Moves the item to the stash, or back to the inventory; only with the stash panel open. A Gems, Materials or Runes tab item goes to its tab cell and a map of the base the Maps tab shows goes onto the Maps tab, as Ctrl + click puts them (#1773, #1774) |
| `cube` | Ctrl + Shift + right click | Moves the item to the Cube (back from the Cube), even with the Cube closed and any other panel open |
| `identify` | Shift + left click | Identifies the item with a Horadric Almanac, a Tome of Identify (one charge, never its last) or a Scroll of Identify in the inventory |

A move that has no room leaves the item where it was. The stash move still needs the stash panel open (no remote stash). Every action plays its game sound: the item drop sound for drop and moves, the identify cursor sound for identify. Shift + left click on an identified item is still D2R's own equip. Host and single player only.

## Rebinding

`<game>\mods\<mod>\d2rloader\config\core-ui-hotkeys.toml` is written on first run. Each line is `action = "modifiers+button"`
(modifiers `shift`, `ctrl`, `alt`; button `left` or `right`; `off` turns an action off). Restart the game after a change.
`drop` must stay `ctrl+right` or be off. The tooltip labels name the default binds only. There is no Options > Controls
entry: the SDK input service binds keys, not mouse chords.

## How

Left clicks arrive as the SDK's item interaction event. D2R sends none for a right click, so the grid widget's right-click
handler (93847 `0x2c3310`) is hooked by signature; every other right click goes to D2R unchanged. Moves and identify are SDK
item transactions (`executeExistingItemTransaction`) on the game thread. The SDK reaches no Gems, Materials or Runes tab, so a stash move of an inventory item those tabs take runs D2R's own Ctrl + click deposit on the UI thread, both found by signature: the gate 93847 `0x15a0b0` (stash open, the item's class in the tabs' table) and the grid's deposit `0x2aaaa0` (sends the tab deposit 0x63), as the grid's left click `0x2c7540` calls them (#1774). A drop blanks D2R's panel-open table around the call so the game takes its inventory-only drop path. Identify sources are Almanac `rid`, tome `ibk` (never its last charge: the SDK cannot set a stack to 0, so D2R's own right-click use spends that one) and scroll `isc`: the first one in the inventory is used.
