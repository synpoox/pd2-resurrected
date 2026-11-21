# Inventory hotkeys

core-ui's `hotkeys` part gives the inventory, stash and Cube PD2's mouse binds (#1567):

| Action | Default | What it does |
|---|---|---|
| `drop` | Ctrl + right click | Drops the item (D2R's own drop path), only from the inventory with no other panel open |
| `stash` | Shift + right click | Moves the item to the stash, or back to the inventory; only with the stash panel open |
| `cube` | Ctrl + Shift + right click | Moves the item to the Cube (back from the Cube); only with the Cube open |
| `identify` | Shift + left click | Identifies the item with one charge of an Identify tome in the inventory |

A move that has no room leaves the item where it was. With a panel closed that a bind needs, the click does nothing (no remote stash, no Cube move from afar, no drop while a stash, Cube or vendor panel is open). Shift + left click on an identified item is still D2R's own equip. Host and single player only.

## Rebinding

`<game>\mods\<mod>\d2rloader\config\core-ui-hotkeys.toml` is written on first run. Each line is `action = "modifiers+button"`
(modifiers `shift`, `ctrl`, `alt`; button `left` or `right`; `off` turns an action off). Restart the game after a change.
`drop` must stay `ctrl+right` or be off. The tooltip labels name the default binds only. There is no Options > Controls
entry: the SDK input service binds keys, not mouse chords.

## How

Left clicks arrive as the SDK's item interaction event. D2R sends none for a right click, so the grid widget's right-click
handler (93847 `0x2c3310`) is hooked by signature; every other right click goes to D2R unchanged. Moves and identify are SDK
item transactions (`executeExistingItemTransaction`) on the game thread.
