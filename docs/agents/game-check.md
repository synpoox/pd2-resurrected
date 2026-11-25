# Game check (pd2-resurrected)

The generic recipe (lock, install, start, cheats, warp, shoot, close out, report) is the toolkit's
`d2r-in-game-testing` skill. Run it. This file holds only what differs here. Rules in `AGENTS.md` ("Game checks") apply.

## Setup for this repo

Set once per shell; every toolkit command below needs them:

```powershell
$env:MODKIT_PROJECT='pd2-resurrected'; $env:MODKIT_TESTING_CONFIG='C:/D2RTestSlots/modkit-testing-pd2.toml'; $env:MODKIT_RUN='<owner>'
```
Git Bash: `export MODKIT_PROJECT=pd2-resurrected MODKIT_TESTING_CONFIG=C:/D2RTestSlots/modkit-testing-pd2.toml MODKIT_RUN=<owner>`

- Slots: Slot2-5 only. Install steps: `docs/agents/game-slots.md`, "Steps" 3-4. `plugin\build.ps1` builds against the toolkit's PluginSDK.
- No free slot: stop and file a `ready-for-agent` issue with the steps left (not `ready-for-human`).
- No DS1 pinning (retired with the converter, #965): skip skill steps 3 and 9's `force_ds1`.
- Character: reuse a level 99 save already in `%USERPROFILE%\Saved Games\Diablo II Resurrected\mods\PD2Resurrected-Slot<N>`. Only if none exists, make one with the skill's "The test character". No mercenary.
- Cheats: `cheat nogethit 1`, `cheat speedup 300`, `cheat god 1`. Before spawning an item that must survive Save and Exit, `cheat savecheatitems 1` first (it applies at spawn time).

## Proof

- One setup, one action, one proof. Further checks go to the owner as exact steps.
- Plugins log no per-hit, missile, kill or drop line (#2101). Prove a behaviour by item or unit state, a `core-ui` / `core-rules` status counter, a console-command reply, or one screenshot.
- Look at a screenshot at reduced size or zoomed to the region that matters. Open each one once. Report in words.
- Tooltips and monster name plates are the owner's (hover reads fail through `game_ctl`).
- Remove any test log line before the PR.

## Plugin test commands

Full syntax: `docs/console-commands.md` (grep the command, don't read the whole file).

- **Map event**, one shot for the next map opened: `core-events event wanderer|mendeln|treasure|horazon|gheed|altar|spire|tomb|invaders` (`off` clears). Log: `rolled event <n> ..., console event`. Skips the quality roll. A map is any `cmap` item (type 245).
- **Kill a boss**: never click it down, and never judge its life from god-mode hits. `core-events kill <class>` (or `kill unit <id>`) kills the nearest one through the normal death path (treasure class rolls; `core-maps.log` line `test aid: kill: class .. unit ..`). `core-events life <pct> unit <id>` sets its life. Spawn a superunique with `cheat monster super:<row>`.
- **Corrupt / desecrate**: item on the cursor (or inventory cell 0,0), then `core-events corrupt` or `core-events desecrate` (amulet already corrupted; a failed roll can be run again).

## Driving the pad

- Build d2r-test-harness from its origin/main in a scratch detached worktree, never the main harness checkout.
- `tools/pad.py --slot N press <BTN>`, 150 ms presses. Run `game_ctl.py --slot N release` before the first press (d2r-test-harness#4). After that, no mouse input through `game_ctl`.
- Close every panel first: START opens the inventory only when none is open. No grid has focus until the first D-pad press ("Hold to Sort" shows when it does; focus is a green cell).
- LT/RT page the main menu tabs, LB/RB the skill tabs (with Controller QoL, LB is the item modifier). Use the D-pad for cells, not the right stick.
- Stand-in art: identify spawned items by tooltip, not icon.

## Items and places not to test with

- Retired items: `uba`, `ubaa`, `ubab`, `ubac`, `ubtm`, `rtmf`. They don't split or stack like live items. Use runes (`r01` to `r33`).
- A stack spawned above its `maxstack` is refused by D2R itself; it proves nothing.
- Stash: `cheat goto 1` and walk to the real town stash; Click the chest and tabs. `core-ui stash personal` and a `cheat ospawn 267` chest take no harness clicks and break move hotkeys.

## Worktree hygiene

Remove a worktree only with toolkit `tools/worktree_remove.ps1 -Path <worktree> [-DeleteBranch]`. `git worktree remove --force`, `rm -r` and
`gh pr merge --delete-branch` follow the `pd2-data` junction and empty `C:\pd2r-cache`. Unlink `pd2-data` before any harness cleanup, and commit or delete scratch files first.
