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

5. **Copy what the stash world open does** (lane 1456b, 2026-11-18, slot 5). Stash path: Bank object OperateFn 32 ->
   `0x528270` -> `FUN_140480650(client, 0x10)` sends packet 0x77 -> the client opens UI 0x10 -> the BankPanel open
   handler `FUN_14023cc40`. In pad mode (`FUN_140077e10`) that handler resets the input state (`FUN_14008d540`),
   fetches the existing ControllerOverlay panel and, only if it is attached, refreshes it (`0x1f6e50(ov, int ov+0x438)`,
   `0x1f5d60(ov)`, `0x1f5a80(ov, 0x18)`, `0x1f5a80(ov, 1)`), then focuses the panel (`FUN_140858050`). Route 2 did only
   the last. `altar_panel.cpp` now does the same one frame after the altar and the inventory beside it are both active
   (panel +0x51), clearing the inventory's focus first (`0x857c00`, as the switcher does), by byte signature; each
   signature matches once in the 93847 dump. It logs the focus manager's +0x198 (`FUN_140846020()`) a few frames later
   against the altar panel.
   Result of the first version (it dispatched `OpenPanel:ControllerOverlay` and focused straight after the open): the pad
   frame and help bar showed and the D-pad drove the panel, but the first DOWN gave "Hold to Sort" (a grid focus), not the
   confirm button; LEFT then lit it. Cause guessed: the inventory opens after the focus and takes it.
   Second version (slot 3, 2026-11-18): the altar and the inventory are waited for, the inventory's focus is cleared, the
   overlay is only refreshed when attached, never opened. The focus manager's +0x198 held the altar panel three frames
   after the focus (log "same"), yet the pad frame and help bar did not show (the overlay was not up), DOWN highlighted
   nothing, and B did not close the panel. The stash's decompile (0x23cc40) does build an OpenPanel message for
   "ControllerOverlay" in pad mode, so the code now opens it when it is not up and refreshes it otherwise; that variant is
   untested. Slot 5 was taken by another lane, so no second try ran.
