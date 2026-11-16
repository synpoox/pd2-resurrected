# Game check

An ordered checklist, run after the PR is open (order: `AGENTS.md`, "PR order"). A brief says "game check per `docs/agents/game-check.md`". Each rule lives in one home; this
file links to it. Test rules (Hell, level 90+, the 10-minute limit, cheat-commands) are in `AGENTS.md`, "Testing
in-game". Slot details are in `docs/agents/game-slots.md`.

1. **Lock a slot.** `game_slots.py status`, then `lock <N> <owner>` on a free Slot2-5 (game-slots.md, "Steps" 1-2).
   No free slot: stop and file a `ready-for-agent` issue with the steps left.
2. **Install the build** (game-slots.md, "Steps" 3-4). `plugin\build.ps1` initialises an empty `third_party/PluginSDK` submodule itself.
3. **No DS1 pinning** (retired with the converter, #965): levels are D2R's own or Cartographer's.
4. **Start the game** in HD on a character that meets the AGENTS.md test rules:
   `python <toolkit>/testing/game_ctl.py --slot <N> start`.
5. **Set the cheats** `cheat nogethit 1`, `cheat speedup 300`, `cheat god 1`, each through
   `game_ctl.py --slot <N> console "<cmd>"`.
   Before any spawn whose item must survive a Save and Exit, also run `cheat savecheatitems 1`: cheat-spawned items are
   dropped from the save otherwise (any item, not only maps). The flag applies at spawn time: items spawned before it
   are still dropped.
6. **Warp** to each spot with `cheat goto <level>,<x>,<y>` (formula: `cheat-commands.md`).
7. **Shoot** with `game_ctl.py --slot <N> shot`: HD, then `key g` for legacy and `shot` again. Look at each
   screenshot for a held cursor item before the next click. Visual changes get before/after pairs.
8. **Upload the images** to the orphan `issue-assets` branch under `<issue>/` (a scratch checkout without
   junctions), push, and link them on the issue and the PR. Keep that branch.
9. **Close out** right after the check: `game_ctl.py --slot <N> stop`, then, with `MODKIT_RUN=<owner>` set, `game_slots.py unlock <N> <owner>`.
10. **Report** what you saw per spot, with the image links.

## Forcing a map event

Console command (core-events, #998), one shot for the next map opened: `core-events event wanderer|mendeln|treasure|horazon|gheed|altar|spire`
(`core-events event off` clears it). Run it through `game_ctl.py --slot <N> console "core-events event gheed"` before using the
map item; the log shows `rolled event <n> ..., console event`. A forced event skips the quality roll (the event is the one named, whatever the map's quality). A map is any item of type `cmap` (item type 245).

## Screenshots that settle the question

One shot should answer the question. Before each shot:

- **Quiet spot.** Warp to a spot with no monsters, or repeat `cheat killmonster` (or `cheat monfreeze 1`), and shoot
  once nothing casts or burns near you.
- **Structure shots** (walls, edges, floors) in the dark: `cheat stat item_lightradius, 25`. Never for lighting shots.
- **Water under canopy:** `cheat-commands.md` lists no canopy or tree toggle. Warp to where the canopy is thin, or
  say in the report that the canopy hid it.

## Worktree hygiene

Remove a worktree with `tools/worktree_remove.ps1 -Path <worktree> [-DeleteBranch]`: it unlinks every junction first and
checks the shared cache is intact. Never delete a worktree any other way: `git worktree remove --force`, `rm -r` and
`gh pr merge --delete-branch` follow the `pd2-data` junction and empty `C:\pd2r-cache`. Agents also unlink `pd2-data` before any removal by the
harness (unverified whether its auto-cleanup follows junctions), and commit or delete scratch files first.
