# Lock Inventory

PD2's Lock Inventory ([#1578](https://github.com/synpoox/pd2-resurrected/issues/1578)): a padlock at the bottom right of the
inventory panel. Locked, clicks, Ctrl/Shift clicks and the pad cannot put an item on, take it off or swap it on the player's or
the mercenary's body slots. Click it again to unlock. Weapon swap (W) still works: it changes no item.

## How it works

- **Button.** Two `ButtonWidget`s at one spot in `playerinventoryoriginallayouthd.json` (the expansion layout lists them), x 1001,
  y 1702 beside the gold field: `InventoryLock` (unlocked art) and `InventoryLockOn` (locked art). A frame watch shows only the
  one matching the lock (shown and enabled: a hidden button still takes the hover and hid the other's tooltip, #1659); the
  loader's widget service cannot set a toggle button's state, but it can show and hide widgets. Art:
  `PANEL\pd2r\inventory_lock` (`files/data/hd/global/ui/panel/pd2r/`, HD 72 px and lowend), PD2's padlock
  (`buysellbtn.dc6` frames 24-27, upscaled in `merge/art/ui/`) altered by `tools/make_inventory_lock.py`: gold and bronze metal,
  a taller shackle, a soft bevel (frames: unlocked, pressed, locked, pressed; rerun it to redraw).
- **Legacy.** `playerinventoryoriginallayout.json` (the expansion layout lists them) holds the same two buttons at x 276, y 464 in
  the bottom strip, art `PANEL\pd2r_inventory_lock` (`files/data/global/ui/panel/pd2r_inventory_lock.dc6`, 26 px, the HD frames
  scaled by `tools/make_inventory_lock.py`).
- **Toggle.** The click sends `PanelManager:OpenPanel:core-ui/InventoryLock`; core-ui's `lock` part catches it, flips the lock and
  consumes it. The lock carries over to the next game and is saved per character, as PD2 saves it: while locked,
  `<mod>/d2rloader/config/inventory-lock-<name>.locked` exists, and `inventory-lock.locked` holds the last lock in use, which the
  padlock shows on entry. The first message from the local player in a game reads the character's name and loads its lock
  (the log says `inventory lock: <name> is locked|unlocked`).
- **Block.** core-ui hooks the server's client-message dispatch (93847 `0x4f2fa0`, by signature) and drops, from the local player
  while locked, every message that changes a body slot (`ChangesEquipment` in `plugin/core-ui/inventory_lock.h`): 0x1a-0x1e,
  0x21 and their player-id forms 0x56-0x5a (0x58 is Shift+click unequip); 0x51 and 0x5b for the mercenary's slots; 0x5c (drop to
  the ground) only from a body slot; and the list forms 0x22 and 0x60 when they name a worn item. The log says
  `inventory lock: refused message 0x..`. No SDK service sees these moves, so it is a native hook.
- **PD2.** `ProjectDiablo.dll` keeps `equipment_lock` in its settings and sends it to the server (packet 0x5c); its wrappers of
  the 1.13c body-slot handlers refuse while it is on, and also refuse picking up magic or better charms from the inventory.

## Differences

Listed in [deviations](../deviations.md) (Lock Inventory): no charm lock, single player and host
only, no sound, no controller-layout button.
