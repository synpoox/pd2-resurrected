# Controller support audit (#340)

Steps 1 and 2 of [#340](https://github.com/synpoox/pd2-resurrected/issues/340): what D2R's native controller support
already covers of PD2's controller layer, and what the screens and plugins this port adds can't reach with a controller.
Findings only; nothing is ported. D2R 93847 under D2RLoader 1.3.1, mod build at `a9ded919`, checked on 2026-11-18 in
Slot3 on a level 99 Assassin (Hell, Harrogath) driven by a virtual XInput pad (test harness `tools/pad.py`,
[d2r-test-harness#2](https://github.com/synpoox/d2r-test-harness/pull/2)). Screenshots stay in the slot's run folder;
the evidence below is the word description of each one and the string and layout files named.

## 1. What D2R already does of PD2's layer

PD2's layer, as `docs/pd2/engine-dll-triage.md` lists it (from `ProjectDiablo.dll`: the `XINPUT1_4.dll` import, the
`Controller*` strings, `controllerhelp`, `controllermenu` art). The PD2 side was not re-read for this audit.

| PD2 feature | D2R 93847 | Evidence | Verdict |
|---|---|---|---|
| Movement with the left stick | Yes | the virtual pad's stick moved the character in Harrogath; `ControllerHelpRun`: "Move (PRESS) Run / Walk Toggle" | covered |
| Aim assist | Not found | no aim, target or assist string in `ui.json`, `ui-controller.json` or `keybinds.json`; the game may have it unnamed | unknown |
| Dead zone | Not found | no string or option; XInput applies no dead zone, so the game applies its own. Test: a stick pushed to 0.1 and 0.25 did not move the character, 0.5 and 1.0 did, so it sits between 0.25 and 0.5 | unknown |
| Cursor sensitivity | Yes | `ControllerCursorSpeedSetting` "Cursor Sensitivity" (the UI cursor; not a stick speed for movement) | covered for menus |
| Button styles | Partly | the HUD and help bars draw Xbox glyphs for XInput; the strings name DualShock and DualSense hardware only; no style option | covered as glyph sets, no choice |
| Per-screen navigation help | Yes | a help bar sits under each controller screen: the menu shows "Open Cube", "Close Menu"; the cube shows "Transmute", "Hold to Populate Belt", "Close Menu"; the `ControllerHelp*` and `ControllerPrompt*` strings | covered |
| 13 skill-select binds | 12 plus 2 | six skill slots (`ControllerHelpSkill1..6`) and six more with `ControllerWeaponSwapSeparateBindingsSetting` ("Weapon Swap Separate Skill Buttons"), plus Interact and Loot as skills (`SkillNameInteract`, `SkillNameLoot`); the HUD shows A, X, B, Y, RB, RT on the six slots, LT swaps the bar | covered, in a different shape (6 + 6) |
| Multi-button loot | Yes | `SkillNameLoot` "Pick up Items, weapons, and armor"; `ControllerHelpHighItems` "Show item labels"; `WideItemDropEnabledTooltip` "item spacing assists with controller play" | covered |
| Crosshair | Replaced | no crosshair; a world object under the character gets its name and an A glyph (the altar showed "Sacrificial Altar" with A) | covered by the interact prompt |
| PD2's `controllerhelp` and `controllermenu` art | Not needed | D2R draws its own help bars and menu | covered |

Open from this table: whether aim assist and the dead zone exist at all in D2R, and whether PD2's players use them
enough to ask. The virtual pad does send partial values; the game's own dead zone is the 0.25 to 0.5 band measured below.

## 2. Screens and plugins the port adds

The pad moved the controller UI as a real one: START opens the tabbed game menu on the Inventory tab, LS opens the
Cube, RB and LB switch tabs, LT and RT switch the skill tree's tabs, B and START close.

| Screen or plugin | Reached with the pad | Result |
|---|---|---|
| Inventory (10 x 8 grid, `files/data/global/excel/inventory.txt` (gridY 8 against vanilla 4), `files/data/global/ui/layouts/playerinventoryexpansionlayouthd.json`) | yes | **Broken.** The mod ships no controller inventory layout or art, so D2R's controller one applies: its background has four rows. Rows 5 to 8 of the grid draw items below the panel, over the gold bar, the help bar and the HUD (items seen at the foot of the screen, outside the frame). Cells in rows 5 to 8 have no frame to focus, so they can't be used with the pad. The same stray items draw when another panel is open (the altar). |
| Horadric Cube (4 x 4, `files/data/global/ui/layouts/controller/horadriccubelayouthd.json`) | yes | Works: the panel draws the 4 x 4 grid, "Y Transmute" and "B Close Menu" in the help bar. One thing to confirm: right after LS the highlighted slot was the fourth belt slot of the inventory half (green), not the cube grid the layout's `defaultWidget` names. Seen once. |
| Sacrifice Altar (`files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json`, `plugin/core-events/altar_panel.cpp`) | opens (A on the altar) | **Unreachable.** The panel has a PC layout only: no `controller/` layout, no `navigation`, no `defaultWidget`. D-pad presses moved no focus (they drank belt potions: the D-pad belongs to the belt), so the amulet slot and the confirm button can't be reached, and B does not close it (`CloseOnEscape` is keyboard Escape); START opened the game menu behind it and a second START closed both. No help bar. |
| Skill tree (the Assassin's four tabs, General Skills as the fourth) | yes | Works: LT and RT change tab, the help bar shows "Close Menu". The seven class sprite sets ship in `files/data/hd/global/ui/controller/panel/spells`. Not checked: every class, and the spend-point flow. |
| Stack split (core-ui, `plugin/core-ui/stack_split.cpp`) | no | **No pad path.** It reads `GetAsyncKeyState(VK_CONTROL)` plus the mouse click, so a pad cannot split a stack. D2R's own controller split, if it has one, was not checked. |
| Item quantity on stacks (core-ui `item_quantity`) | yes | Works: the counts draw on the stacks in the controller grid (a "2" on a stack of two). |
| HD pictures for PD2 item codes (core-ui `item_pictures`) | yes | Drawn in the controller grid like any item. |
| Companion plugins (Pyre Loot Filter, Pyre Stash Search, Item Roll Ranges, Buff HUD, Left Click Auras, the RuffnecKk rule plugins, Mass Identify, Mercenary Drop On Hire, DPS Counter) | not checked | Loot filter, rule plugins and the HUD plugins add no screen. Stash Search takes typed text, Left Click Auras acts on a mouse click, Mass Identify has a config file, Item Roll Ranges adds tooltip text: how each works with a pad was not tested. |
| Map items and events (Cartographer, core-events `map_open`) | not checked | Opening a map is the item's use; the map device and portals use the native A prompt like any object. Not tested with the pad. |

What the test could not do: judge stick feel on a real stick (partial values were tested: 0.1 and 0.25 did not move the character, 0.5 and 1.0 did), try a DualShock layout, or play a fight.
The 4-row inventory art and the altar panel were seen directly; the rest is read from the layout files.

## 3. What is left to port

Short work items, each a sub-issue of #340:

1. A controller inventory for the 10 x 8 grid (a layout in `files/data/global/ui/layouts/controller/` and a taller
   background sprite, as the cube did).
2. A controller layout for the Sacrifice Altar panel: focus on the amulet slot and the confirm button, B to close, a
   help bar.
3. A controller way to split a stack, or a check that D2R already has one.
4. A pad check of each companion plugin's input, with a note per plugin.
5. Confirm where the Cube's controller focus starts.

Left out on purpose: aim assist and the dead zone (aim assist not found in D2R; the dead zone is the game's own, no data that anyone misses it), button styles,
and PD2's 13 binds (D2R's 6 + 6 stand in). Add them only if the owner or the players ask.
