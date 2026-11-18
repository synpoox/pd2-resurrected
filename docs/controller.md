# Controller controls

Every controller control the mod ships: D2R's own binds, Controller QoL's (PyreFly's plugin, pinned in #1453) and ours.
Status is from the [controller audit](research/controller/controller-audit.md) and the merged controller PRs.
"Untested" means no in-game check yet. Parent issue: [#340](https://github.com/synpoox/pd2-resurrected/issues/340).

Source: **PD2 mirror** (PD2's bind), **D2R native** (D2R's bind, unchanged), **Controller QoL**, or **Our own**.
No control is a PD2 mirror yet; PD2's 13 skill binds are left out (D2R's 6 + 6 stand in).

| Control | What it does | Works in | Source | Status |
|---|---|---|---|---|
| Left stick | Moves the character | world | D2R native | works (audit, section 1) |
| Right stick | Moves the aim cursor | world, menus | Controller QoL | works (audit, section 6) |
| Right stick, snap | Snaps the aim to an enemy near the cursor | world | Controller QoL | untested (#1448) |
| A | Interacts with a world object (altar, portal, map device); picks up and places items | world, inventory | D2R native | works (audit, sections 1 and 2; #1449 for the 10 x 8 grid) |
| D-pad | Drinks belt potions. It does not move the inventory focus | world, inventory | D2R native | works (audit, section 2; #1449) |
| START | Opens the tabbed game menu on the Inventory tab | world | D2R native | works (audit, section 2) |
| B | Closes the menu | inventory, Cube, skill tree, altar | D2R native | works (audit, section 2; #1454 for the altar) |
| LB | Switches the menu tab | menu | D2R native | works before Controller QoL (audit, section 2); not re-tested with it |
| RB | Switches to the skill tab (was the main menu page) | skill tree | Controller QoL | works (audit, section 6) |
| RT | Switches the main menu page, Inventory to Skill Tree (was the skill tab) | menu | Controller QoL | works (audit, section 6) |
| LT | Switches the skill tree tab | skill tree | D2R native | works before Controller QoL (audit, section 2); not re-tested with it |
| LS | Opens the Horadric Cube | inventory | D2R native | works (audit, section 2); focus start open (#1444) |
| Y | Transmutes | Cube | D2R native | works (audit, section 2) |
| Modifier (LB) + B | Transmutes | Cube | Controller QoL | untested (log says "available") |
| Hold LB | Labels ground items with X, A and Y | world | Controller QoL | works (audit, section 6) |
| LB + X | Picks up the labelled ground item into belt slot 1 (potions) | world | Controller QoL | works (audit, section 6) |
| LB + A | Picks up the labelled ground item into the inventory | world | Controller QoL | works (audit, section 6) |
| LB + Y | Picks up the labelled ground item | world | Controller QoL | untested (Y label seen, pickup not run) |
| LB + A | Identifies the focused item | inventory | Controller QoL | works on an unidentified ring (audit, section 2) |
| LB + X | Transfers the focused item to the stash | inventory, stash | Controller QoL | untested |
| LB + Y | Sends the focused item to the Cube | inventory | Controller QoL | untested |
| LB + R3 | Fills the belt | inventory | Controller QoL | untested |
| Modifier (LB) + A | Splits a stack on the cursor, as Ctrl-click does | inventory | Our own (the modifier is Controller QoL's setting) | works (#1459) |
| START, then RB | Opens the menu on the altar tab, panel beside the inventory with focus | altar | Our own | works (#1454); tab label shows "Missing string" (#1457) |
| D-pad up / down | Moves focus between the amulet slot and the confirm button | altar | Our own | works (#1454); amulet pickup and place not proven |
| A on the altar | Opens the altar panel without pad focus | world | D2R native | gap (#1456) |

## Known gaps

- #1444: Cube focus starts on the wrong widget (the belt slot, not the grid).
- #1456: altar opened in the world gets no pad focus.
- #1457: altar route tab shows "Missing string".
- #1463: stack split with modifier + A onto an occupied cell, and without Controller QoL.
- #1466: Holy Sword parry is missing (PD2's class-5 parry roll).
- #1468: Item Roll Ranges shows no ranges with the pad (RT is Controller QoL's page button).
- #1469: Potion Counter shows no counts under the controller HUD.
- #1448: aim assist. Owner test of D2R's aim behaviour; Controller QoL's snap is untested.

## Rule

- Controller binds mirror PD2 where possible, or reuse D2R's or Controller QoL's own binds.
- No new binds without the owner's approval.
- Every controller PR updates this file.
