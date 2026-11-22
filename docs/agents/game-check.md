# Game check

An ordered checklist, run after the PR is open (order: `AGENTS.md`, "PR order"). A brief says "game check per `docs/agents/game-check.md`". Each rule lives in one home; this
file links to it. Test rules (Hell, level 90+, the 5-minute stuck rule, which is not a time limit, cheat-commands) are in `AGENTS.md`, "Testing
in-game". Slot details are in `docs/agents/game-slots.md`.

Every toolkit command below runs with `MODKIT_PROJECT=pd2-resurrected` and `MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml` set in the shell, and with
`MODKIT_RUN=<owner>` (the lock owner) set for every command that locks, starts, drives or checks a slot. Without the
config the toolkit refuses to run; without the project the registry refuses the slot. The `VAR=x command` form below is for Git Bash; in PowerShell set `$env:NAME = "x"` first, then run the command.

1. **Lock a slot.** With `MODKIT_PROJECT=pd2-resurrected`, `MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml`: `game_slots.py status`, then `MODKIT_RUN=<owner> game_slots.py lock <N> <owner>` on a free slot of this repo's registry entry (Slot2-5 here; game-slots.md, "Steps" 1-2).
   No free slot: stop and file a `ready-for-agent` issue with the steps left.
2. **Install the build** (game-slots.md, "Steps" 3-4). `plugin\build.ps1` builds against the toolkit's PluginSDK, found with the toolkit.
3. **No DS1 pinning** (retired with the converter, #965): levels are D2R's own or Cartographer's.
4. **Start the game** in HD on a character that meets the AGENTS.md test rules:
   `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> python <toolkit>/testing/game_ctl.py --slot <N> start`.
5. **Set the cheats** `cheat nogethit 1`, `cheat speedup 300`, `cheat god 1`, each through
   `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> game_ctl.py --slot <N> console "<cmd>"`. God mode also kills any unit the character damages in one hit, so never judge a boss's life, stages or immunity from the character's hits; lower its life with the plugin's test commands instead.
   Before any spawn whose item must survive a Save and Exit, also run `cheat savecheatitems 1`: cheat-spawned items are
   dropped from the save otherwise (any item, not only maps). The flag applies at spawn time: items spawned before it
   are still dropped.
   Hot-path log lines (per hit, arrow, missile, kill, drop: `multiple shot:`, `crit:`, missile-hit lines) stay silent unless the game starts with `PD2R_HOT_LOGS=1` in its environment (`debug_mode` and cheats do not turn them on); set it before `start` when the check reads one (#1609).
6. **Warp** to each spot with `cheat goto <level>,<x>,<y>` (formula: `cheat-commands.md`).
7. **Shoot** only what settles the question (`MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> game_ctl.py --slot <N> shot`); log lines are the preferred proof. Take an HD,
   then `key g` legacy pair only when the change is visual in legacy. Look at each screenshot for a held cursor item
   before the next click.
8. **Never upload.** Shots stay in the slot's run folder. Never commit or push one, to any branch: `issue-assets`
   keeps old links only, nothing is added to it.
9. **Close out** right after the check: `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> game_ctl.py --slot <N> stop`, then, with the same three set, `game_slots.py unlock <N> <owner>`.
10. **Report** what you saw per spot in words and log lines.

## Forcing a map event

Every plugin console command with its syntax: `docs/console-commands.md`.

Console command (core-events, #998), one shot for the next map opened: `core-events event wanderer|mendeln|treasure|horazon|gheed|altar|spire|tomb|invaders`
(`core-events event off` clears it). Run it through `game_ctl.py --slot <N> console "core-events event gheed"` before using the
map item; the log shows `rolled event <n> ..., console event`. A forced event skips the quality roll (the event is the one named, whatever the map's quality). A map is any item of type `cmap` (item type 245).

## Killing a boss

Never click a boss down (adds soak the clicks, bosses hide, `cheat killmonster` skips the normal death). Use core-events' console commands, with `cheat god 1` on: `core-events kill <class>` (or `kill unit <id>`) kills the nearest living monster of that merged monstats class in your level through the normal death path, so death listeners run and its treasure class rolls, the player gets no kill credit; the log (`core-maps.log`) gets one `test aid: kill: class .. unit ..` line. `core-events life <pct> unit <id>` sets the life of a monster in your level (`life <pct> <class>` still covers ported bosses only). Both skip a player's merc, golem and summons. Spawn a superunique with `cheat monster super:<row>` (`cheat mspawn <name>` does not spawn one); the unit id is in the kill or life log line.

Corrupt and desecrate an item without the cube or the altar panel: put an item on the cursor (or leave it at inventory cell (0,0); the cursor wins), then `core-events corrupt` runs the normal Corruption roll on it (the cube's code; log line `corruption: ... by cubemain row N`), and `core-events desecrate` runs the altar's Desecration on a corrupted amulet: it sets Lucion's stage to 8 first, the roll sets it to 9, and the log gets the normal `desecration:` lines (the roll can fail at 50-60%: run it again, nothing is bricked). Both log `test aid: ...` lines, and a clear one when there is no item.

## Screenshots that settle the question

One shot should answer the question. Before each shot:

- **Quiet spot.** Warp to a spot with no monsters, or repeat `cheat killmonster` (or `cheat monfreeze 1`), and shoot
  once nothing casts or burns near you.
- **Structure shots** (walls, edges, floors) in the dark: `cheat stat item_lightradius, 25`. Never for lighting shots.
- **Water under canopy:** `cheat-commands.md` lists no canopy or tree toggle. Warp to where the canopy is thin, or
  say in the report that the canopy hid it.

## Driving the pad

Three lanes spent 5 minutes stuck without pad focus before they got it. These steps are what they lacked.

- Build d2r-test-harness from its origin/main in a scratch detached worktree. Never use the main harness checkout; it holds someone else's work.
- Press buttons with `tools/pad.py --slot N press <BTN>` (its README documents it). Use 150 ms presses.
- The game follows the last device used. Once the pad check starts, do not click or move the mouse through `game_ctl`.
- Run `game_ctl.py --slot N release` before the first pad press: with the harness active the pad gets no grid focus ([d2r-test-harness#4](https://github.com/synpoox/d2r-test-harness/issues/4)).
- Close every panel first. START opens the inventory only when no panel is open. Pressed while a panel is open, it closes that panel.
- After a panel opens, no grid has focus until the first D-pad press. The help bar shows "Hold to Sort" once a grid has focus.
- Focus shows as a green cell highlight, plus a tooltip on an occupied cell.
- LT and RT page the main menu tabs. LB and RB page the skill tabs. With Controller QoL, LB is the item modifier by default.
- The right stick moves a free cursor at a speed that is not linear, so you cannot land it on one cell. Use the D-pad for cell focus.
- Spawned items can show stand-in art. Tell them apart by their tooltip after focusing, not by their icon.
- If focus is still stuck after 5 minutes with no progress, stop and hand off. Do not try workarounds.

## Items not to test with

- **Retired items:** PD2's Uber Ancients map and sigils (`uba`, `ubaa` Sigil of Madawc, `ubab`, `ubac`), the Uber Tristram key `ubtm` and the Bone Fragment `rtmf` are unused in game. They don't drop, their recipes are off, and they stay only so old saves load ([deviations](../deviations.md), #1585). A cheat can still spawn them, but they don't split or stack like live items. Never use them to check stacks, splits or the stash: use a live material such as keys (`pk1`), essences (`tes`) or runes.
- **Over-max spawns:** a stack spawned above the item's `maxstack` (tooltip "Quantity: 120 of 50") is refused by D2R's own merge rule. It proves nothing about our stacking code.
- **Stash clicks:** harness clicks can't put items into a stash opened with `core-ui stash personal` or a `cheat ospawn 267` chest. Walk to the real town stash.

## Worktree hygiene

Remove a worktree with the toolkit's `tools/worktree_remove.ps1 -Path <worktree> [-DeleteBranch]`: it unlinks every junction first and
checks the shared cache is intact. Never delete a worktree any other way: `git worktree remove --force`, `rm -r` and
`gh pr merge --delete-branch` follow the `pd2-data` junction and empty `C:\pd2r-cache`. Agents also unlink `pd2-data` before any removal by the
harness (unverified whether its auto-cleanup follows junctions), and commit or delete scratch files first.
