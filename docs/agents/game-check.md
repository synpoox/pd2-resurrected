# Game check

An ordered checklist. A brief says "game check per `docs/agents/game-check.md`". Each rule lives in one home; this
file links to it. Test rules (Hell, level 90+, the 10-minute limit, cheat-commands) are in `AGENTS.md`, "Testing
in-game". Slot details are in `docs/agents/game-slots.md`.

1. **Lock a slot.** `game_slots.py status`, then `lock <N> <owner>` on a free Slot4-8 (game-slots.md, "Steps" 1-2).
   No free slot: stop and file a `ready-for-agent` issue with the steps left.
2. **Install the build** (game-slots.md, "Steps" 3-4).
3. **Pin the DS1 variant** when the level has several (game-slots.md, "Force a DS1 variant"), game closed.
4. **Start the game** in HD on a character that meets the AGENTS.md test rules:
   `python <toolkit>/testing/game_ctl.py --slot <N> start`.
5. **Set the cheats** `cheat nogethit 1`, `cheat speedup 300`, `cheat god 1`, each through
   `game_ctl.py --slot <N> console "<cmd>"`.
6. **Warp** to each spot with `cheat goto <level>,<x>,<y>` (formula: `cheat-commands.md`).
7. **Shoot** with `game_ctl.py --slot <N> shot`: HD, then `key g` for legacy and `shot` again. Look at each
   screenshot for a held cursor item before the next click. Visual changes get before/after pairs.
8. **Upload the images** to the orphan `issue-assets` branch under `<issue>/` (a scratch checkout without
   junctions), push, and link them on the issue and the PR. Keep that branch.
9. **Close out** right after the check: `game_ctl.py --slot <N> stop`, `pd2_force_ds1.py --slot <N> --reset` if you
   pinned, then, with `MODKIT_RUN=<owner>` set, `game_slots.py unlock <N> <owner>`.
10. **Report** what you saw per spot, with the image links.

## Worktree hygiene

Remove a worktree with `tools/worktree_remove.ps1 -Path <worktree> [-DeleteBranch]`: it unlinks every junction first and
checks the shared cache is intact. Never delete a worktree any other way: `git worktree remove --force` and `rm -r`
follow the `pd2-data`, `generated` and `out` junctions and empty `C:\pd2r-cache`.
