# Game check

The steps for checking a change in game. A brief says "game check per `docs/agents/game-check.md`". Slot setup details
(the machine layout, the shared cache, `pd2_force_ds1`, the edge tour) are in `docs/agents/game-slots.md`.

1. **Read** `d2rAI-toolkit/knowledge/cheat-commands.md` and origin/main's AGENTS.md (a worktree copy can be stale).
   Use console cheats, not clicking or walking.
2. **Lock a free slot.** Set `MODKIT_TESTING_CONFIG=C:\D2RTestSlots\modkit-testing-pd2.toml`, run
   `python <toolkit>/testing/game_slots.py status`, pick a free slot from Slot4-8, then `lock <N> <owner>`.
   No free slot: stop and file a `ready-for-agent` issue.
3. **Build and install.** `plugin\build.ps1`, `python tools/pd2_extract.py`, `python tools/pd2_merge.py`, then with
   `MODKIT_RUN=<lock owner>`: `tools\install.ps1 -Game C:\D2RTestSlots\Slot<N> -Data . -NoValidate`.
4. **Pin the variant** when the level has several DS1s: `python tools/pd2_force_ds1.py --slot <N> "<level>=<ds1>"`
   with the game closed. Undo it with `--reset` before you unlock.
5. **Start the game** with a Reign of the Warlock character of level 90 or higher, in a Hell game, in HD.
6. **Set the console cheats:** `cheat nogethit 1`, `cheat speedup 300`, `cheat god 1`.
7. **Warp, do not walk:** `cheat goto <level>,<x>,<y>` with x = OffsetX*5 + tileX*5 + 2 (y alike; offsets from the
   merged `levels.txt`).
8. **Shoot** HD, then press `g` for legacy and shoot again. Check each screenshot for a held cursor item before the
   next click. Visual changes get before/after pairs.
9. **Upload the images** to the orphan `issue-assets` branch under `<issue>/` (check it out in a scratch folder
   without junctions), push, and link them on the issue and the PR. Never delete that branch.
10. **Stop the game and unlock** the slot right after the check: `game_slots.py unlock <N> <owner>`. Never leave a
    game idle.
11. **Report** what you saw per spot, with the image links.

**Stuck:** past 10 minutes with no significant progress on what you check, stop the game, unlock the slot, and file a
`ready-for-agent` issue with the steps left and what you already verified.

## Worktree hygiene

`pd2-data`, `generated` and `out` in a worktree are junctions into the shared cache (`C:\pd2r-cache`, or
`F:\pd2r-cache`). Before any worktree removal, unlink each with `cmd /c rmdir <worktree>\<name>`, then
`git worktree remove --force`. Never delete through a junction, and never delete a cache folder another worktree's
`pd2-data` points at: `--force` follows junctions and empties the cache. Check that `C:\pd2r-cache\extract` still has
files afterwards.
