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
