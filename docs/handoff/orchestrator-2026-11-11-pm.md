# Orchestrator handoff, 2026-11-11 late: the Cartographer workstream

This branch holds only this note and is never merged.

It covers one new workstream, **Cartographer**: a standalone map-system plugin for any D2R mod. The HD-parity lanes (P7–P10, PRs #876 #878 #912 #914 #915 #922 #923 #607) were not touched in this session. Their state and next steps are unchanged from `handoff/orchestrator-2026-11-11-day2`. Keep that branch: it is not superseded.

## 1. Start here

1. Run the repo skill **`/orchestrator`**: `.claude/skills/orchestrator/SKILL.md` on main.
2. Read the Cartographer plans on branch **`docs/map-kit-plan`**. It is an owner reference branch: no PR, never merged to main.
   - `docs/plans/cartographer-plugin.md` is the main plan. Section 3 (the author tables) and section 5 (phases) are what you dispatch from.
   - `docs/plans/cartographer-outdoor-builder.md` is the outdoor builder, phases S0–8.
3. Model rules are unchanged, strict and enforced by the owner:
   - implementers: **Sonnet, `effort: medium`**;
   - reviewers: **Opus, `effort: medium`**;
   - never swap models, and the cap is 5 implementers.

## 2. The goal (owner, 2026-11-11)

**What Cartographer does.**
- It is a fully standalone D2RLoader plugin.
- A map item used in Harrogath opens a red portal into a map. There is **one map per game**.
- Each map has **one tileset**. Every game builds a new procedural layout from that tileset, inside a named **shape** (Corridor, Winding, Branching, Sprawl).
- Monster, area and player map stats.
- Map events.

**Simplicity is a hard requirement.** Someone with little modding experience must be able to make a map:
- one required table, `maps.txt`, needing 3 cells per map;
- plain words, no ids;
- plain-English load errors;
- no edits to vanilla tables.

The release is gated on a **novice test** (plan §3.0 and phase 8).

**Author-side decisions (owner, 2026-11-11 late).** These are all in plan §3:

- **The tileset decides the builder.** Authors never choose indoor or outdoor.
  - Each built-in tileset name is tagged maze or outdoor in the shipped catalogue: `Cave` builds a maze, `Jungle` builds outdoor.
  - An option-C piece set inherits the builder of its `Base` tileset.
- **Shapes:** `Corridor`, `Winding`, `Branching`, `Loop` and `Sprawl`. The numbers behind each are hidden from authors; experts can override them in `shapes.txt`.
- **`mapstats.txt`** is one shared pool of properties, with columns `Target` (Monsters/Players), `Stat`, `Min`, `Max` and `Weight`.
  - Every map draws **6** properties. That is the default in `cartographer.txt`; a map overrides it with a `Properties` column.
  - A property's text comes from the game's itemstatcost stat description.
  - Area effects are fixed per map, as the `maps.txt` columns `Density`, `Champions` and `Uniques`. That split was the session's call, not the owner's; confirm it with the owner if it matters.
- **`mapevents.txt`.** Each map has a **20%** event chance (`EventChance`). Five generic, hardcoded kinds: `Boss`, `Ambush`, `Horde`, `Treasure` and `Shrine`. Everything else about an event is data. Scripted PD2-style events are out of scope.
- **Maze authoring options** (§3.6):
  - **A.** Pick a built-in tileset.
  - **B.** Add DS1 variants. These also show in vanilla levels of that tileset.
  - **C.** A private 15-piece set through `tilesets.txt` (`Name`, `Base`, `First`), which needs a piece-picker hook on `0x3eb960` for the slot level.
- **Test commands:** `cartographer check` and `cartographer open <Name>`.
- **Every tileset becomes map-ready over time** (plan §3.5 table, phases 10+). The release-1 list is only the rollout order.
- **Tilesets will be mixed within one level** (D9). **Zone warping is ruled out.** This is the future research phase 9, which starts **only after phases 1–8 ship**. Don't dispatch it in waves 1–3.
- **The worked example** in §3.5a (Shorelines = Jungle/Corridor, Tunneling Depths = Cave/Loop) is the README's acceptance case.

**Out of scope.** PD2-Resurrected does **not** migrate to Cartographer, and core-maps stays. Cartographer and core-maps are never loaded together. There is no content interface for other plugins. Maps are not erased or rebuilt within a game.

## 3. State at handoff

**main** is **97489f6**. No Cartographer code is on main. The plans' latest commit on `docs/map-kit-plan` is **785935e**.

**Every game slot is free** (2–11, checked 2026-11-11 late). No agents are running from this session.

**Research is done.** Both spikes reported, and both PRs are open docs-only PRs:

| PR | Issue | Verdict | Next step |
|---|---|---|---|
| #929 `docs/drlg-steering-spike` | #928 | Maze (indoor) layouts **can be steered**, game-checked on Cave, Crypt and Lava. D2R's outdoor builders **cannot** build a map level; only the Act 3 jungle route of levels 76–78 is steerable. | Merge, once the owner allows it (§5). |
| #930 `claude/level-regen-spike-117061` | #927 | Rebuilding a level in a running game **works with caveats** (20 rebuilds, new game, Cold Plains). Now background only, since there is one map per game. Still important: D2R rebuilds an empty level on the next visit, so builds must be deterministic. | Merge, once the owner allows it (§5). |

**Prototype branches.** These are never merged and are reference only:
- `spike/cartographer-steer-proto`: the guided-maze hooks on `0x3271c0`, `0x3e96d0` and `0x3eb850`, with byte checks;
- `spike/cartographer-level-regen`.

**Other owner reference branches.** `docs/ported-features` holds the inventory of PD2 hardcoded features we ported.

**Leftover worktrees,** from the spike sessions (each has its own session in the app):
- `.claude/worktrees/handoff-orchestrator-dispatch-aa0df4` on `docs/drlg-steering-spike`;
- `.claude/worktrees/pd2-resurrected-pr-queue-b28b28` on `claude/level-regen-spike-117061`;
- this session's `.claude/worktrees/orchestrator-handoff-review-7b0ddd` on `docs/map-kit-plan`.

Remove each with `tools/worktree_remove.ps1` once its PR has merged.

## 4. Next: wave 1 (5 implementers, in parallel)

None of these issues is open yet. File each one first, as a sub-issue of a new epic, "Cartographer: standalone map plugin". The briefs below give each worker its own files, so no two workers touch the same file.

| Worker | Issue to open | Owns (files) | Needs game? | Needs Ghidra? | Done when |
|---|---|---|---|---|---|
| A | Phase 1 skeleton | `plugin/cartographer/` (not `layout/`), and `plugin/CMakeLists.txt` | Yes, at the end | Little | A 3-cell `maps.txt` opens a built-in preset map through the item and through `cartographer open <Name>`; a typo gives a plain-English error in `cartographer-report.txt` and `cartographer check`; a second open is refused; `cartographer.txt` defaults are 6 properties and 20% events |
| B | Phase 2 layout graph | `plugin/cartographer/layout/`, `tests/cartographer/` (its own CMake; **don't edit `plugin/CMakeLists.txt`**) | No | No | 10k seeds per shape preset (all five, `Loop` included) meet their rules; pure C++ with no engine headers; deterministic PCG/splitmix |
| C | Outdoor S0 probe + S1 jungle spike | spike branch only, `plugin/cartographer-outdoor-spike/`, not merged; findings in `docs/research/cartographer-outdoor-s1.md` | Yes | Yes | The outdoor plan's S0 and S1 "done when" are met, and river-or-path is answered |
| D | Tileset catalogue tool | `tools/cartographer/` | No | No | Masks generated from `lvlprest.txt` and DS1s match the hand-checked masks for 530–604 (outdoor plan §3 phase 3) |
| E | Phase 0 docs | `docs/adr/`, `GLOSSARY.md`, `docs/cartographer/` (README and table-reference drafts) | No | No | ADR merged (standalone, never loaded with core-maps, one map per game, simplicity rules); the README draft "Your first map in 5 minutes" follows plan §3 and uses the §3.5a example |

**What each brief must say** (on top of the skill's rules):
- Read `docs/plans/cartographer-plugin.md`, from `git show origin/docs/map-kit-plan:<path>`.
- Cartographer must never include `plugin/common/pd2_engine.h`. Its RVAs go into the toolkit's `rvas_93847.h` or the plugin itself.
- Before any native hook, check the SDK first, and say in the code why no SDK service fits.
- Never copy code from locbones/D2RHUD-2.4: it has no license.
- Workers A and C: run game checks with core-maps switched off (`core-maps-off.txt`) or not installed.
- Worker A: decide how the slot levels and the default map item rows get into the game. Is there a D2RLoader row-injection route? Otherwise write the installer (plan §3.4).

**Shared resources serialise** (the cap of 5 is fine; more than that only queues):
- **One game at a time.** Five games at once exhausted memory commit on 2026-11-07. Only A and C use the game in wave 1.
- **One Ghidra headless query at a time** per project (project lock). Only C, and a little of A, use it.

**Wave 2, after A and B merge:**
- phase 3, guided maze (needs A, plus the steer prototype). This includes `Loop`, which links the graph's loop edges with `0x360bb0`, and the option-C piece-set hook;
- phase 4, the outdoor builder (needs A, B, C and D);
- phase 5, map stats (needs A);
- phase 6, map events (needs A).

**Wave 3:** phase 7, multiplayer, then phase 8, the release with the novice test.

## 5. Owner items

1. **Docs-only PRs can't merge without `--admin`.**
   - **Why:** the ruleset "table diff" requires `table-diff` and `unit`. On purpose, `gate.yml` starts no run for docs-only paths. And since #898/#908 a skipped check no longer counts as passing.
   - **Effect:** every docs-only PR sits in BLOCKED, including #929 and #930. The spike session's `gh pr merge --admin` was refused by the permission settings.
   - **Root-cause fix for the owner to choose:**
     - a docs-only job that proves the diff is docs only and posts a real `table-diff` success;
     - or a ruleset bypass for those PRs.
   - **Until then:** the owner merges docs-only PRs, or approves `--admin` per PR.
   - **No issue is open for this yet.** Open one if the owner agrees.
2. **Plans are on the owner branch `docs/map-kit-plan`.** If the owner wants the Cartographer plans on main, they go through the normal issue + PR path.

## 6. Hazards

- **D2RLoader's D2RCore owns the outdoor entry** (`0x3271c0` → `JMP [0x3e2ac38]`). Worker C's S0 probe must byte-check it on the owner's install before anyone builds on it.
- **Cross-act tilesets in an Act 5 slot level** are unchecked except for Ice in Act 1. Jungle in Act 5 is part of S1.
- **D2R rebuilds empty levels on the next visit** (#930). Every Cartographer build must give the same rooms in the same order within a game.
