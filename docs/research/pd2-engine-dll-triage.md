# Triage of PD2's engine DLLs

Answers research ticket #3 (map #1). Sorting scheme from `knowledge/porting-classic-mods.md` section 1 of the d2rAI-toolkit:
(a) data only, (b) D2RLoader service, (c) native hook, (d) UI rebuild, (e) cut.

**Method.** Static, read-only inspection of the local PD2 install: PE headers, export and import tables, printable strings (assert
source file names, config keys, UI resource names, string-table keys), and the shipped config files (`BH.json`, `ProjectDiablo.json`,
`ProjectDiablo.cfg`, `PlugY.ini`, `D2HD.ini`, `d2gl.yaml`, `ddraw.ini`). No binary was executed, decompiled or copied. Sizes and
risks below are estimates from string evidence, not from reading the code. A string proves a feature exists, not how it works.

## Answer in brief

- PD2's own engine code is two things: `ProjectDiablo.dll` (5.2 MB, the client/server/UI core, with guilds) and `BH.dll` (1.4 MB, the
  loot filter and screen-info layer). `PD2_EXT.dll` is only a loader shim. Everything else is third-party, and most of it D2R already
  does natively.
- Cut outright (D2R covers or makes it irrelevant): `D2HD.dll`, d2gl (`glide3x.dll`), cnc-ddraw (`ddraw.dll`), SGD2FreeRes, `PD2_EXT.dll`,
  the Battle.net/launcher plumbing, and PlugY's windowing, language and save-path options.
- The real porting work is the loot filter (BH), the stash system (categories, search, shared and hardcore-shared stashes, currency tab),
  custom item/skill rules in `ProjectDiablo.dll`, controller support, and the PD2 aura/event/notification features. Most of it is
  UI rebuild plus plugin services. Native hooks are needed for a handful of server rules.
- Guilds (a large subsystem with its own save blob and UI) belong with the online/server features the map marks out of scope. Cut for now.
- Whether the `ProjectDiablo.dll` rule hooks need hooks or are data-only cannot be settled from the DLL strings. That needs the
  data-table diff (new function numbers in `pd2data.mpq`), which is a separate ticket.

## 1. Inventory

| DLL | Role | Size | Arch | Exports | Source file names found |
|---|---|---|---|---|---|
| `ProjectDiablo.dll` | PD2 core mod: patches the 1.1x client and game DLLs, UI, sound, stash, guilds, controller | 5.25 MB | x86, C++ (MSVC 14) | 4 (`D2Client_GetItemDiscoveryState`, `D2Client_GetItemStorageType`, `D2Client_PlaySoundWithCustomVolumeOrPriority`, a boost stub) | 43 (see section 2) |
| `PD2_EXT.dll` | Proxy of Windows `version.dll` (all 12 exports forward); loads `ProjectDiablo.dll` and the game data files, shows an error if either fails | 87 KB | x86 | 12 forwards | none |
| `BH.dll` | Loot filter, item display, screen info, party/loot automation, stash export, stats display | 1.43 MB | x86 | 10 (`BHInteract`, `BHGetConfig`, `BHSetConfig`, `BHOnPropertyBuild`, `BHOnDamagePropertyBuild`, `BHOnProperties`, `BHSetMonstersRemaining`, `BHShowPlayersGear`, `BHIsReady`, `Initialize`) | `StatsDisplay.cpp` |
| `PlugY.dll` | PlugY 14.02-era "Survival Kit": multi-page stash, shared stash, stats unassign | 215 KB (2021) | x86 | 2 (`_Init@4`, `_Release@0`) | 17 (section 2) |
| `D2HD.dll` | Widescreen HD resolution patch (with `D2MultiRes.mpq`) | 214 KB (2018) | x86 | none | `D2HDDraw.cpp` |
| `glide3x.dll` | d2gl, the Glide-to-OpenGL wrapper (shaders, FXAA, bloom, HD cursor and text, mini map) | 2.9 MB | x86 | d2gl config query/set | n/a |
| `ddraw.dll` | cnc-ddraw, a DirectDraw renderer wrapper | 280 KB | x86 | n/a | n/a |
| `SGD2FreeRes.dll`, `SGD2FreeDisplayFix.dll` | Free-resolution patch | 307 KB, 221 KB | x86 | n/a | n/a |

BH.dll and ProjectDiablo.dll carry the same PD2 build stamp. PlugY and D2HD are unmodified third-party builds that predate the current
PD2 release. PD2 ships at least two renderer stacks (D2HD or SGD2FreeRes, d2gl, cnc-ddraw), so display is a launcher choice, not game logic.

## 2. Features by DLL

### ProjectDiablo.dll

Source file names, grouped. The first group reuses names from the classic D2 source tree, so these files are patched game code, not new code:

- Client: `Gfx.cpp`, `Msg.cpp`, `SCmd.cpp`, `Skills.cpp`, `SoundHdr.cpp`, `inv.cpp`, `panel.cpp`, `showitems.cpp`, `ui.cpp`, `ARCHIVE.CPP`.
- Common: `ItemTbls.cpp`, `Items.cpp`, `StatsEx.cpp`, `CmnItems.h`.
- Game (server): `Game.cpp`, `Clients.cpp`, `ItemMode.cpp`, `PlrModes.cpp`, `PlrMsg.cpp`, `SkilPal.cpp`, `Skills.cpp`, `SUNITNPC.CPP`,
  `SUnitEvent.cpp`, `AiThink4.cpp`, `a3q5.cpp` (a quest script).
- PD2's own (template project): `DrawHoverItem.cpp`, `Sound/Sound*.cpp` (channel, instance, sample), `CubeFunctions.cpp`, `D2CommonFunctions.cpp`,
  `D2NewFunctions.cpp`, `D2ServerFunctions.cpp`, `D2ClientFunctions.cpp`, `HelperFunctions.cpp`, `ItemFunctions.cpp`, `SaveFunctions.cpp`,
  `GuildClient.cpp`, `GuildCommon.cpp`, `GuildGame.cpp`, `GuildShowcase.cpp`.

Feature evidence from strings and `ProjectDiablo.json`:

| Feature | Evidence | Triage | Size | Risk |
|---|---|---|---|---|
| Custom sound system (volume groups, priorities, custom volume per sound, bad-sound guard) | `Sound*.cpp`, volume keys for items, monsters, skills, combat, ambient, voice, quest, objects, drop, `Bad Sounds.txt` | (a) for rows and volumes; D2R sound table does the rest. Export `PlaySoundWithCustomVolumeOrPriority` implies per-call volume: (b) small sound service | S | L |
| Custom item rules (new item types, charm "only single unique" rule, item quantity, discovery state, storage type) | `ItemTbls.cpp`, `Items.cpp`, `ItemMode.cpp`, `CmnItems.h`, `CharmOnlySingleUniqueDesc`, exports `GetItemDiscoveryState`, `GetItemStorageType` | (c) native hooks for the equip/limit rules; item rows are (a) | M | M |
| Item discovery tracking ("discovered chat msg", "always show undiscovered/unowned", holy grail aura) | `ItemDiscoveredChatMsg`, `AlwaysShowUndiscovered`, `AlwaysShowUnowned`, `HolyGrailAura` | (b) service holding per-character or account discovery state, (d) grail UI | M | M |
| Stash system: categories (uniques, sets, gems, runes, maps, misc, bossing, storage) with sub-categories by item type, search box, private/shared tabs, hardcore-shared stash, currency tab, cube-from-stash hotkeys, "stash affinities" auto-sort | resource names `stashtabs`, `stashcategories`, `stashsearch`, `TradeStash`; `pd2_shared.stash`, `pd2_hc_shared.stash`; keys `HotkeyCurrencyTabItem*`, `HotkeyStashCubeItem*`; JSON `stash_affinities` | (b) save-extension service for stash pages and shared files, (d) full UI rebuild, (c) probably a few hooks for transmute-from-stash | XL | H |
| Hover-item tooltip changes (stat ranges, item level, quantity, tooltip screenshot, help text on stat hover) | `DrawHoverItem.cpp`, `ShowStatRange`, `ShowItemLevel`, `HOTKEY_TooltipScreenShot`, `HelpTextStatHoverBase/Item` | (d) tooltip rebuild; strings are (a) | M | M |
| Advanced stats panel and chat stat commands | `AdvStats`, `.allstats`, `.accountstats`, `StatsCommand*`, `HotkeyAssignStats*` (ctrl/shift assign) | (d) panel; ctrl/shift assign is (c) or (d) | M | M |
| Inventory lock / equipment lock, auto-equip, belt status, hide potions | `InventoryLock`, `equipment_lock`, `auto_equip`, `BeltStatus`, `HideHealingPotions`, `HideManaPotions` | (d) UI plus (b) settings; check D2R before building (it has some of these) | S | L |
| In-game overlays: DPS counter, buff timers, skill bar, screen shake toggle, mini skill icons | `DPSCounter`, `PlayerDpsDisplay`, `buff_timers`, `SkillBar`, `ScreenShake`, `skillicons_mini` | (d); DPS needs a damage event feed (b) | M | M |
| Quick cast, per-skill auto-aim disable | `QuickCast`, `skill_auto_aim_disabled` | (a)/(b): D2R has quick cast; auto-aim exceptions are a setting | S | L |
| Auras and event markers (dev, max level, Rathma, Diablo Clone, PvP, Lucion, holy grail) shown as client auras | `*Aura` keys, `dclone_aura` | (a) states/overlays for visuals; (b) service flips them from game state | M | M |
| Map events and notifications (Dark Wanderer, Gheed, Horazon, Mendeln, Shard, Tainted, Tomb, Treasure, Invaders by class, timers) | `MapEventNotify*` keys | (b) event service reading game state; (d) notification UI. Rules behind the events are data plus server hooks and need their own ticket | L | M |
| Automap options (centered, fade, party, party names, classic mode) | `AutoMap*`, `MapClassic` | (d) small; D2R has most | S | L |
| Controller support (XInput): movement, aim assist, deadzone, sensitivity, button styles, per-screen navigation help, 13 skill-select binds, multi-button loot, crosshair | `XINPUT1_4.dll` import, `Controller*` keys, `controllerhelp`, `controllermenu` | D2R has native controller support: (e) cut and then re-add only PD2-specific screens | M | M |
| Quick save of hotkeys, `.savehotkeys`, `.savepath` commands | strings | (b) | S | L |
| Custom save data (save bits, param bits, item decode failure guards, "Saved act", socketed item decode/insert) | `SaveFunctions.cpp`, `save bits`, `save param bits`, `itemstatcost` readers | Highest-risk area: PD2 widens item stat storage. Save compatibility is its own ticket. (b)/(c) | L | H |
| Server rules in game DLL patches: skill palette/charge, NPC behaviour, unit events, player modes and messages, an AI think routine, quest 3-5 script, experience regained message, phasing stat | `SkilPal.cpp`, `Skills.cpp`, `SUNITNPC.CPP`, `SUnitEvent.cpp`, `PlrModes.cpp`, `PlrMsg.cpp`, `AiThink4.cpp`, `a3q5.cpp`, `StrPhasingStatDesc`, `Your character gains experience again.` | (c) hooks per rule, or (a) if the rule is a table value. Needs the data diff to count. Estimate 10 to 30 rules | XL | H |
| Cube extensions | `CubeFunctions.cpp` | (c) hooks on the cube output/op handlers (the toolkit notes D2R's cube executor is narrow); recipes themselves are (a) | M | M |
| Rewritten item and mercenary strings (merc descriptions) | `MercDesc*`, string-table keys | (a) strings | S | L |
| Guilds: name, tag, MOTD, members, ranks (promote/demote/transfer), disband, showcase, guild ladder, guild inventory stored as a blob, guild icon picker | `Guild*.cpp` (4 files), `Caps*Guild*` strings, `GuildLadderbckg`, `guild2` to `guild6`, `guildicon_select` | (e) cut: server-bound, online scope. Large UI and server code | XL | H |
| Battle.net replacement: realm "ProjectD2", game list filters, ladder recreate, account stats | `bnet` JSON block, `BnetRecreateLadder`, `libcrypto` import, SHA256 | (e) cut. Offline first; private multiplayer is a later ticket | L | n/a |
| Crash/diagnostics: boost stacktrace, hook interception log | `boost_stacktrace_impl_return_nullptr`, `*_INTERCEPT` names | (e) | n/a | n/a |

Note the `*_INTERCEPT` names (for example in the hover-item and mouse-over unit paths): PD2 intercepts classic client functions by address.
None of those addresses transfer to D2R. Every item in the (c) and (d) rows is a rewrite against D2R's functions, not a port.

### BH.dll

BH is the open-source Blizzhackers overlay, heavily modified by PD2 (the install ships its license file). Exports `BHOnPropertyBuild`,
`BHOnDamagePropertyBuild`, `BHOnProperties` show that `ProjectDiablo.dll` calls into it while building item property text.

| Feature | Evidence | Triage | Size | Risk |
|---|---|---|---|---|
| Loot filter with a rule language: `ItemDisplay`, regex, stat-id and quality conditions, runeword, maptier, filter levels (page up/down), local and online filter folders (`filters\local`, `filters\online`), `default.filter`/`loot.filter`, reload command, per-filter selection | `ItemDisplayFilterName`, `filter_level*`, `FILTER REGEX ERROR`, `Reloaded filter: %s`, `selectedFilter`, `SelectedAuthorAndFilter` | (b) filter engine as a plugin service (parser, evaluator); (d) item name and notification rendering; (c) hook where D2R builds item name text. The filter is the core PD2 player experience | L | H |
| Advanced item display (`advanced_item_display`, `allow_unknown_items`, `always_show_stat_ranges`, `show_iLvl`, ethereal, sockets, rune numbers, alt item style, color mod, shorten names) | `legacy_settings` in `BH.json` | (b)+(c) same engine as above | M | M |
| Drop and close notifications, detailed/verbose notifications, drop sounds, "always show items" | `drop_notifications`, `close_notifications`, `drop_sounds`, `always_show_items` | (d) with the filter | M | M |
| Auto party, auto corpse loot, looting/party toggles, `Allocated Loot` | `auto_party`, `auto_corpse_loot` | (b) light; D2R multiplayer later | S | L |
| Stash export (JSON or markdown via templates, include equipment, copy item to clipboard) | `stash_export`, `mustache_*`, `Exported stash to:` | (b) optional; low value for a port | M | L |
| Screen info (game name, password, level, difficulty, clock, ping), experience meter, stats on right | `screen_info`, `AutomapInfo`, `experience_meter`, `stats_on_right` | (d) small; D2R has a clock and ping option | S | L |
| Stats display (resists, curse resist, shrine and state names) | `StatsDisplay.cpp`, `Cold/Fire/Lightning/Poison Resist:` | (d); D2R character panel shows resists | S | L |
| Players gear display hotkey, monsters remaining counter | `BHShowPlayersGear`, `BHSetMonstersRemaining` | (d) | S | M |
| Game list filter and autofill (last game, password, description) | `bnet` block | (e) | n/a | n/a |
| Draggable window UI (`bh_ui` positions), config reload key | `bh_ui` JSON | (e) replaced by D2R settings or plugin console | n/a | n/a |

Not assessed: whether BH ships any map-reveal behaviour. The config and strings contain no maphack keys, only the automap info lines above.

### PlugY.dll (third-party, PlugY Survival Kit)

Source files: `ExtendedSaveFile.cpp`, `SharedSaveFile.cpp`, `LoadPlayerData.cpp`, `SavePlayerData.cpp`, `InfinityStash.cpp`, `SkillsPoints.cpp`,
`NewInterface_Stats.cpp`, `NewInterface_StatsPageTwo.cpp`, `NewInterface_Runewords.cpp`, `MainScreen.cpp`, `ExtraOptions.cpp`, `UpdateClient.cpp`,
`PlugYFiles.cpp`, `D2functions.cpp`, `D2wrapper.cpp`, `GlobalVariable.cpp`, `Parameters.cpp`. PD2's `PlugY.ini` enables:

| Feature | PD2 setting | Triage | Size | Risk |
|---|---|---|---|---|
| Multi-page stash, 10 pages per index and 100 per second-level index | `ActiveMultiPageStash=1` | (b) plus (d). Superseded in practice by PD2's own stash system in `ProjectDiablo.dll`; confirm which owns the stash UI before building | L | H |
| Shared stash, hardcore-separate, shared set-item colour | `ActiveSharedStash=1`, `SeparateHardcoreStash=1` | (b) save extension service (D2R has a shared stash; D2R's hardcore split exists) | M | M |
| Stats point unassign (key 18) | `ActiveStatsUnassignment=1` | (c) small hook, or (d) UI; D2R's respec rules differ | S | L |
| Commands (`ActiveCommands=1`), extended save format | `ExtendedSaveFile.cpp` | (e) the extended save belongs to PlugY's own character files; D2RLoader has its own extended save | n/a | n/a |
| Big stash, windowed mode, language change, save path, shared gold, runeword and stats pages | all `0` in PD2's ini | (e) off in PD2, not needed | n/a | n/a |

### D2HD.dll, d2gl, cnc-ddraw, SGD2FreeRes, PD2_EXT.dll

All (e) cut. They give resolution, scaling and shader effects that D2R renders natively, or are loader plumbing (`PD2_EXT.dll` hijacks
`version.dll` to load `ProjectDiablo.dll`; a D2RLoader plugin replaces that entry point). d2gl's settings (FXAA, bloom, sharpen, HD cursor
and text, HD orbs, mini map) are display preferences, not game rules. One design lesson to keep: PD2 gets a 1068x600 playfield from D2HD or
SGD2FreeRes (the shipped config says "1068x600"). D2R's UI scale differs, so UI rebuilds must be written for D2R's layout, not copied pixel positions.

## 3. Summary by bucket

| Bucket | What goes there |
|---|---|
| (a) data only | Sound rows and volumes, merc and item description strings, auras as states and overlays (visuals), cube recipes, item rows, auto-aim settings |
| (b) D2RLoader service | Loot-filter engine, stash/save extension (pages, shared and hardcore-shared files, categories), item discovery state, map-event service, DPS/damage feed, hotkey persistence, auto party and corpse loot |
| (c) native hook | Item equip and limit rules (charm unique rule), item name builder hook for the filter, cube output extensions, unit event and skill palette rules from the server patches, stats-assign shortcuts |
| (d) UI rebuild | Stash tabs, categories and search, advanced stats panel, tooltips, DPS counter, buff timers, skill bar, notifications, map-event toasts, grail panel, belt/inventory lock |
| (e) cut | Guilds (for now), Battle.net/ladder replacement, controller layer (D2R native), all renderer and resolution DLLs, PD2_EXT shim, BH game-list tools and window, PlugY options PD2 leaves off |

## 4. Rough build order by value

1. Loot filter engine (B, then the item name hook): highest daily-use impact, self-contained, well-specified by `BH.json` and the filter files.
2. Stash system: needs the save-compatibility decision first (map item "save/character compatibility"), so it blocks on that.
3. Server rule hooks: size them only after the data-table diff finds which rules are not table values.
4. Event/notification features and overlays (DPS, timers): independent, low-risk, can follow any time.
5. Guilds and Battle.net replacement: leave to the multiplayer phase if at all.

## 5. Gaps and follow-ups

- Hook-vs-data split for the server patches needs `pd2data.mpq` compared to vanilla: new `srvdofunc`, `cltdofunc`, `pSrvHitFunc`, `pSpell`
  and cube `op` numbers, and extra columns. That is a different ticket.
- String evidence is weaker for `ProjectDiablo.dll` than for PlugY: it has few assert paths, so the count of server rules is a guess.
- The PD2 asset resource names in the stash UI mean new HD art (stash tabs, category icons) is needed. Stand-in first, per the map policy.
- Save format: PD2 stores items with widened bit fields, uses `.stash` shared files, and (via PlugY settings) may use extended save files.
  Whether PD2 characters can load in D2R is unresolved and blocks the stash decision.
- Not checked: BH loot filter file contents under `filters\`, and any runtime behaviour. Both would need to be read, not run.

## Sources inspected

Local PD2 install files named in the Method paragraph (headers, import/export tables, strings, and the config files listed there), plus
`knowledge/porting-classic-mods.md` (sections 0 to 1, 5 to 6) and `knowledge/engine-re.md` in the d2rAI-toolkit for the bucket definitions and
D2R's existing hook points.
