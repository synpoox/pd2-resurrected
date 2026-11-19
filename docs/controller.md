# Controller controls

Every controller control the mod ships: D2R's own binds, Controller QoL's (PyreFly's plugin, pinned in #1453) and ours.
Status is from the [controller audit](research/controller/controller-audit.md) and the merged controller PRs.
"Untested" means no in-game check yet. Parent issue: [#340](https://github.com/synpoox/pd2-resurrected/issues/340).
Controller QoL ships with `[aim] cast_observer_enabled = false` (`companions/config/controller-qol-updates.toml`) so Cast Triggers can hook the cast input (#1517); aim, snapping and reticles are unaffected.
To drive the pad in a game slot, see [Driving the pad](agents/game-check.md#driving-the-pad).


Source: **PD2 mirror** (PD2's bind), **D2R native** (D2R's bind, unchanged), **Controller QoL**, or **Our own**.
No control is a PD2 mirror yet; see [PD2 binds](#pd2-binds) for each PD2 bind's status.

| Control | What it does | Works in | Source | Status |
|---|---|---|---|---|
| Left stick | Moves the character | world | D2R native | works (audit, section 1) |
| Right stick | Moves the aim cursor | world, menus | Controller QoL | works (audit, section 6) |
| Right stick, snap | Snaps the aim to an enemy near the cursor | world | Controller QoL | untested (#1448) |
| A | Interacts with a world object (altar, portal, map device); picks up and places items | world, inventory | D2R native | works (audit, sections 1 and 2; #1449 for the 10 x 8 grid) |
| D-pad | Drinks belt potions. The harness pad's D-pad moved no inventory focus (#1449); D2R's D-pad moves focus in the altar panel (#1454) | world, inventory, altar | D2R native | works for potions and the altar (audit, section 2 altar row; #1454); inventory focus not moved (#1449) |
| START | Opens the tabbed game menu on the Inventory tab | world | D2R native | works (audit, section 2) |
| B | Closes the menu | inventory, Cube, skill tree, altar | D2R native | works (audit, section 2; #1454 for the altar) |
| LB | Switches to the previous skill tab | skill tree | Controller QoL | untested with QoL (the audit ran the native LB before the remap) |
| RB | Switches to the skill tab (was the main menu page) | skill tree | Controller QoL | works (audit, section 6) |
| LT | Switches to the previous main menu page | menu | Controller QoL | untested with QoL (the audit ran the native LT before the remap) |
| RT | Switches the main menu page, Inventory to Skill Tree (was the skill tab) | menu | Controller QoL | works (audit, section 6) |
| LS | Opens the Horadric Cube | inventory | D2R native | works (audit, section 2); focus start open (#1444) |
| Y | Transmutes | Cube | D2R native | works (audit, section 2) |
| Modifier (LB) + B | Transmutes | Cube | Controller QoL | untested (log says "available") |
| Hold LB | Labels up to seven ground items with X, A, Y, B, RB, RT and LT | world | Controller QoL | works with X, A and Y (audit, section 6); B, RB, RT and LT untested |
| LB + X | Picks up the labelled ground item into belt slot 1 (potions) | world | Controller QoL | works (audit, section 6) |
| LB + A | Picks up the labelled ground item into the inventory | world | Controller QoL | works (audit, section 6) |
| LB + Y | Picks up the labelled ground item | world | Controller QoL | untested (Y label seen, pickup not run) |
| LB + A | Identifies the focused item | inventory | Controller QoL | works on an unidentified ring (audit, section 2) |
| LB + X | Transfers the focused item to the stash | inventory, stash | Controller QoL | untested |
| LB + X | Sells the focused item in a shop | shop | Controller QoL | untested |
| LB + Y | Sends the focused item to the Cube | inventory | Controller QoL | untested |
| LB + R3 | Fills the belt with potions from the inventory | inventory | Controller QoL | untested |
| LB + L3 or LB + R3 | Fills the merchant with potions and tomes | shop | Controller QoL | untested |
| Modifier (LB) + A | Splits a stack on the cursor, as Ctrl-click does | inventory | Our own (the modifier is Controller QoL's setting) | works (#1459) |
| START, then LB/RB to the altar tab | Removed (#1480): the altar opens only from the altar object in the Temple, so there is no altar tab | altar | Our own | cut (#1480) |
| D-pad up / down | Moves focus between the amulet slot and the confirm button | altar | Our own | works (#1456: the first DOWN lights the confirm button); amulet pickup and place not proven |
| A on the altar | Opens the altar panel with the pad frame, the help bar and focus on the amulet slot; B closes it | world | D2R native | works (#1456, `docs/research/controller/altar-world-focus.md`) |
| A, X, B, Y, RB, RT | Skill slots 1 to 6 | world | D2R native | from strings (`ControllerHelpSkill1..6`), not tested |
| LT | Swaps the skill bar (Skills Bar and Skills Back Bar) | world | D2R native | from strings (`ControllerHelpSkillsBar`, `ControllerHelpSkillsBackBar`), not tested |
| L3 | Run / walk toggle | world | D2R native | from strings (`ControllerHelpRun`), not tested |
| R3 | Swaps weapons; hold for the emote wheel | world | D2R native | from strings (the "Swap Weapons" and "Emote wheel" help text), not tested |
| Automap | Opens the Automap, used in place of PD2's LT minimap | world | D2R native | from strings (`ControllerHelpAutoMap`), not tested |
| Interact, Loot, Show Items | Pseudo-skills in the skill bar: interact with the world, pick up items, show item labels | world | D2R native | from strings (`SkillNameInteract`, `SkillNameLoot`, `ControllerHelpHighItems`), not tested |

## PD2 binds

One line per PD2 pad bind, with its status.

- Assign a skill: covered by D2R.
- LT minimap: D2R's Automap is used instead, since LT clashes.
- RT swap weapons: covered by D2R (R3).
- RT attack with the right skill: covered by D2R.
- LB show loot: covered by D2R and QoL.
- Always show loot: covered by D2R's Show Items.
- Belt potion to mercenary: gap (#1472).
- Run/walk: covered by D2R (L3).
- Resync: cut, since D2R has no resync.
- Per-skill auto-aim: covered by QoL (R3 in the skill tree).
- Inventory LT/RT as Shift/Ctrl: replaced by QoL's LB modifier.
- Identify, transfer, drop, Cube transfer and sell: covered by QoL LB chords and D2R prompts.
- Multi-buy: gap (#1472). QoL's LB+R3 fills potions and tomes only.
- Stack-tab "transfer 5": unverified, possible gap (#1472).
- Whole stack-tab stack to the Cube: gap (#1472).
- Gamble Refresh and Repair All: unverified.

## Known gaps

- #1444: Cube focus starts on the wrong widget (the belt slot, not the grid).
- #1457: closed by #1480 (the altar tab is gone, so no "Missing string" tab).
- #1463: stack split with modifier + A onto an occupied cell, and without Controller QoL. The slot 4 check (2026-11-18) reached neither case: the harness pad moved no inventory focus (#1449) and the free pad cursor would not land on a spawned item. From the code, `ModifierHeld()` falls back to LB when Controller QoL is absent, so LB + A on a held stack would split there too. Not proven in game.
- #1466: Holy Sword parry is missing (PD2's class-5 parry roll).
- #1468: Item Roll Ranges shows no ranges with the pad (RT is Controller QoL's page button).
- #1469: Potion Counter shows no counts under the controller HUD.
- #1472: PD2 pad bind gaps (belt potion to mercenary, multi-buy, stack-tab transfer, whole stack to the Cube).
- #1448: aim assist. Owner test of D2R's aim behaviour; Controller QoL's snap is untested.

## Rule

- Controller binds mirror PD2 where possible, or reuse D2R's or Controller QoL's own binds.
- No new binds without the owner's approval.
- Every controller PR updates this file.
