# PD2 hardcoded features ported to PD2-Resurrected

Read from origin/main at 97489f6 on 2026-11-11. Each date is the merge date of the PR that landed the feature (UTC). Later fix PRs follow a `;`.

**Status:** complete means no deviation row and no open gap. Partial means there is an "approximated" or "not yet" row in `docs/deviations.md`, an open issue, or a game check still open.

**Extraction:** how easily the feature lifts out into a standalone plugin.

- 🟢 **standalone-ready**: it has its own slots or hooks and no shared state. It needs only a plugin shell, plus its RVAs moved from `plugin/common/pd2_engine.h` into the toolkit's `rvas_93847.h`.
- 🟡 **needs shared hook layer**: it shares a function slot, a hook or a dispatcher with other features.
- 🔴 **tightly coupled**: it calls other features' code, shares per-game counters, or is a hub that other features go through.

## 1. core-rules (Shared, NativeHooks): skills, missiles, items, loot, cube, mercs

### 1a. Skill and missile functions (`skill_functions.cpp`, `missile_functions.cpp`)

| # | Feature | Ported (PR) | Status | Missing / deviating | Extraction |
|---|---|---|---|---|---|
| R1 | Split Throw + start buff | 11-03 #37 #61 #69; 11-07 #391 | partial | Spread uses Multiple Shot's line. calc4 enhanced damage not applied | 🟡 start slot 79 shared with Fire Claws |
| R2 | Leap / Leap Attack / Korlic | 11-04 #82; 11-08 #615 | partial | Client uses the server's speed. HitFlags not applied | 🟢 |
| R3 | Double Throw | 11-04 #81 | partial | PvP-map check skipped | 🟢 |
| R4 | Bash 50% knockback | 11-04 #71 | partial | Roll runs before the apply | 🟡 router (server do 190) |
| R5 | Find Potion / Find Item guard | 11-04 #71 | partial | Second guard absent (same result) | 🟢 |
| R6 | Exploding Arrow spread, Multiple Shot falloff, Power Strike client half | 11-04 #111 | partial | Explosion timing. Pierce count | 🟢 |
| R7 | Poison Dagger (+ Spider Boss) | 11-04 #123 | partial | No rescale of poison length | 🟡 missile hit 61 shared with potion clouds |
| R8 | Blood Warp | 11-04 #114 | partial | Packet 0x83 timer not sent | 🔴 TeleportCore |
| R9 | Desecrate (skill) | 11-04 #115 | partial | Corpse timer kept as a state list | 🟡 router |
| R10 | Revive aura (event 32) + Golem count | 11-04 #121 #126 | partial | Splash centres on the struck unit | 🔴 calls melee Splash |
| R11 | Melee splash (proc splash, Golem/Skeleton splash, item_splashonhit) + Frenzy/Tiger Strike start | 11-04 #126 #187 | partial | Smite's shield damage. Off-hand radius rules | 🔴 hub for Revive, Charge, Frenzy; reads an item_rules stat |
| R12 | Charge + Charge/Whirlwind rule | 11-04 #129; 11-08 #615 | partial | Distance is measured to the target point | 🟡 splash list |
| R13 | Holy Light + Fist of the Heavens client hit | 11-04 #135 | partial | Target/PvP alignment. No bolt limit | 🟢 |
| R14 | Vengeance / Joust | 11-04 #154 | partial | Uses D2R's melee hit. Link ring cap | 🟡 router + TeleportCore |
| R15 | Holy Sword / Smite start, Smite event 16 + crushing blow | 11-04 #166 #184; 11-07 #401 | partial | Event 16 never fires in D2R (#165) | 🔴 shares the crushing blow code with item rules |
| R16 | Blessed Hammer | 11-04 #163 | complete | none | 🟢 |
| R17 | Inferno / Arctic Blast + channel take-over | 11-04 #127 | partial | Frame-event re-timing not ported | 🔴 BusyModeGate hook shared with Blade Fury |
| R18 | Firestorm + wall makers (DiabWall etc.) | 11-04 #136 | partial | Maker seed comes from a game counter | 🟡 maker callback shared with Fire Claws |
| R19 | Gust, Hunger | 11-04 #139 | partial | Timer packet | 🟡 TeleportCore, TimedStatList |
| R20 | Fire Claws + Spirit Wolf / Fenris start | 11-04 #160 | partial | Claw hit uses D2R's roll | 🟡 shared slot and maker loop |
| R21 | Teleport / Blink / LucionBlink / Mon Bloodwarp + blockers | 11-04 #128; 11-07 #500 | partial | No "TeleportBlocked" line | 🔴 TeleportCore hub (6+ users) |
| R22 | Static Field, Combustion + boss novas | 11-04 #140 | partial | Monstats-flag refusals left to D2R | 🟡 ring helper |
| R23 | Fire/Ice Bolt, Blizzard, Chilling Armor bolt splash | 11-04 #161 | partial | Splash adds no physical damage | 🟢 |
| R24 | Assassin charge-up finishers (server + client) | 11-04 #124 #130 #137 #142 #174 | partial | Hit uses D2R's server do 34 | 🔴 start slot shared with Frenzy; prg dispatch shared with Dragon Flight |
| R25 | Dragon Flight + Psychic Hammer chain falloff | 11-04 #162 | partial | Target | 🟡 TeleportCore + finisher release |
| R26 | Blade Fury fragment | 11-04 #164 | partial | Not sent to other clients. Wall test | 🟡 gate hook |
| R27 | Get-hit / block end window | 11-04 #173 | partial | Game checks open (#175–177, #120) | 🟢 |
| R28 | Monster kits: PoisonEggSpawn, TorajanBossNest, Corpse/Vine Cycler, summon-on-hit | 11-05 #257; 11-08 #626 | partial | Nest egg count. Cycler target | 🟡 router |
| R29 | Royal Fire (Radament, Lucion) | 11-07 #463 | complete | none | 🟡 router |
| R30 | Death novas (Rathma, Nihlathak map, Lucion, Doll Meteor) | 11-07 #518; 11-08 #594 | complete | none | 🟡 router |
| R31 | Uber Diablo summon / run | 11-07 #565; 11-08 #492 | partial | Spawn spot and flags | 🔴 contract with core-maps' Dclone AI |
| R32 | Rathma summon + bone spear damage | 11-08 #572 | partial | Area check. Spawn spots | 🔴 reads core-maps' stat 500 |
| R33 | Uber immolation arrow hit | 11-08 #650 | complete | none | 🟢 |
| R34 | Lucion kit (Nova, Dash, Punish, Blink client) | 11-07 #560; 11-08 #596 | partial | Server-only missiles | 🔴 TeleportCore + core-maps' Lucion keys |
| R35 | Item casts of the novas | 11-08 #667 | partial | Missile flag 0x80000 missing (#673) | 🟡 ring helper |
| R36 | Throwing potions + Sharptooth's Armageddon | 11-08 #662 | partial | Armageddon not game-checked | 🟡 missile hit 61 |

### 1b. Item rules (`item_rules.cpp`)

| # | Feature | Ported (PR) | Status | Missing / deviating | Extraction |
|---|---|---|---|---|---|
| I1 | Ormus' Robes random sorceress skill | 11-04 #181 | complete | Firing depends on the Cast Triggers companion (#385) | 🟢 |
| I2 | Critical / deadly strike rework | 11-05 #317 | complete | none | 🟢 |
| I3 | Op bases: Purgatory ED per ethereal item, missing-life stat | 11-05 #317; 11-07 #455 #535 | partial | Refreshes only on a durability or life change | 🔴 StatCallbackInit hook shared with I8 |
| I4 | Nightmare's Feast life-steal cap + splash radius | 11-05 #317 | complete | none | 🟡 radius read by melee splash |
| I5 | Crushing blow formula and roll | 11-05 #317; 11-07 #401 #455 | partial | Boss divisor 30 not ported | 🔴 dispatcher wrapper shared with I6 and Smite |
| I6 | Open / deep wounds rework | 11-08 #646 | partial | One list per target. Rathma split (#645) | 🔴 same wrapper |
| I7 | Curses: max curses, curse effectiveness, gate, Dark Pact | 11-04 #266; 11-07 #430; 11-08 #625 | partial | Dark Pact at the slot cap | 🟢 one hook, as a group |
| I8 | Skill while equipped + Phasing | 11-05 #283 | partial | Skill list. Tooltip line | 🔴 shares StatCallbackInit with I3 |
| I9 | Replenish charges | 11-08 #651 | partial | Reads only the first charged skill | 🟢 |
| I10 | Unlimited Town Portal / Identify books | 11-08 #648 | partial | Almanac gate | 🟡 identify half is in core-maps |

### 1c. Loot, cube and merc rules (`loot_rules.cpp`, `cube_rules.cpp`, `cube_stacks.cpp`, `merc_rules.cpp`)

| # | Feature | Ported (PR) | Status | Missing / deviating | Extraction |
|---|---|---|---|---|---|
| L1 | Ethereal bonus 5/4 | 11-07 #420 | partial | Not game-checked. Early return dropped | 🟢 |
| L2 | Affix `rare` 2/3 rules | 11-08 #629 | complete | none | 🟢 |
| L3 | Weapon durability loss at half rate | 11-08 #660 | complete | none | 🟢 |
| L4 | Staffmods class skill roll | 11-08 #670 | complete | Warlock keeps D2R's roll | 🟢 |
| L5 | Chaos seal-sweep kills drop nothing | 11-08 #684 | complete | none | 🟢 |
| C1 | Corruption in one press | 11-05 #327 | complete | none | 🟡 calls cube_stacks |
| C2 | Demonic Cube reroll (op 31) | 11-05 #327; 11-07 #423; 11-08 #666 | partial | Some stats not kept. Hosted game refuses other players | 🔴 same CubeExecutor hook as C1 |
| C3 | Stack-aware cube matching | 11-07 #432 | partial | An empty tome counts as one | 🟡 core-maps op 30 calls the hooked matcher |
| M1 | Tyrael hires the Ascendant | 11-04 #273 | complete | none | 🟢 |

## 2. core-maps (Shared): map system and uber bosses

| # | Feature | Files | Ported (PR) | Status | Missing / deviating | Extraction |
|---|---|---|---|---|---|---|
| P1 | Cube op 30 map recipes | cube_ops | 11-05 #263 | complete | numinputs counts the stack quantity | 🟡 cube search shared with Desecration and core-rules |
| P2 | Open a map (item spell 16, gates, one per game, red portal) | map_open | 11-05 #280 | complete | Monastery and PvP maps have no pSpell | 🔴 item-spell, portal and room-release hub for every fight |
| P3 | Monster/player map stats (`map_mon_*`, `map_play_*`) | map_stats | 11-05 #284 | partial | No HUD panel or packet 0x55. Aligned monsters get no list | 🔴 hosts 5 hooks used by 6 files |
| P4 | Area map stats (density, area level, rarity, extra boss, fortify, corrupt/socket/eth drops, death drops, multi-level) | map_stats | 11-05 #287 #288; 11-07 #380 #414 #496 #529 | complete | Death-drop ilvl is the monster's level | 🔴 same hub |
| P5 | Map events (Mendeln, Dark Wanderer, Treasure Fallen, Gheed) + chat line | map_events | 11-07 #427 #516 #567 #568 #570 | complete | Gheed player list cut. Flags 0x3000000 not set | 🔴 RoomPopulate hub; event value read by 5 files |
| P6 | Altar of the Catalyst / Spire / Void-Tomb events | map_events, event_portals | 11-08 #608 | partial | Sounds play on players. Red portals, not gold | 🔴 |
| P7 | Altar guardian AI | guardian_ai | 11-08 #618 | complete | Clone summon goes through core-rules | 🟡 ai_hooks |
| P8 | Horazon event (red portal, drops, 10% unique map) | event_portals | 11-07 #435; 11-08 #571 | partial | Kills by missiles or pets roll for the first player | 🔴 |
| P9 | Uber key drops (odds-only TCs, Talisman/Shard in Terror Zones, unique maps) | key_drops | 11-06 #360; 11-07 #524 | complete* | Terror Zones stand in for corrupted zones | 🔴 death-event hub for every fight |
| P10 | Uber opening at the Blood Fountain (group warp, strip, re-entry, no waypoints) | map_open, ai_hooks | 11-06 #344; 11-07 #357 #457 #459 #481 #565 | partial | RemovePet kills all pets. Re-entry kept in counters | 🔴 |
| P11 | Uber tier scaling | uber_scaling | 11-07 #381 | partial | Life after the raise not identified | 🟡 monster-spawn event |
| P12 | Pandemonium / Uber Tristram (ubtm, primes, minions, Torch, organs) | uber_tristram, prime_minions, map_bosses, uber_drops | 11-07 #416 #434 #497 #501 | partial | Drop ilvl 120 not 130. Minion rolls. Organ recipe | 🔴 spread over 5 files |
| P13 | Diablo Clone (AI phases, souls, Annihilus, 1/200 uniques) | dclone_ai, uber_drops | 11-07 #425 #557 #563 | partial | **No gold portal to Harrogath (no deviation row)**. #592 immune bosses one-shot | 🟡 AI / 🔴 drops |
| P14 | Rathma / Mendeln (stages, Summoner AI, shared life, drops, re-entry) | rathma, rathma_ai, rathma_life, uber_drops | 11-07 #417 #559 #569; 11-08 #591 #595 #603 #605 #656 | partial | Walks are idles. Boss slots. #645 #592 | 🔴 called from 3 hubs; core-rules reads its keys |
| P15 | Lucion (death portals, Demonic Cube, Overseer AI, Temple chests) | lucion, lucion_ai, lucion_chests, level_fx | 11-07 #433 #443 #482 #491; 11-08 #597 #606 | partial | Harrogath portal not linked. Target distance | 🔴 |
| P16 | Desecration in the Temple cube | lucion, map_stats, cube_ops | 11-07 #521 | complete* | Cube instead of PD2's panel. Single player only | 🔴 spans 3 files |
| P17 | Map boss swap (placeholders → each map's boss) | map_bosses | 11-08 #619 | partial | Dungeon boss swap and barriers (#533) | 🟡 preset-spawn event |
| P18 | Map boss AIs: Leoric, Siege Beast, Sharptooth | map_boss_ai | 11-08 #631 | complete | Adds rely on core-rules' summon-on-hit | 🟡 ai_hooks only |
| P19 | Unique maps (Zhar, Warlord, Iskatu, Rakanoth, Imperial, Ureh, Outer Void) | unique_maps | 11-07 #528 #561 #562 | partial | Kill counters cut. Kamyr portal link (#527) | 🔴 uses 4 hubs |
| P20 | Zhar's Sanctum seals | zhar | 11-08 #613 | partial | Wing RNG is not game seed mod 3. Sounds missing (#405) | 🟡 preset-spawn event |
| P21 | Engine pool growth for big presets (infra) | entity_pool | 11-05 #277 | partial | Sizes are guesses (#771) | 🟢 |
| P22 | Shared AI registry, idle window, boss immunity (infra) | ai_hooks | 11-07 #437 #491 #497 #565 #597 | infra | SetImmune copies have drifted (#600) | 🟢 as a library |
| P23 | Level FX: act darkness, level sound (infra) | level_fx | 11-08 #606 | infra | none | 🟢 |
| — | Invaders (MS10/MS11) | map_events | **not ported** | missing | Rolled and logged only (#224, #225) | n/a |

\* complete except for an approximation listed in `docs/deviations.md`.

## 3. core-ui (client-only, not Shared)

| # | Feature | Files | Ported (PR) | Status | Missing / deviating | Extraction |
|---|---|---|---|---|---|---|
| U1 | Automap for PD2 level types 36+ | automap | 11-05 #276 (+ data #279 #285) | complete | Custom table path has "core-ui" in it. Leaks the old snapshot on a table reload | 🟢 rename the table, move 4 RVAs |
| U2 | HD pictures for 4-character item codes | item_pictures | 11-05 #313 | complete | none | 🟢 MutationService only |
| U3 | `cheat spawn` with 4-character codes (dev aid) | item_pictures | 11-05 #326 | complete | Same transaction as U2 | 🟢 |
| U4 | Stack split (Ctrl+click puts 1 down) + Ctrl+click adds 1 | stack_split | 11-06 #361; 11-07 #392 | partial | The original item goes down, not a new one. No TCP/IP client support. Cap-merge differs | 🟢 SDK only, the easiest to lift out |
| U5 | Show Item Quantity on stacks | item_quantity | 11-07 #402 | partial | Number bottom right with no box. Tomes show nothing. No hotkey | 🟡 replaces D2R's grid draw; waits on SDK `getWidgetScreenRect` |
| U6 | Legacy palette on load-screen-less level entry | level_palette | 11-07 #452 | complete | Legacy graphics only | 🟢 |

## 4. Covered by bundled companions (third-party, not our code)

| PD2 feature | Companion | Added (PR) | Gap |
|---|---|---|---|
| BH loot filter | Pyre Loot Filter | 11-03 #39 | No BH syntax or filter levels |
| Stash search | Pyre Stash Search | 11-03 #39 | none recorded |
| Tooltip roll ranges | Item Roll Ranges | 11-03 #39 | Not compared with PD2 |
| Buff timers, mini skill icons | Buff HUD | 11-03 #39 | PD2's icon art not ported |
| Left-click auras | Left Click Auras | 11-04 #117 | PD2's start rules not run |
| Skill on cast / on block | Cast Triggers | 11-07 #250 | On block: no proc seen in game. Cast aiming differs |
| Ethereal set items 5% | Ethereal Item Rules | 11-07 #250 | none |
| Ethereal full durability | Item Durability | 11-07 #250 | Reported as not loading on D2RLoader 1.3.1-beta |
| Rare affix count by item level | Progressive Affixes | 11-07 #250; 11-08 #661 | ilvl 45–64 band is 33.33/33.33/33.34% |
| Mass identify with a tome | Mass Identify | 11-07 #250 | Reported as not loading |
| Merc gear drops on hire | Mercenary Drop On Hire | 11-07 #250 | Game check open |
| DPS counter | DPS Counter | 11-08 #665 | Host only; no damage over time |
| Charm inventory, one unique charm | Charm Inventory | 11-05 #303 | Charm groups not compared |

## 5. Not ported yet / cut

- **Not built:** invaders (#224, #225). W37/W38 (#245, #246): `.set-home`, `skilldelay` state remove, Act 3 quest 5 patch, PlrModes death handler, perfect shrine. Loot allocation. Object operate functions. Dye tint (#190). core-stash (waits on SDK support for extra pages). Force Move (#751, #689, waits on a third-party plugin).
- **Cut:** `.noexp`, character stat commands, PvP and battle-royale rules, the level life-regen cap, corrupted zones (Terror Zones stay), map stash tab, stash layouts.
- **Unreachable in D2R:** skill on crit, skill on pierce, corpse finish event, `item_shiny_appearance`.

## 6. What blocks extraction (shared layers to build first)

1. **core-maps event bus.** Three hub files name every fight directly: `key_drops` (monster death), `map_stats` (monster spawn, item spawn, preset spawn) and `map_open` (item spell, portal operate, room release), plus `map_events` (RoomPopulate). Each fight needs to subscribe to an event instead. This is #600's "one module per fight" plan.
2. **Counter key registry.** About 35 four-character `GameCounter` keys, with `TierKey` defined in 9 files. core-rules reads core-maps' Rathma and Lucion keys across the DLL seam.
3. **core-rules skill slot table and router.** Server do 190 routes about 20 skills. Move it to a registration API so each skill pack installs its own entries.
4. **Shared skill helpers as a library:** TeleportCore, TimedStatList, the ring helpers, melee Splash, BusyModeGate.
5. **Item-rules hook sharing:** the event dispatcher wrapper (crushing blow, open wounds, Smite) and StatCallbackInit (op bases, skill-on-equip).
6. **Data travels with the code.** Each slot only works because the merge points table rows at it (`tools/pd2_ids.py` FUNCS, `patches/020`, `030`, `060`). A standalone plugin must ship those rows.
7. **RVAs.** Move each extracted feature's entries from `plugin/common/pd2_engine.h` into the toolkit's `rvas_93847.h`, as that header's own rule says.

**Ready to extract now (🟢):** stack split (U4), legacy palette (U6), 4-character item pictures (U2/U3), automap 36+ (U1), engine pool growth (P21), loot rules L1–L5, Tyrael's Ascendant (M1), crit/deadly strike (I2), curses (I7), replenish charges (I9), Ormus' Robes (I1), get-hit window (R27), Blessed Hammer (R16), and the own-slot skills R2, R3, R5, R6, R13, R23, R33.
