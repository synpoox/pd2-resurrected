# The Dark Wanderer map event

Research for [#1064](https://github.com/synpoox/pd2-resurrected/issues/1064). It reads PD2's `ProjectDiablo.dll` and the
1.13c `D2Game.dll` from the PD2 install in Ghidra (project `PD2.gpr`) and compares them with main at 80cd1575. Static
reading only: the game was not run. Addresses are PD2's: `ProjectDiablo.dll` addresses from its `0x10000000` base,
`D2Game.dll` RVAs from its `0x6fc20000` base. Monster ids are PD2's unless marked "port".

Builds on [pd2-map-system-dll.md](pd2-map-system-dll.md) (section 4, the event roll and spawns) and the #1006 research
comment (the boss).

## Answer in brief

| Owner's goal | PD2 | Main |
|---|---|---|
| 3 Wanderers in a map | Event 14 spawns `darkwandererMap` (921, port 938) up to 3 times as rooms activate | same (`map_events.cpp`) |
| He waits, then disappears | 1.13c's stock think, with its Act 3 quest gate removed by a 2-byte patch. He idles until a player is within 20, walks a few times, then leaves (death mode, no drop) | same, as the port think of #1087 |
| He spawns a random superunique | 85 frames after he leaves: 1 in 3 an act boss, else a superunique (or Izual or Blood Raven), buffed, with state `mapevent` | same (`wanderer.cpp`) |
| It drops 3-6 shards | **4-10** Worldstone Shards (`wss`), item level 131, when a monster with state `mapevent` dies while the map's event is 14 | **missing** |
| He is uninteractable | Data only: `isAtt` 0 and `killable` 0 (no attack, no damage); `isSel` 1 and `alSel` 1, so his name **does** show on hover | data matches PD2 |

So main lacks one thing PD2 has: the shard drop. Two of the owner's points differ from PD2 (the shard count and the
hover); see [Open questions](#open-questions).

## 1. The trigger and the 3 spawns

From [pd2-map-system-dll.md](pd2-map-system-dll.md) section 4, checked again in `0x1036fef0` (room activation):

- The map roll picks event 14 (`game+0x1df4 == 0xe`) for 1 in 9 rolled tiered maps.
- The n-th Wanderer (n = 1..3, count at `game+0x2628`, `puVar2[0x98a]`) spawns when `n x 25 < progress` and
  `rand() % 100 < 25`. Progress is the level's activated rooms x 100 / a per-level room constant (`0x1038b1d0`).
- The spot is the activating room's centre, through the spot search `0x10389c20(xy, room, mask 0x805, reach 8, shift 1)`;
  the spawn is `0x1038a170` -> `0x1038a060(x, y, class 0x399, 1)`. `0x1038a060` spawns through `0x10371c30` and sets
  bits `0x3000000` of the unit flags (+0xc4); nothing there makes him untargetable.
- Each spawn sends `MapEventNotifyDarkWanderer`.

Main's `plugin/core-events/map_events.cpp` does the same (the `DarkWanderer` branch, `FindEventSpot(..., 0x805, 8, true)`).

## 2. The wait and the vanish

PD2 does not reference class 921 in any think. The monster's `AI` is the stock `DarkWanderer` (monai row 91). Its 1.13c
think is `D2Game 0xa6e60` (the only pointer to it is the AI table entry at `D2Game 0x10ed40`):

1. `0xe2780`: the a3q7 gate. It walks the game's quest list (`game+0x10f4`) for quest 0x1c and returns 0 unless that
   quest's data byte +0 is set (only the Act 3 docks set it). On success it writes the spot (quest data +4/+8, y-20 on first use, nudged by
   `Ordinal_10686` collision checks) into the think's local.
2. `0xa6e8e: TEST EAX,EAX` / `0xa6e90: JZ` -> idle 10 frames (`0xf22c0(10)`).

**PD2's patch.** `ProjectDiablo.dll`'s patch table (installed DLL, file offset `0x49bfc8`) holds the entry
`{dll 3 (D2Game), offset 0xa6e90, fill 0x90, type 0, length 2}` (its neighbours are type-1 hooks: `{3, offset, hook address, 1, 0}`): it NOPs the `JZ`. So the gate's result is ignored and the
think always runs. In a map the gate returns 0 without writing the spot, so the walk target is whatever is on the
stack: undefined, in practice not reachable.

The rest of the think (unpatched):

- No target, or the target 20 or more away (`tick+0x14 >= 0x14`): idle 40 frames (10 with no target).
- State 0 -> 1 (`monster AI params +0x14`), trips (+0x18) = 0.
- State 1: walk to the spot (`0xf2540`), state 2.
- State 2: if the distance to the spot (`0xb07b0`) is below 2, or trips > 2: unit flag `0x20000` (no treasure), death
  mode (`0xf21e0(game, unit, 0, 0)`), then a3q7's callback `0xe6760`. Else trips + 1 and walk again.

With an unreachable spot he walks 3 more times and leaves: once a player comes within 20, he is gone after about four
thinks (`aidel` 15 frames each, plus walk time), the owner's "waits a few seconds".

`0xe6760` marks quest 0x1c's data (+0xd = 1, +0x10 = his unit id) and runs the quest's update the first time in a game
(+0xc and +0xd clear). In a map that is the first Wanderer to leave: a3q7's effect `0xe57d0` (8 missiles around him, a
player quest update). Main skips it on purpose (#1087, so the real a3q7 never arms); see Open questions.

**Main:** `wanderer.cpp`'s `ThinkWanderer` is this think minus the gate, for port class 938 only, with the spot at
y-20 of where he stands. Same timeline. The deviations row "leave spot: vanilla's y-20" can now say PD2's spot is
undefined (the gate is skipped, the local is never written).

## 3. The superunique

The leave puts him in death mode, so PD2's dead-mode wrapper `0x1037a330` runs its steps. `0x10375fe0`, class 0x399:
sound 705 to every client (`0x1037f240`), then a unit timer (`0x103164f0`, type 7) at frame + 0x55 (85) with callback
`0x10370d90`. That callback (decompiled again here, same as #1006's comment):

- One free-spot search at the corpse (`0x10389ad0`, size 1); none, no boss.
- Up to 50 tries, first spawn that returns a unit wins:
  - `rand() % 3 == 0`: pool A, the act bosses 156 andariel, 211 duriel, 242 mephisto, 243 diablo, 544 baalcrab.
  - else pool B: 17 superuniques (hcIdx 6, 11, 12, 15, 18, 20, 21, 23, 24, 36, 37, 38, 39, 42, 49, 52, 65), passed as
    monstats count + hcIdx (`0x1030cb00(2, id)`), plus monsters 256 izual and 267 bloodraven as they are.
  - A superunique already spawned this game is refused, so the loop rolls again.
- Spawn `0x103a1420(id, room, game, x, y, 1)`: the stock spawn, so the level is the area's (no level override).
- Buff: max life (stat 7) x2 pool A (x1 for 544), x4 pool B; a stat list (flags 8) with state 0xd5 (213, `mapevent`,
  port 259), `damagepercent` +50 (A) or +100 (B) and the four masteries (329-332) at twice that.

Main's `SpawnBoss` and `Buff` match this.

## 4. The shards

Not the boss's TC and not the superunique row: a death step. `0x1037a330` calls `0x10379950` near the end, only when
the dying unit lacks unit flag `0x20000` (no treasure):

```
0x379957: CMP dword ptr [EAX + 0x1df4],0xe      ; the map's event is the Dark Wanderer
0x379960: PUSH 0xd5 / CALL 0x1030c320          ; the dying monster has state 213 (mapevent)
0x379974: code = 'wss '                         ; Worldstone Shard
0x379984: CALL 0x1036f320                       ; the monster's seed
0x37998b: DIV 7 ; EDI = rem + 4                 ; 4..10 drops
0x3799a5: 0x1036ebc0(game, monster, code, 7, 0x83, 0, 0, 0, 3) x EDI
```

- Count: `seed % 7 + 4`, so **4 to 10**, uniform.
- Item: `wss` (misc "Worldstone Shard", type `corr`), item level 0x83 = 131, quality request 7. `corr` has `Magic` 0,
  `Rare` 0, `Normal` 1, so the shard comes out normal.
- Who: any monster with state 213 while the event is 14. Only the event's boss gets that state under event 14 (its
  minions do not; Outer Void's state-213 bosses have event 0x21). The Wanderer himself has no state 213 and has the
  no-treasure flag.

Main has no such step: no `wss` drop anywhere in `plugin/core-events` for event 14.

## 5. Uninteractable

PD2 does it in data only; no code sets unit flags on him for this. PD2's rows (main's are the same):

| Table | Column | Value | Effect |
|---|---|---|---|
| monstats | `killable` | 0 | takes no damage |
| monstats | `npc`, `interact` | 0 | no talk |
| monstats | `neverCount` | 1 | not counted |
| monstats2 | `isAtt` | 0 | cannot be attacked |
| monstats2 | `isSel`, `alSel` | 1, 1 | **selectable: hover shows "Dark Wanderer"** |
| monstats2 | `noSel` | 0 | |
| monstats2 | `noMap`, `automapCel` | 1, 1499 | his own automap marker |

The owner's "no hover" is not PD2's. If wanted, it is a data deviation: `isSel` 0 and `alSel` 0 on port row
`darkwandererMap` (monstats2), plus a deviations.md row.

## 6. Cartographer

pd2r has no `mapevents.txt` under `files/data/global/excel/d2rloader/cartographer/`: PD2's events run in
`core-events/map_events.cpp` (its own roll and room activation, also on Cartographer's native maps,
deviations.md "Map events on Cartographer's native maps"). Cartographer's own `mapevents.txt` (kinds Boss, Ambush,
Horde, Treasure, Shrine) cannot express a delayed boss or a state-gated drop and is not needed. No d2r-tools change.

## Gap list

1. **Shard drop missing** (section 4): 4-10 `wss` at item level 131 when a `mapevent` monster dies under event 14.
2. **Deviations text stale**: the "leave spot" row says PD2's spot is not found; it is undefined in PD2 (gate NOPed).
3. **a3q7 leave effect skipped** (section 2): PD2's first leaving Wanderer per game runs a3q7's callback. Main skips it
   on purpose; it needs a deviations row if kept.
4. **Hover** (section 5): only if the owner wants no hover, which differs from PD2.

Already matching PD2, no work: the 3 spawns and their odds, the leave, the 85-frame boss with its pools, buff and
state.

## Implementation spec

### pd2r plugin (`plugin/core-events/lane-c/wanderer.cpp`)

Gap 1. No new engine address is needed: every call below already exists as a signature entry or SDK service in main.

1. In `InstallWanderer`, register a monster-death listener the way `InstallEventPortals` does in `horazon.cpp`
   (`D2RL::LifecycleService`, `registerMonsterDeathListener`, `MonsterDeathListenerSize`). If the service or the
   listener is refused, keep the think and the boss, and add "no shard drop" to the report.
2. The callback `OnShardDeath(context, event, userData)`:
   - Return unless `event->monster.type == UnitType::Monster` and `event->gameId <= 0xffff`.
   - `Game* game = GameOf(event->gameId)` (the helper already in the file); return if null.
   - Return unless `MapEvent(game) == 14` (`map_events.h`).
   - `Unit* monster = FindUnit(game, 1, event->monster.id)` (`d2r::sig::FindUnit`, as `SpawnBoss` calls it); return if null.
   - Return unless `UnitHasState(monster, BossState) != 0` (`BossState` = 259, already in the file; use
     `UnitHasState` as `uber_entry.cpp` does).
   - Return if `At<uint32_t>(monster, UnitMainFlags) & UnitNoTreasure` (as `lucion_ai.cpp` reads the flags).
   - `count = d2r::RandomBelow(UnitSeed(monster), 7) + 4`.
   - `DropUniqueAt(game, monster, D2RL::Items::MakeItemCode("wss"), -1, 131, QualityNormal)` `count` times
     (`uber_drops.h`; `QualityNormal` is 2). PD2 asks for quality 7, which a `corr` item turns into normal, so asking
     for normal gives the same item and avoids a failed-quality warning.
   - `plog::Info("wanderer: %d shards from boss %d (unit %u), event 14", count, event->monster.classId, event->monster.id)`
     and add a `g_shards` counter to `WandererCounters`.
3. Constants at the top: `ShardItemLevel = 131`, `ShardMin = 4`, `ShardSpread = 7` (comment: PD2 `0x10379950`,
   `seed % 7 + 4`). If the owner picks 3-6 instead, `ShardMin = 3`, `ShardSpread = 4`, plus a deviations row.
4. Add a `static_assert(ShardMin + ShardSpread - 1 == 10)` (or `== 6`) next to the constants.
5. Update the file's header comment: the boss's shards.

Gap 2 and 3, `docs/deviations.md`:

- Row "Dark Wanderer's leave spot": PD2 NOPs the a3q7 gate's jump (`D2Game 0xa6e90`, 2 bytes), so its walk target is
  an unset local; the port's y-20 keeps him walking a few steps first. Keep "approximated".
- New row "Dark Wanderer's leave skips a3q7's callback": PD2's first leave per game runs a3q7's quest callback and
  effect; the port skips it so the Act 3 quest is never touched. "approximated", #1064.

Changelog fragment `changelog.d/1064-dark-wanderer-shards.md`: `#### Maps` / `- The Dark Wanderer's boss drops 4-10 Worldstone Shards.`

### pd2r data

Only for gap 4, if the owner wants no hover: `files/data/global/excel/monstats2.txt`, row `darkwandererMap`: `isSel`
0, `alSel` 0. Not a JSON patch: edit the row (or the merge source that generates it, if the row is generated; check
`merge/` for a `darkwandererMap` override first). Plus a deviations row.

### d2r-tools Cartographer

None.

### Game check

Slot per `docs/agents/game-check.md`, Hell, level-99 character, map with the event forced (the `wanderer` test aid,
`ForceNextMapEvent`), or `cheat goto 110` + `cheat mspawn 938` for one Wanderer. The `MapEvent(game) == 14` gate means
an `mspawn` outside a Wanderer map leaves and spawns a boss but drops no shards; use a forced Wanderer map for the drop.
Log lines expected: 3 `wanderer: left`, 3 `wanderer: boss`, and after each boss kill `wanderer: N shards` with
4 <= N <= 10. Hover check (name plate) is the owner's.

## Open questions

1. **Shard count.** PD2's code drops 4-10 (`seed % 7 + 4`). The owner's goal says 3-6. Keep PD2's 4-10, or use 3-6 as
   a deviation?
2. **Hover.** PD2 lets players hover him (name shows) but not attack or damage him. Keep PD2's, or remove hover
   (`isSel`/`alSel` 0)?
3. **Pool A.** 1 in 3 bosses are act bosses (Andariel, Duriel, Mephisto, Diablo, Baal's crab), not superuniques. PD2
   does this and main matches; keep?
4. **a3q7's effect.** PD2's first leaving Wanderer per game runs a3q7's callback (missiles around him, a quest update
   for players). Main skips it. Keep skipping?
