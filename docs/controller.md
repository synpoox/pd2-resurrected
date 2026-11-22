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
| X | Tapped on a focused stash item, drops it on the ground at the character's feet, a stack whole; held, it seems to move it to the inventory (the help bar, cut off on screen, reads "X ...op (Held to Move to I...") | stash | D2R native | tap: works (#1637); hold: untested. A 40-Eld stack tapped with X in the Personal tab left the stash and lay on the ground as "40x Eld Rune"; nothing is lost, but the ground is hidden while the stash is open |
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
| Modifier (LB) + A | Splits a stack on the cursor, as Ctrl-click does | inventory | Our own (the modifier is Controller QoL's setting) | works (#1459); a legend "LB + A Split Stack" shows in the right margin beside the inventory grid only while the cursor holds a splittable stack (more than one item, not a tome): `pd2rSplitLegendLB` + `pd2rSplitLegend` (`controller/playerinventoryexpansionlayouthd.json`, hidden by default, #387), shown and hidden by `stack_split.cpp`'s cursor watch through the SDK WidgetService (findPanel, findWidget, setWidgetVisible; on a state change or a reopened panel; no hook). Two widgets as a ButtonLegendWidget draws its presets A first; the LB glyph follows `cycleLeft`. Seen in game, covering nothing (a left-margin spot hid behind the Cube panel) |
| Modifier (LB) + RB + A | Sends the whole stack under the pad focus (stash or stack tab) to the Cube. The SDK has no focused-item read, so the chord is held with A on the focused item (#1472) | stash, stack tab | Our own (the modifier is Controller QoL's setting) | proven from the Personal stash tab only (the Runes or stack tab is unchecked): with a real LB + RB on the virtual pad and the 40-rune stack focused, LB+RB+A logged `chord on container 7 page 4 cell 0,0` and `stack of 40 to the Cube moved`, and the stash cell emptied (#1516) |
| RB + A | On a stack-tab cell (Runes, Gems), moves 5 to the inventory; LB + RB + A there moves the whole stack to the Cube. Hooks D2R's stack-cell activate by signature and calls D2R's own bulk move (#1515) | stack tab | Our own | works from the Runes tab (#1515): RB + A took the stack 80 to 75 and put 5 in the inventory, RB did not change the tab; LB + RB + A filled the Cube (16 runes, then the Cube was full: the stack 75 to 59) |
| START, then LB/RB to the altar tab | Removed (#1480): the altar opens only from the altar object in the Temple, so there is no altar tab | altar | Our own | cut (#1480) |
| D-pad up / down | Moves focus between the amulet slot and the confirm button | altar | Our own | works (#1456: the first DOWN lights the confirm button); amulet pickup and place not proven |
| A on the altar | Opens the altar panel with the pad frame, the help bar and focus on the amulet slot; B closes it | world | D2R native | works (#1456, `docs/research/controller/altar-world-focus.md`) |
| D-pad | Moves focus over the mercenary's gear, laid out like the inventory's (gloves, belt and right ring below the torso row; boots, amulet and left ring on the right; taller panel, `controller/hirelinginventorypanelhd.json`, #1627, #1657) | mercenary | Our own | untested |
| D-pad right from the gold button, then A | Focuses the inventory padlock (bottom right under the grid) and toggles Lock Inventory (`controller/playerinventoryoriginallayouthd.json`, #1660); focus stays on the padlock, so A again toggles back (one button under a locked-art image, help bar says Toggle, #1675) | inventory, mercenary | Our own | inventory and mercenary tabs verified in game (focus, A lock and unlock, locked item move refused; on the mercenary tab a locked merc helm pick-up and unequip are refused; #1675: A twice with no D-pad press locks then unlocks with focus kept, inventory and mercenary tabs) |
| LT / RT | Switches stash tabs; past Runes RT opens Maps, then Bossing, then wraps to Personal (LT the other way). Core-ui catches the tabLeft/tabRight action in the stash panel's OnMessage, so any binding, pad number and pad type (DS4, Steam Input) works (#1671): the stash's own tab bar stays on Shared under our tabs, as an index past 4 (5 or more) crashes the stash (`controller/bankexpansionlayouthd.json`, #1661) | stash | Our own (stock bar extended) | works (#1661, redone for #1671 on the action catch, virtual pad only: a rebound LT/RT and DS4 untested): Runes RT Maps (page 16), RT Bossing (page 20), RT Personal; LT back the same way, no assert. The Maps tab shows its tier 1-3 and base buttons and the Maps and Bossing backgrounds (#1669) |
| LB / RB, left stick click | On the Maps tab only: LB and RB step the base of the shown tier back and forward (wrapping), the left stick click (D2R's `option3`) steps the tier I, II, III and round. Caught in the stash's OnMessage like LT/RT, no focus needed, and run the same code as a click on the buttons (the D-pad can't reach them: the stash grid never takes pad focus). On Bossing they do nothing; on Shared and the stock tabs they keep their usual roles, and the right stick click stays Hold to Sort (#1669) | stash (Maps tab) | Our own | works (#1669, virtual pad): LB/RB log `pad base (view 1, tier 2, base 1)` and wrap 0 to 7; left stick click logs `pad tier (view 1, tier 2, ...)` through tiers 2, 3, 1, 2; LT/RT still cycle all tabs and LB+RT on Shared still stops at 15; after B no press is caught. A legend in the panel (not the help bar, which only names a focused widget; the stash has none on Maps) names them: LS Next Tier, LB Previous Base, RB Next Base, drawn by `pd2rMapsLegend` (`bankexpansionlayouthd.json`, shown by core-ui on Maps only, #1687; seen in game on Maps, absent on Shared and Bossing); the Maps and Bossing art are the pad Mats art with our cell recesses drawn on it (tools/make_pad_stash_art.py), so frame and edges match Mats (#1686) |
| LB + LT / RT | Pages the Shared tab; page right stops at page 15, as pages 16-20 are the Maps and Bossing pages. Caught in the stash's OnMessage as tabLeft/tabRight while the cycleLeft action (LB) is held (any binding, pad and pad type, #1671). Counted with the mouse arrows, so mixed paging stops there too (#1661) | stash | D2R native, stop is our own | works (#1661): stops at `Page 15 / 20` with `Shared pad page right at page 15 stops`; pad to 12, then mouse, then pad still stop at 15 |
| (none) | Pyre Stash Search box hidden in the controller stash: its box takes the x of a `StashSearchAnchor` widget in `controller/bankexpansionlayouthd.json`, set off-screen (x 20000); the mouse layout has none and keeps the box (#1684) | stash | Our own | works (#1684, virtual pad): pad stash shows no Search box and the plugin logs `layout invalid (StashSearchAnchor=...)`; after a mouse click the stash shows the box and its drop-down again |
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
- Belt potion to mercenary: covered natively by D2R's LT + D-pad left/right, proven in game 2026-11-21, no plugin bind (#1513).
- Run/walk: covered by D2R (L3).
- Resync: cut, since D2R has no resync.
- Per-skill auto-aim: covered by QoL (R3 in the skill tree).
- Inventory LT/RT as Shift/Ctrl: replaced by QoL's LB modifier.
- Identify, transfer, drop, Cube transfer and sell: covered by QoL LB chords and D2R prompts.
- Multi-buy: gap (#1514). Chosen chord LB + Y on a shop item, not built: the SDK emits no shop event and has no buy call. QoL's LB+R3 fills potions and tomes only.
- Stack-tab "transfer 5": built as RB + A on a stack-tab cell (#1515), see the table.
- Whole stack-tab stack to the Cube: built as LB + RB + A (#1472), see the table.
- Gamble Refresh and Repair All: unverified.

PD2's own combos can't be mirrored: its LT + X multi-buy clashes with Controller QoL's LT/RT page change, and its other three are keyboard-only in PD2.

## Known gaps

- #1444: Cube focus starts on the wrong widget (the belt slot, not the grid).
- #1457: closed by #1480 (the altar tab is gone, so no "Missing string" tab).
- #1463: stack split with modifier + A onto an occupied cell, and without Controller QoL. The slot 4 check (2026-11-18) reached neither case: the harness pad moved no inventory focus (#1449) and the free pad cursor would not land on a spawned item. From the code, `ModifierHeld()` falls back to LB when Controller QoL is absent, so LB + A on a held stack would split there too. Not proven in game.
- #1466: Holy Sword parry is missing (PD2's class-5 parry roll).
- #1468: Item Roll Ranges shows no ranges with the pad (RT is Controller QoL's page button).
- #1469: Potion Counter shows no counts under the controller HUD.
- #1513, #1514, #1515: PD2 pad bind gaps (belt potion to mercenary, multi-buy, stack-tab transfer); #1516: owner pad check of the Cube chord.
- #1448: aim assist. Owner test of D2R's aim behaviour; Controller QoL's snap is untested.

## Rule

- Controller binds mirror PD2 where possible, or reuse D2R's or Controller QoL's own binds.
- No new binds without the owner's approval.
- Every controller PR updates this file.
