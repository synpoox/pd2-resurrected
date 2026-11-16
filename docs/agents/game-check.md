# Game check

An ordered checklist, run after the PR is open (order: `AGENTS.md`, "PR order"). A brief says "game check per `docs/agents/game-check.md`". Each rule lives in one home; this
file links to it. Test rules (Hell, level 90+, the 10-minute limit, cheat-commands) are in `AGENTS.md`, "Testing
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
6. **Warp** to each spot with `cheat goto <level>,<x>,<y>` (formula: `cheat-commands.md`).
7. **Shoot** only what settles the question (`MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> game_ctl.py --slot <N> shot`); log lines are the preferred proof. Take an HD,
   then `key g` legacy pair only when the change is visual in legacy. Look at each screenshot for a held cursor item
   before the next click.
8. **No upload by default.** Shots stay in the slot's run folder. Upload to the orphan `issue-assets` branch under
   `<issue>/` (a scratch checkout without junctions, push, keep the branch) only when the brief asks, or a filed bug
   can't be shown without a picture.
9. **Close out** right after the check: `MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner> game_ctl.py --slot <N> stop`, then, with the same three set, `game_slots.py unlock <N> <owner>`.
10. **Report** what you saw per spot in words and log lines; image links only if you uploaded any.

## Forcing a map event

Console command (core-events, #998), one shot for the next map opened: `core-events event wanderer|mendeln|treasure|horazon|gheed|altar|spire|tomb`
(`core-events event off` clears it). Run it through `game_ctl.py --slot <N> console "core-events event gheed"` before using the
map item; the log shows `rolled event <n> ..., console event`. A forced event skips the quality roll (the event is the one named, whatever the map's quality). A map is any item of type `cmap` (item type 245).

## Killing a boss

Never click a boss down (adds soak the clicks, bosses hide, `cheat killmonster` skips the normal death). Use core-events' console commands, with `cheat god 1` on: `core-events kill <class>` (or `kill unit <id>`) kills the nearest living monster of that merged monstats class in your level through the normal death path, so death listeners run and its treasure class rolls, the player gets no kill credit; the log (`core-maps.log`) gets one `test aid: kill: class .. unit ..` line. `core-events life <pct> unit <id>` sets the life of a monster in your level (`life <pct> <class>` still covers ported bosses only). Both skip a player's merc, golem and summons. Spawn a superunique with `cheat monster super:<row>` (`cheat mspawn <name>` does not spawn one); the unit id is in the kill or life log line.

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
