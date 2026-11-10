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
   pinned, then `game_slots.py unlock <N> <owner>`.
10. **Report** what you saw per spot, with the image links.

## Worktree hygiene

`pd2-data`, `generated` and `out` in a worktree are junctions into the shared cache (`C:\pd2r-cache`, or
`F:\pd2r-cache`). Before any worktree removal, unlink each with `cmd /c rmdir <worktree>\<name>`, then
`git worktree remove --force`. Never delete through a junction, and never delete a cache folder another worktree's
`pd2-data` points at: `--force` follows junctions and empties the cache. Afterwards check that
`C:\pd2r-cache\extract` still has files.
