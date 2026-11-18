# Controller support audit (#340)

Steps 1 and 2 of [#340](https://github.com/synpoox/pd2-resurrected/issues/340): what D2R's native controller support
already covers of PD2's controller layer, and what the screens and plugins this port adds can't reach with a controller.
Findings only; nothing is ported. D2R 93847 under D2RLoader 1.3.1, mod build at `a9ded919`, checked on 2026-11-18 in
Slot3 on a level 99 Assassin (Hell, Harrogath) driven by a virtual XInput pad (test harness `tools/pad.py`,
[d2r-test-harness#2](https://github.com/synpoox/d2r-test-harness/pull/2)). Screenshots stay in the slot's run folder;
the evidence below is the word description of each one and the string and layout files named.

The controls the mod ships, with source and status, are in [docs/controller.md](../../controller.md).

## 1. What D2R already does of PD2's layer

PD2's layer, as `docs/pd2/engine-dll-triage.md` lists it (from `ProjectDiablo.dll`: the `XINPUT1_4.dll` import, the
`Controller*` strings, `controllerhelp`, `controllermenu` art). The PD2 side was not re-read for this audit.

| PD2 feature | D2R 93847 | Evidence | Verdict |
|---|---|---|---|
| Movement with the left stick | Yes | the virtual pad's stick moved the character in Harrogath; `ControllerHelpRun`: "Move (PRESS) Run / Walk Toggle" | covered |
| Aim assist | Not found | no aim, target or assist string in `ui.json`, `ui-controller.json` or `keybinds.json`; PD2 has `controller_aim_assist` (default 2); in-game check not possible (no monster spawned in town), see section 4 | open, section 4 |
| Dead zone | Not found | no string or option; XInput applies no dead zone, so the game applies its own. Test: a stick pushed to 0.1 and 0.25 did not move the character, 0.5 and 1.0 did, so it sits between 0.25 and 0.5; finer steps: 0.315 no move, 0.316 move (radial, no option) | cut, section 4 |
| Cursor sensitivity | Yes | `ControllerCursorSpeedSetting` "Cursor Sensitivity" (the UI cursor; not a stick speed for movement) | covered for menus |
| Button styles | Partly | the HUD and help bars draw Xbox glyphs for XInput; the strings name DualShock and DualSense hardware only; no style option; PD2's option only picks the Xbox or DualShock glyph sheet | covered by device detection, section 4 |
| Per-screen navigation help | Yes | a help bar sits under each controller screen: the menu shows "Open Cube", "Close Menu"; the cube shows "Transmute", "Hold to Populate Belt", "Close Menu"; the `ControllerHelp*` and `ControllerPrompt*` strings | covered |
| 13 skill-select binds | 12 plus 2 | six skill slots (`ControllerHelpSkill1..6`) and six more with `ControllerWeaponSwapSeparateBindingsSetting` ("Weapon Swap Separate Skill Buttons"), plus Interact and Loot as skills (`SkillNameInteract`, `SkillNameLoot`); the HUD shows A, X, B, Y, RB, RT on the six slots, LT swaps the bar | covered, in a different shape (6 + 6) |
| Multi-button loot | Yes | `SkillNameLoot` "Pick up Items, weapons, and armor"; `ControllerHelpHighItems` "Show item labels"; `WideItemDropEnabledTooltip` "item spacing assists with controller play" | covered |
| Crosshair | Replaced | no crosshair; a world object under the character gets its name and an A glyph (the altar showed "Sacrificial Altar" with A) | covered by the interact prompt |
| PD2's `controllerhelp` and `controllermenu` art | Not needed | D2R draws its own help bars and menu | covered |

Open from this table: aim assist (section 4). The dead zone is measured (0.316) and button style is glyph selection only.

## 2. Screens and plugins the port adds

The pad moved the controller UI as a real one: START opens the tabbed game menu on the Inventory tab, LS opens the
Cube, RB and LB switch tabs, LT and RT switch the skill tree's tabs, B and START close.

| Screen or plugin | Reached with the pad | Result |
|---|---|---|
| Inventory (10 x 8 grid, `files/data/global/excel/inventory.txt` (gridY 8 against vanilla 4), `files/data/global/ui/layouts/controller/playerinventoryexpansionlayouthd.json`) | yes | Works (#1440): the mod ships its own controller layout and a taller background, scaled to fit the menu frame. All 8 rows draw inside the panel, above the gold bar, and a row 8 item can be picked up and placed with A. Checked in Slot2 on 2026-11-18 (the virtual pad's stick and D-pad did not move the focus; the cursor did, as before). |
| Horadric Cube (4 x 4, `files/data/global/ui/layouts/controller/horadriccubelayouthd.json`) | yes | Works: the panel draws the 4 x 4 grid, "Y Transmute" and "B Close Menu" in the help bar. One thing to confirm: right after LS the highlighted slot was the fourth belt slot of the inventory half (green), not the cube grid the layout's `defaultWidget` names. Seen once. 2026-11-18 (lane 1444, slot 2, build 93847, level 99, town): LS from the pad inventory opened the Cube 4 of 4 times with no focus frame on the grid; the first D-pad press then selected inventory cell (0,0) (the paired right panel's `defaultWidget`), and DOWN stayed in the inventory. The layout matches vanilla except the grid rect, so the engine ignores the cube `defaultWidget` when the inventory is paired; a data fix was not found, and a code focus call is blocked (the installed loader lacks `focusWidget`, plugin panels only). Right-click open is not reachable in pad mode (any pad press closes it). Not fixed; #1444 stays open. |
| Sacrifice Altar (`files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json`, `plugin/core-events/altar_panel.cpp`) | opens (A on the altar) | **Unreachable.** The panel has a PC layout only: no `controller/` layout, no `navigation`, no `defaultWidget`. D-pad presses moved no focus (they drank belt potions: the D-pad belongs to the belt), so the amulet slot and the confirm button can't be reached, and B does not close it (`CloseOnEscape` is keyboard Escape); START opened the game menu behind it and a second START closed both. No help bar. **#1441:** now reachable with a pad. A controller layout and a controller route exist: START, then LB/RB to the altar tab (label shows "Missing string", the loader does not resolve the route label key) opens the panel next to the inventory with focus, D-pad moves between the amulet slot and the confirm button, and B closes the menu with the "B Close Menu" help bar. A on the altar itself still opens the panel without pad focus (the SDK has no open-route call). |
| Skill tree (the Assassin's four tabs, General Skills as the fourth) | yes | Works: LT and RT change tab, the help bar shows "Close Menu". The seven class sprite sets ship in `files/data/hd/global/ui/controller/panel/spells`. Not checked: every class, and the spend-point flow. |
| Stack split (core-ui, `plugin/core-ui/stack_split.cpp`, `controller_split.cpp`) | yes (#1442) | **Works with a pad: modifier (LB by default) + A, like Controller QoL's item shortcuts.** D2R has no controller split of its own (Slot3, 2026-11-18, stack of 12 keys: A picks the whole stack up, the footer offers only Place, Drop, Show Ranges and Close Menu, Y and a held A do nothing, A places all 12). The mod's Ctrl test now also reads the pad with `XInputGetState`: Controller QoL's `modifier` setting (`[qol]` in `d2rloader/config/controller-qol-updates.toml`; `bumper`, `lb` and `l1` = LB, the default; `rb` and `r1` = RB; `lt`, `l2`, `trigger` and an empty value = LT; `rt` and `r2` = RT; `l3`; `r3`; `any` = LB or LT, as its own parser reads them; LB when the file or key is missing) held when a stack leaves the cursor. Clash check (Slot2, 2026-11-18, level 99 Necromancer, Controller QoL rev.100 from the sha-verified zip, default config, keys): with 4 keys on the cursor, LB held and A pressed logged `split: ... 4 held onto 0: result 0, 1 left down, 3 on the cursor` and nothing else happened (no identify, no cube move); plain A put the 4 down whole, with no split line; LB+A with an empty cursor picked the stack up like plain A (keys have nothing to identify). Not checked: LB+A with an empty cursor on an unidentified item (the slot's inventory was full and `cheat spawn` could not place one), a real pad, DualShock through Steam Input. A plugin's own XInput import sees the harness pad (it swaps the import tables of the plugin folder's DLLs). |
| Item quantity on stacks (core-ui `item_quantity`) | yes | Works: the counts draw on the stacks in the controller grid (a "2" on a stack of two). |
| HD pictures for PD2 item codes (core-ui `item_pictures`) | yes | Drawn in the controller grid like any item. |
| Companion plugins | per plugin | One line per plugin in "Companion plugins with the virtual pad" below (#1443). |
| Map items and events (Cartographer, core-events `map_open`) | not checked | Opening a map is the item's use; the map device and portals use the native A prompt like any object. Not tested with the pad. |

### Companion plugins with the virtual pad (#1443)

Checked in slot 2 on build 93847 (Hell, Rogue Encampment, level 99 Necromancer), pad only, with Controller QOL Updates rev.100 loaded. Town only: no fight.

- Controller QOL Updates: works. Pad focus shows a green cell and a tooltip; LB+A identified an unidentified ring; the footer help bar shows "RT Show Ranges".
- Pyre Loot Filter: the ground label showed under the pad HUD. Its rule tabs and LB reorder were not tried.
- Pyre Stash Search: needs keyboard (typed text). Not exercised in game; Pyre's notes say the controller stash layout has no search box.
- Item Roll Ranges: fails with the pad. RT is taken by Controller QoL's menu page change, and RB (its README key for that case) showed no ranges on an identified ring. Not checked without Controller QoL. Issue #1468.
- Left Click Auras: not verified. It logs "controller aura activation on", but the test class is a Necromancer and no aura could be put on the left skill; needs a Paladin-type character.
- Buff HUD: loaded (0 entries); no buff was active, so not verified.
- Mass Identify: does not load on 93847 (stat compatibility admission failed), pad or not. Issue #1467.
- DPS Counter: loaded; needs damage, which is not testable in town. Not verified.
- Potion Counter: fails under the controller HUD. The belt shows no counts, and the plugin only found the mouse HUD belt. Issue #1469.
- Mercenary Drop On Hire, Cast Triggers, Ethereal Item Rules, Progressive Affixes: loaded; no screen or input of their own, not exercised.


What the test could not do: judge stick feel on a real stick (partial values were tested: 0.1 and 0.25 did not move the character, 0.5 and 1.0 did), try a DualShock layout, or play a fight.
The 4-row inventory art and the altar panel were seen directly; the rest is read from the layout files.

## 3. What is left to port

Short work items, each a sub-issue of #340:

1. A controller inventory for the 10 x 8 grid (a layout in `files/data/global/ui/layouts/controller/` and a taller
   background sprite, as the cube did).
2. A controller layout for the Sacrifice Altar panel: focus on the amulet slot and the confirm button, B to close, a
   help bar.
3. A controller way to split a stack, or a check that D2R already has one.
4. Done in #1443: a pad check of each companion plugin, see "Companion plugins with the virtual pad".
5. Confirm where the Cube's controller focus starts.

PD2 pad binds, one line each with its status: [docs/controller.md](../../controller.md#pd2-binds). Aim assist, dead zone and button style: see section 4.

## 4. The three items the first audit left out (aim assist, dead zone, button style)

Measured 2026-11-18 on build 93847 in a test slot (virtual XInput pad, level 99 character, Rogue Encampment, god mode on).

| | PD2 | D2R 93847 | Gap and what it takes | Size |
|---|---|---|---|---|
| Aim assist | `controller_aim_assist` in `ProjectDiablo.json`, default 2; drawn in the DLL's own options menu with `AimAssist.dc6` art. The DLL range was not read (not decompiled for this audit). | No string or option in `ui.json`, `ui-controller.json` or `keybinds.json`. **Not tested in game:** `mspawn zombie1` and `monster zombie1` in the town returned "handled" but no monster showed on screen (one attempt, no retry), so whether D2R picks the nearest monster with a stick direction is unknown. | If D2R has none: a native hook that picks the nearest monster for the attack direction (core-ui or a new input plugin, needs RE of the attack target code). Not possible with tables. | Large (RE and a hook) |
| Dead zone | `controller_deadzone`, default 20 (percent); option with `Deadzone.dc6` art. | No option. Measured: the left stick is a radial threshold; 0.315 of full tilt does not move the character, 0.316 does (raw about 10340 of 32767). Not scaled by tilt. Step run 0.25 to 0.5 in 0.05, then 0.315 and 0.316. | A setting would need a hook on the stick read (XInput export or the game's input code). PD2's default (20) is below D2R's fixed 31.6, so D2R's is already the larger of the two. | Medium (hook plus an option row), low value |
| Button style | `controller_button_style`, default 0, options Auto, Xbox, PlayStation. Confirmed: it selects a glyph set, the `Controls_Xbox_10/20.dc6` or `Controls_Dualshock_10/20.dc6` sheets (16 frames, 10 and 20 px). Not a layout or colour theme. | No option. The glyphs follow the detected pad: Xbox glyphs shown with the XInput pad; `_ps4`, `_ps5`, `_switch` string variants and `controllerbuttons/ps4`, `ps5` sprites exist in the data. | Nothing to port as art. A manual override would need a hook on the device-type check; auto detection already covers real PlayStation pads. | Small to medium, low value |

Recommendations: aim assist, ready-for-human (owner: "i'll test first"; D2R has no setting; what to watch for: D2R's native soft lock-on in the facing direction, i.e. whether an attack with a stick direction already snaps to a nearby monster); dead zone, cut (owner, 2026-11-18; D2R's fixed 0.316 is larger than PD2's default 0.20); button style, cut (owner, 2026-11-18; D2R already picks Xbox or PlayStation glyphs from the device). The PD2 defaults above are the install's values (`ProjectDiablo.json`). Whether movement speed changes past the dead-zone threshold was not checked (only moved or not).

## 5. Pyre-Flies "Controller QOL" plugin

Source: the author's public repo (Pyre-Flies/D2R-Mods, `plugins/controller-qol`, clone at 8dab5f83) and its release `Controller-QOL-Updates-1.3.1+rev.100-PyreFly.zip`. The release was read, never run, and its DLL was tested in a slot next to our plugins (section 6). Not in `companions/manifest.json` today (the other Pyre-Flies plugins are).

- Licence: all rights reserved (`LICENSE`). The owner has approved use of Pyre-Flies' plugins (as with the Loot Filter and Stash Search already pinned); credit as for the others.
- Scope: global D2RLoader plugin (works with any mod). Requires Windows x64 and D2RLoader 1.3.1. `COMPATIBILITY.txt` qualifies it for D2R 3.3.93787, checked by a D2RCore.dll SHA-256, and it falls back to native behaviour otherwise. 93787 (Steam) and 93847 (Blizzard) are the same build: our `D2RCore.dll` has exactly the qualified hash (`2a868d013d2e0830...fdeb5a2`), and its native paths run on our build (section 6). An earlier version of this section said they would not; that was wrong.
- XInput: on our core it installs no XInput hook (it reads the game's own input through SDK hooks); the XInput trampoline is only its fallback. Measured with the harness pad in section 6.

| Gap | Covered by Controller QOL? |
|---|---|
| Aim assist | Partly: right-stick snap targeting picks an enemy near the cursor (with leading); not a left-stick or attack-direction assist. README @8dab5f83 "Controller aiming": `[aim]` `snapping_enabled` (default true), `snap_radius` 6.0, `switch_advantage`, per-skill `[aim.targeting]` "snap", projectile leading. Right-stick dead zone 0.22. |
| Dead zone (left stick) | No. Only the right-stick zone. |
| Button style | No option. |
| #1440 10x8 inventory | No (it routes Cube and stash focus only). |
| #1441 Sacrifice Altar panel | No. |
| #1442 stack split | No. |
| #1443 companion input | Yes, see "Companion plugins with the virtual pad". |
| #1444 Cube focus start | Partly: its Cube routing may change where focus starts; not run, so not confirmed. |

Conflicts: both own the Cube and stash controller focus (our core-ui panels would compete with its routing); both hook XInput; its right-stick cursor mode changes the stick behaviour our slot tests assume.

Other features it ships (no gap of ours): LB+face-button ground loot; identify, transfer, to-Cube, belt fill and vendor-sell shortcuts; Cube tap-open and transmute; LB/RB and LT/RT tab paging.

The table above is from reading its README; section 6 has what the slot run showed, and section 8 the recommendation.

## 6. Controller QoL alongside ours: test results

Run 2026-11-18 in Slot3 (lane-340c): mod build at `3b4693d9` (main) with core-rules, core-events, core-ui, Cartographer
and the test harness, the mod's companions (Pyre Loot Filter, Pyre Stash Search, Item Roll Ranges, Buff HUD and the
others in `companions/manifest.json`), and `Controller QOL Updates.dll` from `Controller-QOL-Updates-1.3.1+rev.100-PyreFly.zip`
(release `release-2026.11.16.2`, zip SHA-256 `2c7eac5e8397dd7894ba9d09156622eaf7559deba8be5c956cc08ded94348d06`;
the zip holds only the DLL) in the game-wide `d2rloader/plugins`. Level 99 Assassin, Hell, Rogue Encampment, god mode,
1920x1080 windowed, driven by the harness pad (`tools/pad.py`). Its executables were never run. Afterwards the DLL, the
config files it created (`d2rloader/config/controller-qol-updates.toml`, `controller-qol-skills/`) and its log were
removed from the slot. Screenshots stay in the run folder.

**Build and hash.** Our `D2RCore.dll` SHA-256 is `2a868d013d2e0830bd2d9e04b918b19e46a73cf726c833e70d089b948fdeb5a2`,
exactly the hash in its `COMPATIBILITY.txt` (`2A868D01...FDEB5A2`). The 93787 (Steam) and 93847 (Blizzard) labels
are the same build. Its log:

```
[QOL/IdentifyNative] admission coreSHA256=2A868D013D2E0830BD2D9E04B918B19E46A73CF726C833E70D089B948FDEB5A2 expected=2A868D013D2E0830BD2D9E04B918B19E46A73CF726C833E70D089B948FDEB5A2 itemService=1 uiScheduler=1 gameScheduler=1.
[QOL/NativeInput] Native normalized controller input active (SDK event hook game+0x13EDD0; reset+0x13DEF0). DualShock/Xbox/Steam use the game's input; no XInput hooks or shared core-slot replacements installed.
[QOL] QOL v1.3.1+rev.100 loaded: controller item features and integrated v0.6 navigation/label hooks.
```

D2RLoader listed `Controller QOL Updates 1.3.1+rev.100 by PyreFly [global]` between Pyre Stash Search and Cartographer,
and `16 plugins loaded; 2 plugins failed` (the two failures are the mod's Item Durability and MassID, which failed
before Controller QoL started). One error line repeats at load and at every ground pickup:
`Plugin safety-check patch failed at RVA 0xFABE0: game bytes did not match what the plugin expected`. It is a probe
of the loot dispatch for another plugin's (UnHoarder) hook; the next lines admit the profile
(`[QOL/ClientLoot] Native loot approach profile admitted.`) and pickup worked, so it is log noise, not a failure.

**Hook order with the harness pad.** On this core Controller QoL installs no XInput hook at all: it takes the game's
normalized input through two SDK inline hooks (game `+0x13EDD0`, `+0x13DEF0`) and reads the right stick through the
game's own axis getter. The harness pad replaces the XInput pointers the game holds (import-table swap) and answers
from its state file without calling the real export. So the order is: game, harness pad (XInput), game's input
layer, Controller QoL. The two never touch the same bytes; the export trampoline (`48 89 5C 24 08` prologue patch,
ordinal 100) is only Controller QoL's fallback on cores it does not admit. Evidence: the left stick moved the
character; START, B, LS, LT/RT, LB/RB and LB+face buttons drove Controller QoL's features (below); the right stick
moved its cyan aim cursor (held at -0.8, -0.6 it went to the lower left of the screen). `qol status` logged
`mode=native ... nativeHook=1 ... controllerEvents=2 ... faults=0`. If a later core falls back to its XInput
trampoline, the pad's state would not reach it (the pad answers before the export runs); see the risks.

**Our panels.**

| Panel | With Controller QoL | Notes |
|---|---|---|
| Inventory 10 x 8 | Unchanged: still broken on main | Rows 5 to 8 still draw below the panel. A Cap picked up with LB+A went into a lower row, drawn over the help bar with a red cell. Controller QoL routes items, it does not change the layout; #1440's layout is still needed. |
| Horadric Cube | Works | LS opened the Cube with the 4 x 4 grid, "Y Transmute", "B Close Menu"; B went back to the Inventory page. Its log: `[QOL/Cube] Controller tap-open/hold-pickup and modifier+B transmute available; native UI routes retained.` No focus frame showed in the Cube or the inventory, before or after D-pad presses, so where focus starts (#1444) is still open; lane 1440 saw the same (the virtual pad's D-pad moved no focus). |
| Stash | Not tested | Needs a walk to the stash. Its routes (LB+X transfer, LB+LT/RT shared pages) only touch stock stash panels. |
| Sacrifice Altar (core-events panel) | Not tested | Needs a map event. Its menu and TabBar hooks name stock panels only (`QOL/Menu`, `QOL/Shared`, `QOL/Submenu`); a plugin panel gets no route from it. |
| Skill tree | Works, remapped | Controller QoL swaps the tab buttons: RT now moves the main menu page (Inventory to Skill Tree) and RB the skill tab (Martial Arts to Shadow Disciplines); without it D2R uses LB/RB for pages and LT/RT for skill tabs (section 2). The panel shows the LT/RT and LB/RB glyphs to match. |

Its aim cursor also draws over open menus (the cyan ring showed on the Inventory and Cube pages), cosmetic only.

**Its features with our mod.**

| Feature | Result |
|---|---|
| Ground loot | Works. Holding LB over three spawned items labelled them (X) Super Healing Potion, (A) Cap, (Y) Super Mana Potion; LB+X picked the potion into belt slot 1 (`triggered pickup for slot [X]`, `native pickup request submitted`), LB+A the Cap into the inventory. |
| Right-stick aim cursor | Works with the pad (cursor moved with the stick). |
| Snap aim (#1448) | Not tested: no monster in town (out-of-town runs are the owner's). |
| Tab paging | Works (above). |
| Item shortcuts (LB+A identify, LB+X transfer, LB+Y to Cube), belt fill (LB+R3) | Not tested: they act on the focused item, and the pad showed no focus frame in the inventory (as in lane 1440's run). |
| Belt fill from the ground | The healing potion from the ground went to the belt (native rule). |

Counts: 7 checks passed (load and hash, pad drives the game, aim cursor, ground loot, Cube opens, tab paging, no new
error in our plugins' logs), 0 failed, 5 skipped (snap aim, item shortcuts, belt fill, stash, altar).

### Gap coverage

| Gap | Controller QoL | Ours to build |
|---|---|---|
| #1440 10 x 8 inventory | Not covered (layout and art; its pickup even fills the hidden rows) | Yes: mod data, lane 1440 |
| #1441 Sacrifice Altar panel | Not covered (it routes stock panels only) | Yes: layout plus focus, lane 1441 |
| #1442 stack split on pad | Not covered (it moves whole items: LB+X, LB+Y) | Done: modifier (LB by default) + A (core-ui) |
| #1443 companions with a pad | Checked: see "Companion plugins with the virtual pad". Controller QoL works; Item Roll Ranges and Potion Counter fail, Mass Identify does not load | Left Click Auras, Buff HUD, DPS Counter and Stash Search typing: not verified |
| #1444 Cube focus start | Not covered (tap-open and transmute, not focus) | Yes: data (`defaultWidget`) or a focus call |
| #1448 aim assist | Covered: right-stick cursor with snap targeting (`[aim] snapping_enabled`, `snap_radius` 6.0), per-skill profiles | No, unless the owner's fight test rejects it |

## 7. Where controller code lives

### Inventory (main at `3b4693d9`)

| Code or data | What it is | Generic or mod-specific |
|---|---|---|
| `files/data/global/ui/layouts/controller/horadriccubelayouthd.json` | The 4 x 4 Cube controller layout | Mod-specific (the mod's Cube size) |
| `plugin/core-ui/stack_split.cpp` | Ctrl + put-down splits a stack, read with `GetAsyncKeyState(VK_CONTROL)`; no pad path | The split is a PD2 rule (mod-specific); only the trigger, "a modifier is held", is input |
| `files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json`, `plugin/core-events/altar_panel.cpp` | The altar panel, PC layout only | Mod-specific |
| core-rules, common | No controller code (`controller` hits are Lucion's monster "controller") | - |

Lanes in flight:

- `ticket/1440-controller-inventory` (pushed, `2fcd9d72`): controller `playerinventoryexpansionlayouthd.json` and
  `playerinventoryoriginallayouthd.json`, and four `ui/controller/panel/pd2r/inventorybg*.sprite` files. Data only,
  mod-specific (the 10 x 8 grid). It also edits the inventory row of section 2 of this file.
- `ticket/1441-altar-controller` (not pushed; work in progress in its worktree): a controller
  `layouts/controller/core-maps/SacrificeAltarhd.json`, and `plugin/core-events/controller_panel.{h,cpp}` with
  `FocusPanelWidgetSoon(context, threads, widgets, panelName, widgetName)`: on the UI thread, once the panel is laid
  out, it focuses a named widget through the SDK `WidgetService` (`findPanel`, `findWidget`, `focusWidget`),
  retrying for up to 300 ticks. `altar_panel.cpp` calls it after the panel opens. The helper is generic (any plugin
  panel, SDK only); the call and the layout are mod-specific.

### The split

- **Mod-specific, stays in the mod:** the controller layouts and art for the mod's grids and panels (#1440, #1441, the
  Cube, #1444's `defaultWidget`), and the stack split rule itself (core-ui, ADR 0004). d2r-tools can't hold them: its
  plugins are global and never rely on a mod shipping layouts (d2r-tools `AGENTS.md`, "Working").
- **Generic:** reading "is a pad modifier or chord held" (for #1442, and any later pad shortcut), and "focus this
  widget once this plugin panel opens" (`FocusPanelWidgetSoon`). Neither needs a hook: the first can use the game's
  controller state as Controller QoL does, or XInput; the second is SDK-only.
- **Not ours to rebuild:** aim and snap targeting, ground loot, item shortcuts, belt fill, tab paging. Controller QoL
  has them, global and maintained.

### Homes

- **A new d2r-tools global plugin: not now.** What is generic is two small helpers, not a feature a player installs.
  A global pad plugin of ours would also hook the same input Controller QoL owns, and the SDK has no gamepad state
  service to share it (`input.h` is keyboard only). Revisit only if a pad feature for any mod comes up that Controller
  QoL lacks.
- **A shared library: the toolkit's `shared/plugin-common`.** That is where vendored-in helpers already live
  (pd2-resurrected links it in `plugin/CMakeLists.txt`). d2r-tools forbids code shared across its plugins, so a header
  there would not be reusable. Move `FocusPanelWidgetSoon` there as `panel_focus.{h,cpp}` once a second project needs
  it (plugin-common's own rule: "A value or helper only one project uses stays in that project until a second project
  needs the same code"); until then it stays in pd2-resurrected, in `plugin/common/` so core-ui and core-events can
  both use it.
- **Stays in the mod:** all layouts and art, and each panel's call into the helper. A mod panel gets controller
  support from the shared piece through a small adapter in the plugin that owns the panel: a controller layout (the
  data-driven focus map: `navigation` and `defaultWidget` in `layouts/controller/`) plus one
  `FocusPanelWidgetSoon` call when it opens, and B to close through the panel's own route. No API of a global plugin is
  needed.

### Plan (lanes 1440 and 1441 are kept as they are)

1. Merge #1440 and #1441 as built. Nothing in them moves later except one file: #1441's `controller_panel.{h,cpp}`.
2. After #1441 merges, one small PR moves `controller_panel.{h,cpp}` from `plugin/core-events/` to `plugin/common/`
   (an include path change in `altar_panel.cpp` and `plugin/CMakeLists.txt`), so core-ui panels can use it.
3. #1442: a pad trigger for the split in core-ui, next to the Ctrl check in `stack_split.cpp` (for example a held
   button read from XInput or the game's controller state, chosen so it does not clash with Controller QoL's LB
   chords). Mod code, since the split is a PD2 rule.
4. #1444: data first (`defaultWidget` of `controller/horadriccubelayouthd.json`); a `FocusPanelWidgetSoon` call only if the
   game ignores it.
5. #1443 is done (checked with Controller QoL loaded); the failures are #1467, #1468 and #1469.
6. Later, when a second project needs it: move the focus helper into the toolkit's `plugin-common` (a toolkit issue).

## 8. Recommendations

**A. Use Controller QoL alongside ours: (a), pin it as a companion and build only what it lacks.** It runs on our build
(same `D2RCore.dll` hash), passed its own checks, coexists with our plugins and the harness pad, and covers aim assist
(#1448) and partly #1443, which would each be large native work for us. It covers none of #1440, #1441, #1442 and
#1444, and none of them overlap its features, so there is nothing of its to switch off (option c): keep its defaults.
Pinning means: a `companions/manifest.json` entry (repo `Pyre-Flies/D2R-Mods`, tag `release-2026.11.16.2`, asset
`Controller-QOL-Updates-1.3.1+rev.100-PyreFly.zip`, its SHA-256, file `d2rloader/plugins/Controller QOL Updates.dll`),
a row in `NOTICE.md` with the other Pyre-Flies plugins (PyreFly's proprietary licence, owner-approved), and no
`companions/config/` file (it writes its own config on first start). Risks:

- Update cadence: rev.97 to rev.100 in two days, and each release qualifies one `D2RCore.dll` hash; a D2RLoader
  update leaves its native paths off until the author re-qualifies. Bump it one PR at a time, after a coexistence run.
- The tab remap changes D2R's own buttons (LT/RT pages, LB/RB sub-tabs). Its config has no switch for it (only the
  console's `qol menu on/off`); our help text and lane checks must follow its mapping.
- On a core it does not admit it falls back to an XInput trampoline that the harness pad answers ahead of, so its
  features would go blind in our slot tests; the log line `[QOL/NativeInput] Native normalized controller input
  active` says which path is live.
- Log noise: the 0xFABE0 safety-check error at every pickup.
- Its proprietary licence: we pin and credit, we never copy its code.

**B. Where our controller code lives: in the mod, with one generic helper on its way to the toolkit.** Layouts, art and
the stack split stay in pd2-resurrected; no d2r-tools plugin for now; #1441's `FocusPanelWidgetSoon` moves to
`plugin/common/` after #1441 merges, and to the toolkit's `shared/plugin-common` once a second project needs it.
