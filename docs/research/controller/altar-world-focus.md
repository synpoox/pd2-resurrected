# Altar opened in the world: pad focus (#1456)

Build 93847. Findings so far; the issue stays open.

## Root cause
A on the altar runs `openPanel` (`PanelManager:OpenPanel:<panel>`), which skips the controller UI switcher. D2R's switcher
(`UISwitcherPanel`, message handler `FUN_14027df80`, tab change `FUN_14027f2f0(switcher, tabIndex, dir)`) is what
sets pad focus: on a tab change it clears the old panel's focus (`FUN_140857c00`), opens the new panel by id
(`FUN_1400cd7c0`), then focuses it (`FUN_140858050(panel)` -> `FUN_1408682b0(focusManager, panel)`).
START sends `PanelManager:OpenPanel:UISwitcher` (`FUN_14011b490`); the switcher holds the tab panel ids at +0x168,
their count at +0x188 and the current index at +0x1b8. `FUN_140846170(name)` is `PanelManager::FetchPanel`; a panel's
active flag is at +0x51 (`FUN_1408682b0` asserts, an int3, on a null or inactive panel).

## Routes tried
1. **Open through the switcher** (START's message, then `FUN_14027f2f0` to the altar tab): not tried in game. Needs the
   switcher object (`FetchPanel("UISwitcher")`) and the altar tab's index in its list at +0x168, and the altar's tab id is
   assigned by the loader at route registration (not found yet). This is the next step.
2. **Focus the already open panel** (plugin, UI thread, frame callback): after `openPanel`, call
   `FetchPanel("core-maps/SacrificeAltar")` and, once +0x51 is set, `FUN_140858050(panel)`. Slot 3, level 99 Assassin,
   town, `cheat ospawn 621` altar, pad A: log `altar: pad focus set, 0 frames after the open` (no crash), but D-pad DOWN
   did nothing (a character walked in the world behind it; no confirm-button highlight), and there is no help bar.
   Via the route (START, LB to the altar tab) the same DOWN highlights the confirm button and the help bar shows.
   The focus manager takes the call, but the world open still leaves the pad in world input. Code not kept.
3. **Layout only** (`defaultWidget`, `navigation`): not a fix. The layout is the same for both opens; the route open
   focuses and the world open does not, so the difference is outside the layout.
4. `WidgetService::focusWidget`: crashes on the installed loader (given); not used.

5. **Copy what the stash world open does** (lane 1456b, 2026-11-18, slot 3, not proven). Stash path: Bank object
   OperateFn 32 -> `0x528270` -> `FUN_140480650(client, 0x10)` sends packet 0x77 -> the client opens UI 0x10 -> the
   BankPanel open handler `FUN_14023cc40`. In pad mode (`FUN_140077e10`) that handler does three things the plugin
   open does not: resets the input state (`FUN_14008d540`), opens the ControllerOverlay panel (help bar and legends,
   `FUN_1401f6e50`, `1f5d60`, `1f5a80(overlay, legendId)`), and focuses the panel (`FUN_140858050`). Route 2 did only
   the last. `altar_panel.cpp` now does all three one frame after `openPanel`, once the panel's +0x51 is set, resolving
   them by byte signature (PanelFocus, PanelFetch, InputReset, PadMode). Not done: the legend calls, and the stash's
   own `UIOpen(1)` inventory open (the stash also opens the inventory beside it).
   Check: PanelFocus and InputReset each matched more than once in the exe, so the scanner refused them and the focus
   code did not run (log: `altar: signature PanelFocus found more than once, no pad focus`). The open itself worked
   (panel shown, inventory beside it). The pad result is therefore still unknown. Next: lengthen both patterns
   until unique (or anchor on a neighbouring unique call), then rerun the pad check.
   Update (same day, slot 5, pad): the lengthened PanelFocus and InputReset patterns are now unique in the 93847 dump
   (each matches once; PanelFetch and PadMode also once), and the focus ran: log `altar: pad mode 1, focus signatures ok`,
   `altar: pad focus set 0 frames after the open`. PadMode (`0x77e10`) returns 1 when the pad is active. Result: the pad
   frame and the help bar now show and the D-pad drives the panel instead of the character (DOWN then gave "Hold to
   Sort", a grid focus; LEFT then lit the confirm button). It is not the route's result: the first DOWN did not land on
   the confirm button. Not closed.