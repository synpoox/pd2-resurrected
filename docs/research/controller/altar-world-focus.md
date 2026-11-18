# Altar opened in the world: pad focus (#1456)

Build 93847. Fixed by lane 1456c (section "The fix"); routes 1-5 are the earlier tries.

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

Third version (2cca9eee: open the overlay if absent, else refresh; wait for the inventory; clear its focus; focus the altar), slot 4, pad check:

- A: the panel opens and the bottom bar shows "B Close Menu". No pad frame is drawn. Log: `pad focus set 0 frames after the open (overlay not up)`, `focus manager holds ... (same)`. The focus manager does hold the altar panel.
- DOWN: nothing visible moves, no green highlight on the confirm button. The bar changes to "Hold to Sort / B Close Menu", which is the inventory bar.
- B: the panel stays open (FAILED). The left stick was not tried while it was open.
- A again: not run (step 3 failed).

Reading: the focus manager points at the altar, but the pad's input does not reach it, and the overlay open does not take (it still reads "not up" and no frame appears). The first version, which focused right after the open message, did show the frame. The open-if-absent timing (focusing 0 frames after the open) looks too early for the overlay. Stopped here per the last cycle's rule.

## The fix (lane 1456c, slot 4, 2026-11-18)

Three causes, each found in Ghidra and then checked in game:

1. **No pad frame.** `FUN_1401f60c0` is the ControllerOverlayPanel type descriptor, not an "attached" test: the stash's loop over
   `vtable+0x58` is a type check, so the stash always refreshes the overlay it just asked for. v2 and v3 refreshed only when
   the overlay's +0x51 was set, which it was not yet. The frame is the overlay's `Background` widget, set to the two-panel frame
   by `0x1f6e50(ov, ov+0x438)`.
2. **DOWN going to the inventory.** The focus manager (`FUN_140846020()`) keeps the focused panel at +0x188, its widget at +0x190
   and the last panel at +0x198; its input handler `FUN_140866eb0` sends a D-pad direction to the +0x190 widget's
   `vtable+0x68`. With no widget there it moves to a neighbouring panel (`FUN_140868470`), which is the inventory. `0x8682b0`
   takes the panel's remembered widget (+0x160) or the first focusable one (`FUN_140857d70`). Focusing once the overlay is up
   gives the amulet slot (log `widget` non-null), and DOWN then follows the layout's `navigation.down` to `confirm`. The
   remembered widget is cleared before each focus so every open starts on the slot.
3. **B doing nothing.** D2R's panels close themselves on controller action 0x17 ("back") in their own OnMessage: BankPanel
   `FUN_14023caa0` (`*(int*)(msg[0x22]+0x10) == 0x17` -> `FUN_1400c7d30(0x18)`), UISwitcher `FUN_14027df80`. The message is
   InputMessage (field 0 hash `0x4ae067c6248b6042`) with ControllerActionBegin (field 0x11 hash `0x1e439768f7dd4594`). The SDK
   panel is a plain Panel whose OnMessage (Widget's, `FUN_140856b00`) only passes messages to its children. The SDK has no pad
   input service, so the altar panel object alone gets a copy of its vtable (17 methods) with OnMessage (slot 4) wrapped: on
   back in pad mode it closes the altar, the inventory and the overlay on the next frame. No game code is patched.

Order in `altar_panel.cpp`: both panels active for 2 frames, input reset, `OpenPanel:ControllerOverlay` when it is not up (so a
second open adds no second help bar), the next frame with the overlay up: refresh, clear the inventory's focus, focus the
altar, wrap OnMessage. New signatures `WidgetFocus` (0x868230) and `WidgetFind` (0x855c90) match once in the dump; the slot
fallback they serve did not fire in the check (the panel focus already picked the slot).

Pad check with the final build, 3 opens in one session (Barbarian level 99, town, `cheat ospawn 621`): A opens with the
two-panel frame and one help bar ("Hold to Sort / B Close Menu"), the slot frame lit; the first DOWN lights the confirm button
(Transmute hover art) and the bar reads "B Close Menu"; B closes every panel (log `closed by B`); the left stick walks after.
Earlier builds of this lane: 4 more opens; one DOWN press showed no move, and before the remembered widget was cleared a
reopen started on the confirm button. A mouse open logs `pad mode 0` and runs none of the pad steps.

Review cycle: every close after a pad open now does what the stash's close does in pad mode (`0x23c2e0`): clears the altar
panel's focus (`0x857c00`) and sends `PanelManager:ClosePanel:ControllerOverlay` (`PadCleanup`, from `CloseAll` on confirm
and B, and from the frame watcher for X or Escape). B is skipped when the message is already handled (`0x8460d0`) and marked
handled after (`0x843fc0`), as BankPanel does. Slot 4 check: pad pickup and place of an amulet, DOWN, A on confirm (an
uncorrupted amulet: `desecration: refused`, amulet back in the inventory), B, and Escape then the pad; each time the log reads
`pad close: focus cleared, controller overlay closed`, the frame and help bar are gone and the left stick walks.
