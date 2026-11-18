# Map events audit: PD2 against core-events

Owner's question: are PD2's map events reverse engineered, and do our versions match (by reading the code, not by
game checks)? Static reading of PD2's `ProjectDiablo.dll` in Ghidra (project `PD2.gpr`) against main at 3c1b623e,
PR [#1131](https://github.com/synpoox/pd2-resurrected/pull/1131) and PR [#1132](https://github.com/synpoox/pd2-resurrected/pull/1132)
(head a1f111d3). No game was run. Addresses are PD2's (`0x10000000` base); monster ids PD2's unless marked "port".
TC rows are zero-based data rows of PD2's `treasureclassex.txt` (the same count that puts `Map Tier 1` at 54).

Builds on [map-system-dll.md](../../pd2/map-system-dll.md) (section 4: the roll, the spawns, the altars, Horazon),
[uber-bosses-dll.md](../../pd2/uber-bosses-dll.md) (section 5: the nihlathakMap AI, #538) and the Dark Wanderer note on
branch `research/dark-wanderer-event` (#1064).

## Answer in brief

- The roll and the spawn rules of all nine events were RE'd (MS1) and the port matches them.
- **Shadow of Mendeln is the one with a real hole.** Its AI (the waves) was RE'd and matches. Its loot was never RE'd:
  PD2's death step `0x10375fe0` drops a guaranteed `rtmv` and then 20-24 items from TC `ShadowOfMendeln`. Main drops
  nothing (the quiet kill sets no-drop), and PR #1132's monstats TC cannot fix that (section 2).
- **Treasure Fallen** drops its TC up to 9 more times on death in PD2 (`0x10378d10`); the port drops it once.
- **Invaders** roll but spawn nothing (known, MS10).
- **Dark Wanderer** lacks its shard drop on main; PR #1131 adds it (4-6, owner's count; PD2 4-10).
- Horazon keeps one unsettled PD2 extra: a 10% roll (`0x10381510`) for one more item.

## 1. Per event

`map_events.cpp` is `plugin/core-events/map_events.cpp`. "RE'd" cites where PD2's side was read.

| Event (value) | PD2 | Ours | Verdict | RE'd |
|---|---|---|---|---|
| Roll | `0x10383060`: 34/17/11% by quality, forced by `map_force_event`, 40 draws of `rand() % 12` | `plugin/common/event_roll.h:20-29`, `map_events.cpp:447-467`; native Cartographer maps only (`:195`) | matches (map scope is a deviation, row "Map events on Cartographer's native maps") | map-system §4 |
| Spawn timing | `0x1036fef0`: one-shots at `rand%100 + progress > 140`; Wanderer/Fallen `n*25 < progress`, 25% | `map_events.cpp:317-366` | matches; progress divides by the built level's rooms (deviation) | map-system §4 |
| Shadow of Mendeln (11) | spawns 922 `nihlathakMap` (`0x1036fef0`, also writes unit `+0xd0 = 11`); AI `0x1035bc60` waves; death step `0x10375fe0` drops loot | spawn `map_events.cpp:331-334`; AI `nihlathak_ai.cpp:63-148`; death `nihlathak_ai.cpp:113-117` (`KillQuietly`, no drop) | AI matches; **loot missing**; waves' champions/uniques are plain monsters | AI: uber-bosses §5; loot: **not RE'd until this note** |
| Horazon (12) | spawns 919 `summonerMap`; death `0x10377ca0`: red portal to a same/next-tier map, 6 picks of `Map Tier 1-3`, 10% `0x10381510` | spawn `:331-334`; death `horazon.cpp:77-141`: Cartographer open-extra portal, 6 picks of `Cartographer Maps` | differs by design (#959, #1028); the 10% extra is missing (section 3) | map-system §4; `0x10381510` partly here |
| Dark Wanderer (14) | 921 up to 3; stock think with the a3q7 gate NOPed; boss 85 frames after leaving; 4-10 `wss` at ilvl 131 from state-213 bosses (`0x10379950`) | spawn `:335-337`; `lane-c/wanderer.cpp` (think, boss, buff) | matches except the **shards, missing on main** (PR #1131: 4-6, owner decision) | dark-wanderer-event note |
| Treasure Fallen (15) | 945 up to 3, `Fallen` monai row with PD2's own think `0x1035a3c0` (drops when hit, flees); death `0x10378d10` drops the TC `9 - n` more times | spawn `:335-337`; data TC `Treasure Fallen H` only, D2R's Fallen think | **differs**: one drop instead of 10 (section 5) | this note (section 5) |
| Spire of Darkness (16) | altar 593 + 3 fog, operate 79, waves of 993-995, end past progress 90 (`0x1036eb30`) | `map_events.cpp:236-311`, `:390-398` | matches (approximated row "Spire of Darkness") | map-system §4 |
| Gheed (18) | 147 `gheed` + marker 594; files players for the realm (`0x10368480`) | `:331-364` | matches; realm list cut (deviation) | map-system §4 |
| Altar of the Catalyst (19) | altar 611, guardian 1064 on first use, clones die with it (`0x10378080`), 2-3 `iwss` at ilvl 90 by the altar | `:400-440`, `:670-709`, guardian heard in `horazon.cpp:87-99` | matches; shards by the user at the user's level (deviation) | map-system §4 |
| Invaders (20) | 1182 `InvaderPortal` up to 3, the n-th when `25 + n*20 < progress`, 25% | rolled, no branch in `SpawnEvent` (`:331-339`): nothing spawns, no notify | **missing** (deviation "not yet", MS10) | map-system §4 (spawn only; AiThink4 not read) |
| Void / Tomb (21) | gold portal 586 linked to level 207, marker 594 | `:348`, `:360`; red portal to Cartographer's Bitter Depths; re-rolled to none while it is not built (`:461`) | matches (deviation row "Void/Tomb") | map-system §4 |
| Butcher (13), 17 | never drawn | never drawn (`event_roll.h:12`) | matches | map-system §4 |

The unique-map values (0x1E-0x24) and dungeons (9) belong to PD2's own map items, cut with #964; not audited here.

## 2. Shadow of Mendeln in depth

### Spawn (`0x1036fef0`)

Event 11, spawn count `game+0x2628` 0, `rand() % 100 + progress > 140`: `0x1038a170` places class `0x39a` (922) at the
activating room's centre, writes `+0xd0 = 0xb` on the new unit (field not identified; nothing in this note reads it),
sets the count to 1 (which is also the AI's stage 1) and sends `MapEventNotifyMendeln`. Port: same
(`map_events.cpp:331-365`); the `+0xd0` mark is a noted shortcut (`:315-316`).

The monster (PD2 monstats `nihlathakMap`): AI `TrappedSoul`, `killable` 0 (players cannot kill it), no treasure class,
skills NihlathakMapMobSpawn (no server function: a visual), RathmaDeath (srvdo 169, the nova) and MonCurseCast. Port row
the same.

### The waves (`0x1035bc60`, branch `0x39a`)

Decompiled again for this note; it matches uber-bosses §5 and the port line by line. With *a*, *b*, *c* = `aip1`'s
three columns (20, 15, 10) and *i* = stage − 1:

| Stage | PD2 | Port (`nihlathak_ai.cpp`) |
|---|---|---|
| 0 | idle 25 | `:71-74` |
| 1 | wait for the target within 14 (`0x103335b0 <= 0xe`), stage 2, cast skill 1 at (0, 0) | `:87-97`, cast at the caster (deviation) |
| 2-21 (`aip2`) | normals `rand() % 8` (column < 3 gives 2), champions `rand() % 1` = 0, uniques 0 | `:121-130` |
| 22-36 (`aip3`) | normals `rand() % 6`, champions `rand() % 3`, uniques 0 | same |
| 37-46 (`aip4`) | normals `rand() % 4`, champions `rand() % 2`, uniques `rand() % 3` | same |
| any wave | none rolled: one normal. Each at boss ±10 (`rand() % 20 − 10` per axis), spot search `0x10389c20(xy, size 2, mask 0x101, reach 4, no shift)`; monster from {−1, 923, 924, 926, 927}, −1 = one of 928-931; normal `0x1037cd00`, champion `0x1037cbf0` (champion flags, mods `0x103a2280`, 3-6 minions `0x103a22c0`), unique `0x1037caf0` (unique mods `0x10313100`, 3-6 minions) | `SpawnWaveMonster` `:43-61`: same picks, **all as normal monsters**, spot from D2R's portal search (`FindFreeSpot`, size 3, mask 0x400) |
| after a wave | stage + 1; idle `aip5` (40 frames in every column); seed % 3: 0 skill 3 on the target, 1 skill 1 at its spot + 2, 2 nothing | `:136-147` |
| 47 | stage + 1, delay 100 | `:98-103` |
| 48 | cast skill 2 RathmaDeath at (0, 0), stage + 1, delay 25 | `:104-112` |
| 49 | `0x10356e00`: unit flag `0x20000` (no treasure), life 0, death mode | `:113-117` `KillQuietly` (no-drop flag, death mode) |

So in Hell about 45 waves, 40 frames apart (~72 s), a 4 s pause, the nova, and one second later the "death". Up to
about 3 normals per early wave, more champions and uniques late.

### The "death" and the loot (`0x1037a330` -> `0x10375fe0`) — new here

PD2's dead-mode wrapper `0x1037a330` runs its steps in order: `0x10375f70` (context), `0x10375fe0`, `0x103760a0`,
`0x10376380`, the class switch `0x10378d80` (no `0x39a` case), then `0x10379170`-`0x10379920`; only the last two
(`0x10379950`, `0x103799d0`) skip a unit with flag `0x20000`. `0x10375fe0` runs **before** that check, so the quiet
kill still drops:

```
0x375ff8: 0x1036ebc0(game, unit, "rtmv", 7, 0x83, 0, 0, 0, 3)   ; one rtmv, quality request 7, item level 131
0x376022: count = rand() % 5 + 0x14                             ; 20-24
0x376032: tc = 0x1037f430(0x3b7)                                ; txt row 951 = ShadowOfMendeln, plus the compiled base
0x376055: 0x103164f0(7, frame + 1, 0x10372780, count, tc)       ; a unit timer one frame later
```

- `rtmv` ("Splinter", type `ubr`: `Normal` 1, no `Magic`/`Rare`, no unique row) is **always** dropped, once. The
  quality-7 request comes out normal, as the Wanderer's `wss` does.
- `0x10372780(game, unit, _, left, tc)`: rolls the TC through 1.13c's TC drop `0x1030f120(game, unit, unit, tc, 0,
  0x1037fa90(), 0, items[6], &dropped, 3)`, so at most 3 items per roll, at the monster's level (`0x103a1b00`, as
  Horazon's), with the monster itself as the "killer" (no player's magic find). While `left − dropped` is above 0 it
  re-arms itself 5 frames later with the rest. Net: **20-24 items** from `ShadowOfMendeln`, 3 every 5 frames
  (about 1.5 s of loot shower).
- `ShadowOfMendeln` (PD2 and port tables, identical): level 90, Picks 6, Unique 950, Set 960, Rare 990, Magic 1024,
  NoDrop 0; `Act 5 (H) Equip C` 787, `Act 5 (H) Good` 45, `Act 5 (H) Junk` 75, `Runes 17 equiv` 10, `wss` 15,
  `Jewelry C` 30, `scrb` 2.
- The monster's own monstats TC is empty in PD2 and never rolls (no-treasure flag).

That is the owner's "dies and drops loot": an unkillable boss that summons waves, novas, vanishes and showers a
TC-driven pile plus a guaranteed Splinter.

### What main and PR #1132 do

- Main: `KillQuietly` sets `UnitNoDrop`; no death listener or think code drops anything for port class 939. **No loot
  at all.**
- PR #1132 (a1f111d3) is data only: it gives `nihlathakMap` the TC `Mendeln Soul Drop` (picks −2: `ShadowOfMendeln`
  once, plus `Mendeln Soul Roll`, `rtmv` 1 in 9). It does not touch `nihlathak_ai.cpp`, so `KillQuietly`'s no-drop flag
  still blocks the TC: nothing drops. Even if the kill kept loot, the result would be one 6-pick roll (about 6 items)
  instead of 20-24, and `rtmv` 1 in 9 instead of always.

### Mendeln gap list

1. **Loot missing** (the main gap): `rtmv` always + 20-24 items from `ShadowOfMendeln`, 3 per roll, at the monster's
   level.
2. PR #1132's `nihlathakMap` TC: not PD2's and dead under the quiet kill. Revert that one monstats change (keep the
   row's TC empty as PD2's) and drop the `Mendeln Soul Drop` / `Mendeln Soul Roll` TCs, or keep them only if the owner
   wants `rtmv` at 1 in 9 instead of PD2's 1 in 1 (then a deviations row).
3. Wave champions and uniques spawn as normal monsters (known shortcut, deviation row "nihlathakMap's AI"): needs D2R's
   champion/unique maker, not in the toolkit's addresses. Late waves are weaker than PD2's.
4. Wave spot search: PD2 `0x10389c20` (size 2, mask 0x101, reach 4, no shift); `FindEventSpot` (`map_events.h`) is that
   search and now exists, so `SpawnWaveMonster` can use it.
5. Unit `+0xd0 = 11` mark on spawn: not identified, no known reader. Leave.

## 3. Other events that need work

- **Treasure Fallen.** `0x10378d10` (class `0x3b1`, from the death switch): with the monster's AI control
  (`unitdata+0x28`) arg `+0x14` = *n* below 9, it calls `0x103843d0` `9 − n` times, each a 1.13c monster TC drop
  (`0x1030f2b0`) skipped for a no-treasure unit. With the stock death drop that is up to 10 rolls of `Treasure Fallen
  H` (3 picks each). Port: one. *n* is decoded in section 5: PD2's own Fallen think drops the TC once per 10% of life
  lost and counts it in *n*.
- **Invaders.** Not built (MS10). PD2 spawns `InvaderPortal` up to 3 times; the rest is `AiThink4.cpp`, never read.
  Until then the event rolls silently (no spawn, no notify). Either leave it out of the roll (`event_roll.h` draw 10 ->
  0) with a deviations row, or build MS10.
- **Horazon's 10% roll** (`0x10381510`, open in map-system "Not settled"): seed & 7 picks one of 8 cases; each looks up
  an item code (`0x1037c880`, `0x1037c020`, `0x1037ff30`, not decoded) and drops it once with `0x1036ebc0` at quality
  request 7, item level 131. Decode those three to know the 8 items, then add it to `horazon.cpp`.
- **Dark Wanderer:** merge PR #1131 (shards).
- Deviations row "Map events on Cartographer's native maps" lists only Wanderer, Mendeln, Treasure Fallen and Gheed;
  `map_events.cpp` runs all events but the invaders. Text fix.

## 4. Implementation plan: Mendeln loot (for a Sonnet implementer)

Branch from main, issue "Shadow of Mendeln drops its loot". One file of code plus data and docs.

1. `plugin/core-events/nihlathak_ai.cpp`, constants (comment: PD2 `0x10375fe0`, docs/research/map-events-audit.md §2):
   `MendelnTc = "ShadowOfMendeln"`, `SoulCode = D2RL::Items::MakeItemCode("rtmv")`, `SoulItemLevel = 131`,
   `LootMin = 20`, `LootSpread = 5`, `LootPerRoll = 3`, `MaxRolls = 40`, `StatLevel = 12`;
   `static_assert(LootMin + LootSpread - 1 == 24)`.
2. New `DropMendelnLoot(Game* game, Unit* unit) noexcept -> int32_t`:
   - `DropUniqueAt(game, unit, SoulCode, -1, SoulItemLevel, 2)` (`uber_drops.h`; quality 2 normal, as #1131 does for
     `wss`).
   - TC lookup as `lucion_chests.cpp:52-63` (`DataTablesOf`, `TcIndexByName`, `TcRecord`); warn and stop if missing.
   - `ilvl = UnitStat(unit, StatLevel, 0)` as `horazon.cpp:60`.
   - `target = LootMin + RandomBelow(UnitSeed(unit), LootSpread)`; loop up to `MaxRolls` while `dropped < target`:
     `Unit* items[6] = {}; int32_t n = 0; TcDrop(game, unit, killer, tc, 0, ilvl, 0, items, &n, LootPerRoll);
     dropped += n;` (signature as `lucion_chests.cpp:66-68`). `killer`: PD2 passes the monster itself; pass `unit`,
     and if the engine refuses a non-player killer (no items), fall back to `NearestPlayer(game, unit, LevelOf(unit))`
     and note it in a deviations row.
   - `plog::Info("nihlathakMap: rtmv and %d of %d items from %s at ilvl %d", dropped, target, MendelnTc, ilvl)`; add a
     drops counter to `NihlathakAiCounts` (and its status line in `dclone_ai.cpp:436-446`; widen the array to 5).
3. Call it in the `i > last + 1` branch right before `KillQuietly(game, unit)` (`nihlathak_ai.cpp:113-117`). Keep
   `KillQuietly`: PD2 also kills with no treasure; the loot is the death step's, not the TC's.
   shortcut comment: all rolls drop on one frame, where PD2 spreads them 5 frames apart (`0x10372780`).
4. Optional, same PR: `SpawnWaveMonster` uses `FindEventSpot(room, xy, 2, 0x101, 4, false)` (`map_events.h`) in place
   of `FindFreeSpot`, and the deviations row "nihlathakMap's AI" drops the spot-search clause.
5. Data: coordinate with PR #1132 — remove its `nihlathakMap` TC change and the two `Mendeln Soul *` TCs (PD2's row has
   no TC), unless the owner wants `rtmv` at 1 in 9 (then skip the `DropUniqueAt` and keep the data).
6. Docs: deviations rows "Shadow of Mendeln's loot: one frame" (approximated) and, if used, the killer fallback;
   changelog fragment `#### Maps` / `- The Shadow of Mendeln drops its loot again when it fades.`
7. Game check (Hell, level 99, `core-events event mendeln` then a map; or wait the ~80 s fight): log lines
   `nihlathakMap: after the nova, it dies` then `rtmv and N of T items` with 20 <= T <= 24 and N >= T; a Splinter and a
   pile on the ground.

## 5. Treasure Fallen's extra drops (#1153)

### What *n* is

PD2 swaps the `Fallen` think. `0x10361150` (called from `0x10344580` at start-up) writes PD2 functions into 1.13c's AI
table (`DAT_105ddeb0`, 0x10 bytes a row, the think at +8): `+0x68` is row 6, `Fallen`, set to `0x1035a3c0`. That think
runs for every `Fallen`-row monster:

```
0x35a3c0: if no monster data, or monstats +0x84 (word) != 1, or no AI control (monster data +0x28):
              1.13c's Fallen think (D2Game +0x83130, PD2 thunk 0x10318910); return
          pct = D2Common #11027(unit)             ; life * 100 / max life (stats 6 and 7), read in D2Common +0x32330
          n   = control +0x14                     ; 1.13c's first AI param
          if n < 10.0 - ceil(pct / 10.0):         ; the 10.0 is 0x1044fbc8
              control +0x14 = n + 1
              0x103843d0({0, unit, 0}, game)      ; one TC drop, the same call the death step makes
          then its own movement (below)
```

- **The gate.** Monstats `+0x84` is `aip8` in Hell in 1.13c's record (aip1-8 as words, three difficulties each, from
  `+0x56`; the offset is inferred from that layout, not read from a table loader). Only `treasurefallenMap` (945) sets
  `aip8(H)` to 1 in PD2's monstats; every other `Fallen` monster keeps the stock think.
- **So *n* counts the drops it already made while hit.** Every think tick (`aidel` 12/8/5 frames) it may drop one TC
  roll: one for each 10% of life lost, at most one per tick. At 95% life ceil(9.5) = 10, threshold 0, nothing; at 85%
  one drop owed; at 5% nine. A burst that takes several tenths in one tick catches up one drop per later tick.
- **The death step** `0x10378d10` (class `0x3b1` in the death switch `0x10378d80`) drops the rest: `9 − n` more calls
  of `0x103843d0`. With the monster's own death drop that is **always 10 rolls** of its TC (`Treasure Fallen H`, 3
  picks each in Hell): up to 9 while it is hit, the rest on death, plus the stock one. A one-shot kill gives all 9 on
  death.
- `0x103843d0(drop, game)`: skipped for a no-treasure unit (flag `0x20000` at unit `+0xc4`), a class with no monstats
  record (`0x1037c1d0`) or a unit with no room (D2Common #10331, the path's room); else 1.13c's monster TC drop
  (D2Game `+0x75680`, thunk `0x1030f2b0`; D2R's `MonsterTcDrop` `0x447b80`) with the record `{0, monster, killer 0}`:
  no killer, so no player's magic find.
- **Its movement** (the rest of `0x35a3c0`, not asked for, noted because the port runs D2R's stock Fallen think): it
  tracks its own position in control `+0x18`/`+0x1c`; standing still 3+ ticks with the target closer than 10 it walks
  to a spot 2-4 cells past its own position away from the target (`0x103a1880`), so it **runs from the player** rather
  than fighting like a fallen. Not decoded further.

### Port plan (for a Sonnet implementer)

Issue #1153, branch from main. Files: a new `plugin/core-events/treasure_fallen.cpp` + `.h`, registered next to the
others in `core_events.cpp`'s start-up (as `horazon.cpp` and `key_drops.cpp` are), and `plugin/CMakeLists.txt`.

1. **Signature.** Add D2R's Fallen think to the toolkit (d2rAI-toolkit `shared/plugin-common/src/rvas_93847/ai.h`
   `AiFallen = 0x5ae650`, `sig/shared.inc` and `sig/cache_6ab3782c.inc` rows, `knowledge/d2r-93847-addresses.md`):
   monai row 6's think, read from the AI table (`0x2396ea0 + 6 * 0x20`). Unique pattern (20 bytes would do):
   `4c 89 44 24 18 55 56 41 55 41 56 48 81 ec 88 00 00 00 48 8b e9 49 8b f0 48 8b ca 4c 8b f2 e8 ?? ?? ?? ??`.
   D2R's think already uses the first AI param (`control +0x44`) as a "fled" flag, so **do not keep *n* there**.
2. **State.** `struct Drops { uint16_t game; uint32_t unit; int32_t n; }` in a fixed array of 16 (at most 3 fallen a
   map); find-or-add by game id and unit id, clear the slot on death.
3. **Think** (`RegisterAiThink(context, 6, d2r::sig::AiFallen.address, 962, &Think)`; 962 is the port's
   `treasurefallenMap`, `map_events.cpp:77`): `pct = life * 100 / UnitMaxLife` (as `dclone_ai.cpp:131`); if
   `n < 10 - (pct + 9) / 10` (integer ceil), `++n` and one `MonsterTcDrop(game, unit, 0, &drop)` with
   `drop = {nullptr, unit, killer}` as `uber_deaths.cpp:39-44`. Killer: the tick's target (`tick + AiTickTarget`) when
   it is a player, else `NearestPlayer(game, unit, LevelOf(unit))`; no player, no drop (PD2 passes none; D2R's drop
   reads the killer for the quality roll, so a null one is not safe). Skip a unit with `UnitNoDrop`. Then always
   `VanillaAiThink(6)(game, unit, tick)` (keep D2R's Fallen movement; the flee is a deviation). Log
   `treasure fallen: hit drop %d of 9 at %d%% life`.
4. **Death** (`registerMonsterDeathListener`, as `horazon.cpp:77-160`): class 962 → `9 - n` calls of the same drop, the
   killer from the event as `uber_deaths.cpp:27-37`; log `treasure fallen: death drops %d (n %d)`. The stock death
   drop stays.
5. **Docs:** deviations row "Treasure Fallen: D2R's Fallen think" (it fights instead of fleeing; drops need a player
   as killer); changelog `#### Maps` / `- Treasure Fallen drops its loot as it is hit and on death, ten times in all.`
6. **Game check** (Hell, level 99): force event 15 on a map, hit a fallen (no god one-shot: `cheat god` kills in one
   hit, so first check the one-shot case: `core-events kill 962` -> `death drops 9 (n 0)`), then without god let it
   be hit down: `hit drop k of 9` lines and `death drops 9-k`.

## 6. Mendeln wave champions and uniques (#1152)

### How PD2's waves pick champion or unique

Each wave rolls three counts from the stage's `aip` row (table in section 2): normals, champions, uniques. Champions
are 0 in stages 2-21, `rand() % 3` in 22-36 and `rand() % 2` in 37-46; uniques `rand() % 3` only in 37-46. Each count
spawns that many monsters, each from the same five-way pick as a normal (923, 924, 926, 927, or a mage 928-931), with:

- **champion `0x1037cbf0(game, room, x, y)`**: 1.13c `SpawnMonster` (D2Game `+0xeef10`, thunk `0x10313070`); then
  AI control `+8 |= 1`, `+0xc` = its own id, `+0x10` = 1, `+0x28` = game; monster flags `|= 1, 4, 8` (other, champion,
  unique, as 1.13c's own champions); one champion mod `0x103a2280` = D2Game `+0x23ae0` (a weighted pick of the
  monumod rows with `cpick`: 16 champion, 36 ghostly, 37 fanatic, 38 possessed, 39 berserk, weight 1 each in PD2) added
  by `0x103a22a0` = `+0x22920`; `0x103a22c0` = `+0x248a0(…, 3, 6)` (applies the mods, minion range 3-6); and
  `0x103a2260` = `+0x47880`, the champion pack: `rand() % 3 + 1` more of the class, each a champion with mod 16.
- **unique `0x1037caf0(game, room, x, y)`**: the same spawn and AI control; flags `|= 1, 8`; unique mods
  `0x10313100` = D2Game `+0x23ca0(game, unit, 0)` (0: no roll to champion; difficulty-scaled count of `upick` mods,
  at most 9); `+0x248a0(…, 3, 6)`: mods applied and 3-6 minions.

The thunk targets come from PD2's version tables, column 0 (1.13c): `0x104a9a14` -> `0x23ae0`, `0x104a8dcc` ->
`0x22920`, `0x104aaf5c` -> `0x248a0`, `0x104aa7e4` -> `0x47880`, `0x104a8854` -> `0x23ca0`, `0x1049be78` -> `0xeef10`.

### D2R's makers (build 93847, `D2Game\src\Monster\MonsterUnique.cpp`)

| RVA | 1.13c | Call (x64) | What it does |
|---|---|---|---|
| `0x499880` | `+0x24c70` | `Unit* (Game*, Room*, void* area, int32 classId, int32 mayChampion, uint16 x, uint16 y, int32 one, uint16 flags)` | the unique maker: spawns the class (`0x49e980`: with `area` null and x, y set, `SpawnMonster` `0x492cf0(game, room, x, y, class, 1, -1, flags \| 0x140)`, retried once; then `0x49edb0` flags 8 and 1 and counts it in the region's unique packs `+0x2c8`), unique mods `0x49e160(game, unit, mayChampion, diff + 1, diff + 1, …)`, minions `0x49ead0(…, 3, 6, flags)` (skipped for a champion), applies mods `0x49e780`, `0x49e860`. The populator `0x50acf0` calls it for a unique pack with `area` = the room's spawn rect, x = y = 0, `mayChampion` 1, `one` 1, flags `0x10`. With `mayChampion` 1, `0x49e160` turns it into a champion (flag 4, one `cpick` mod by `0x49ef90`) on a percent roll; with 0 it is always unique |
| `0x49ab40` | `+0x24bf0` | `void (Game*, Unit*, uint8 umod)` | makes a monster a champion with that mod: unless already a champion, `0x49eed0` (flag 8, region count), flags `\|= 5` (other, champion), the mod in the first free of 9 slots, mods applied (`0x49e780`, `0x49e860`). D2R's champion pack `0x50b1b0` calls it with 16 for each pack mate |
| `0x492b00` | — | `Unit* (Game*, Unit* near, int32 classId, int32 mode, int32 range, uint16 flags)` | spawns a monster next to a unit (the minion and pack-mate spawn when no area is given, `0x49ead0`) |
| `0x50b1b0` | `+0x47880` | `void (Game*, Unit* leader, void* area, int32 classId, uint16 flags)` | champion pack: for a champion leader, `rand % 3 + 1` more through `0x492bf0` (needs the populator's `area`) and `0x49ab40(…, 16)` |

Signatures (all unique in the dump's `.text`; patterns of 36, 40 and 27 bytes, each matching once):

```
MonsterSpawnUnique   0x499880  48 89 54 24 10 55 41 54 41 55 41 57 48 8d 6c 24 e9 48 81 ec a8 00 00 00 0f b7 45 7f 45 8b d9 44 0f b7 55 6f
MonsterMakeChampion  0x49ab40  48 89 5c 24 18 48 89 6c 24 20 56 48 83 ec 20 41 8b e8 48 8b da 48 8b f1 48 85 d2 74 0d 48 8b ca e8 ?? ?? ?? ?? 83 f8 01
SpawnMonsterNear     0x492b00  48 89 5c 24 08 48 89 74 24 10 57 48 83 ec 60 48 8b d9 44 89 44 24 38 33 f6 44 89
```

### Port plan (for a Sonnet implementer)

Issue #1152, branch from main. Toolkit first, then `plugin/core-events/nihlathak_ai.cpp`.

1. **Toolkit** (d2rAI-toolkit PR): `rvas_93847/units.h` `MonsterSpawnUnique = 0x499880`, `MonsterMakeChampion =
   0x49ab40`, `SpawnMonsterNear = 0x492b00` with the call lines above as comments; `sig/shared.inc` +
   `sig/cache_6ab3782c.inc` rows (area `units`, kind `Code`, at 0, next 0, the patterns above);
   `knowledge/d2r-93847-addresses.md` rows. Then bump the toolkit pin here.
2. **`SpawnWaveMonster(game, unit, ux, uy, seed, kind)`** with `enum class WaveKind { Normal, Champion, Unique }`; the
   ThinkNihlathak loop calls it `normal` times with Normal, `champions` with Champion, `uniques` with Unique (today one
   loop of the sum, `nihlathak_ai.cpp:130-132`). Spot and monster pick unchanged.
   - Normal: as today.
   - Unique: `Engine<Unit* (*)(Game*, void*, void*, int32_t, int32_t, uint16_t, uint16_t, int32_t, uint16_t)
     noexcept>(d2r::sig::MonsterSpawnUnique.address)(game, found, nullptr, monster, 0, x, y, 1, 0x10)`.
   - Champion: `SpawnMonster` as today (keep its result), then
     `MonsterMakeChampion(game, leader, ChampionMods[RandomBelow(seed, 5)])` with `ChampionMods[] = {16, 36, 37, 38,
     39}` (PD2's `cpick` rows, one weight each; the port's `monumod.txt` keeps the rows); then the pack:
     `RandomBelow(seed, 3) + 1` times `SpawnMonsterNear(game, leader, monster, 1, 4, 0x110)` and
     `MonsterMakeChampion(game, mate, 16)` (as `0x50b1b0`, which itself needs an `area` we do not have).
   - `plog::Info("nihlathakMap: wave %s %d at (%d, %d)%s", kind, monster, x, y, pack)` per spawn, and counters per
     kind in `NihlathakAiCounts`.
3. **Remove** the shortcut comment at `nihlathak_ai.cpp:41-42` and the deviations row "nihlathakMap's AI"'s champion
   clause; add a row for what is left (the champion leader gets no minions in D2R either way, `0x49ead0` skips
   champions; D2R counts each wave unique/champion in the level's unique-pack count, `0x49edb0`/`0x49eed0` `+0x2c8`,
   which 1.13c's PD2 path does not: later packs of the map may roll fewer uniques).
4. **Game check:** Hell, level 99, `core-events event mendeln` on a map, wait to stage 22+ (about 30 s after waking
   it): the log shows `wave champion` / `wave unique` lines; the name plates are the owner's (ready-for-human).

Not settled: the champion's 3-6 minions. PD2 passes (3, 6) to `+0x248a0` for both makers; whether 1.13c spawns minions
for a champion was not read (`+0x248a0`'s stack arguments did not decompile), and D2R's `0x49ead0` skips champions. The
plan follows D2R. The unique's mod count (`diff + 1` in D2R's populator call) against 1.13c's
`+0x23ca0` count was not compared line by line.

## Open questions for the owner

1. `rtmv` from Mendeln: PD2's always (death step), or PR #1132's 1 in 9?
2. Loot count: PD2's 20-24 items from `ShadowOfMendeln`, or fewer?
3. Invaders: hide the event from the roll until MS10, or keep it rolling silently?
