# D2RLoader 1.3.1 and PluginSDK 0.4.0: capability matrix

Research for issue #4 (map #1). Target: D2R 3.3 build 93847 (PE TimeDateStamp `0x6ab3782c`), D2RLoader 1.3.1.
Use this to check PD2 features: find the system in the matrix, read the verdict, follow the source.

## Sources

| Tag | Source | Trust |
|---|---|---|
| SDK | PluginSDK README, https://github.com/D2RLoader/PluginSDK (SDK 0.4.0, plugin ABI 4; loader accepts ABI 2 and 3) | Primary, official |
| ORG | https://github.com/D2RLoader (repos: Installer, PluginSDK, localization, D2RL-Plugins, wiki; site d2rloader.net) | Primary, official |
| KB-loader | `d2rAI-toolkit/knowledge/d2rloader.md` | Local, observed on 1.3.1 / 93847 |
| KB-sys | `d2rAI-toolkit/knowledge/new-game-systems.md` | Local, design guidance |
| KB-lim | `d2rAI-toolkit/knowledge/engine-limits.md` | Local, observed on 93847 |
| KB-addr | `d2rAI-toolkit/knowledge/d2r-93847-addresses.md` | Local, RVAs for 93847 only |
| KB-save | `d2rAI-toolkit/knowledge/saves.md` | Local, observed |
| TPL | `d2rAI-toolkit/plugin-template/` (README, `src/`) | Local, working template |
| TEST | `d2rAI-toolkit/docs/testing.md`, `testing/` | Local |

Caveats: the SDK README describes SDK 0.4.0, while KB-loader reports loader 1.3.1-beta is older than the SDK, so some
service fields are missing (marked below). Local facts were observed on 93847 and not re-verified here. Only the SDK
front page was read, not its individual headers; per-header detail needs a source read before an implementation ticket
relies on it.

## 1. How a plugin is built, loaded and tested

| Topic | Fact | Source |
|---|---|---|
| Toolchain | C++ DLL, Windows x64, CMake 3.29+, MSVC or clang-cl. Static CRT works | SDK, TPL |
| Minimum plugin | Embedded manifest resource (RCDATA id 1001 = ABI DWORD), exports `D2RLoaderGetPluginInfo`, `D2RLoaderLoadPlugin`, optional `D2RLoaderUnloadPlugin`. Without the resource the loader rejects the DLL as an "old alpha plugin" | SDK, KB-loader |
| Roles / flags | Exactly one of `Client`, `Server`, `Shared`; optional `ModScopedOnly`, `NativeHooks` (needed for inline hooks) | SDK |
| Where it loads | `<game>/mods/<mod>/d2rloader/plugins/` (mod-scoped, loaded first, replaces a same-id global copy) or `<game>/d2rloader/plugins/` (global, loads with every mod) | SDK, KB-loader |
| Which mod runs | `d2rloader.toml` `default_mod` (game-wide); a mod's own toml only overrides `[d2rcore.*]` | KB-loader |
| Config | One embedded default TOML (non-empty, <= 1 MiB); user config created once, never overwritten | SDK |
| Tables | The loader compiles the mod's `.txt` tables itself (no `-txt`); a shipped `.bin` becomes a "custom BIN set" that must be complete | KB-loader |
| First start | First start after install/update builds a compiler snapshot and loads only D2R's own sounds table; mod sounds resolve to -1 (silent, can crash). Needs a second start; guard via `DataTablesLoaded` | KB-loader |
| Packaging | `d2rl plugin pack` builds DLL + companion MPQ (`d2rloader/<plugin-id>/`) | SDK |
| Logs | `<game>\d2rloader\logs\d2rloader.log`; crashes in `<game>\d2rloader\crashes\`; plugin log in `mods\<Mod>\d2rloader\logs\` | KB-loader |
| Build specificity | Engine hooks use RVAs valid for one image. Template checks PE timestamp and hook entry bytes, turns parts off on mismatch. Check game major.minor, not build number | KB-loader, KB-addr, TPL |
| Debugging | Per-part off-switch file `<name>-off.txt`, status console command, guards (build, env, sound, item type ids) | TPL |
| Testing | Python harness: separate game "slots", locks, scripted launch, screenshots, keys, console lines, named checks with results JSON. Click/key/console control needs a plugin implementing the harness protocol | TEST |
| Reverse engineering | Ghidra workflow; each confirmed address recorded in `d2r.h` and the registry | KB-sys, KB-addr |

## 2. Capability matrix

Verdict: **Yes** = supported service or documented path; **Plugin** = possible with native hook/RE work;
**Data** = table change only; **Limit** = hard limit or partial.

| Area | Capability | Verdict | Detail | Source |
|---|---|---|---|---|
| Data | Read compiled tables from a plugin | Yes | `data_tables.h`, read-only | SDK |
| Data | Plugin-owned custom TXT/BIN tables | Yes | Staged activation; V1 columns `Ascii`, `Byte`, `Word`, `Dword`; strict TXT format | SDK |
| Data | Table rows | Limit | 32,768 per table; indexed tables append-only (never reorder); D2R-only rows keep D2R numbers; mod stats go after itemstatcost row 368 | KB-lim |
| Data | Field widths / key lengths | Limit | Per dataguide memSize / dataLength; signed values must be written signed | KB-lim |
| Items | Discover / create / edit items, transactions | Yes | `item.h`, `item_interactions.h`; batches <= 4,096 ops; no equipment/cursor/belt/trade/corpse/ground moves in atomic moves | SDK |
| Items | Add properties to an existing item | Limit | `editItem` and existing-item transactions cannot; a unique can be rebuilt from its seeds with an added `PropertySpec` (a new item) | KB-loader |
| Items | Custom inventory pages | Yes | Retail save: 1 page; extended save: up to 247 pages (numbers to 252), grid 1-16 x 1-16 | SDK, KB-loader |
| Items | Item details / tooltip getters | Limit | `ItemService` is 88 bytes on loader 1.3.1 vs 112 in SDK 0.4.0; `getItemDetails`, `getItemTooltip`, `getItemPreviewTooltip` missing (crash if called). Check `HasItemServiceField` | KB-loader |
| Items | Tooltip lines | Limit | Listeners add lines; colour codes print literally; client copy of item reads no custom stats | KB-loader |
| Items | Socket / stack / skill rules | Yes | `game_rules.h` | SDK |
| Items | Affix augmentation | Yes | Magic 1+1, rare 3+3 (6 total), rare jewel 4 | SDK |
| Items | Set items per set | Limit | 6 | KB-lim |
| Items | Runeword properties | Limit | 7 slots (`T1Code1-7`) | KB-lim |
| Items | Item spells (`pSpell`) | Plugin | 20 entries; 16-19 free; 20+ does nothing; hook dispatchers for custom ones | KB-lim |
| Items | New item stats | Limit | New itemstatcost row needs `Save Bits` (stock max 24) or it is lost on reload; function-28 stats need `Save Param Bits` | KB-loader, KB-lim |
| Cube | Custom recipe conditions (`op`) | Plugin | Unknown ops pass silently; hook `0x529090` / `0x529d50` | KB-lim |
| Cube | Transmute interception | Yes | UI message `HoradricCubePanelMessage` / `Convert` can be consumed; plugin does its own work on the game thread | KB-loader |
| Cube | Portal output | Plugin | Opens only the Cow Level; hook the output handler | KB-lim |
| Skills | New server do-functions | Plugin | srvdofunc slots 172-190 free (191 total) | KB-lim, KB-addr, TPL |
| Skills | New client do-functions | Plugin | cltdofunc 110-129 free | KB-lim |
| Skills | Skills per class | Limit | 30 in retail save; more with the extended save format (an overhaul ran several times 30) | KB-lim, KB-save |
| Skills | New calc codes | Plugin | Switch returns 0 for unknown rows; hook `0x33b3b0` / `0x3b9dc0`; keep ids < 256 | KB-lim |
| Skills | Formula limits | Limit | 255 chars; functions fixed (`min max rand skill miss stat sklvl sksrc cond`); no "has state" test | KB-lim |
| Skills | Skill icons (HD) | Limit | <= 62 per class strip; class-less skills share one sheet | KB-lim |
| Skills | Skill tree layout | Limit | 6x3 per tab; more than 3 tabs works in HD, titles need art | KB-lim |
| Missiles | New hit/do functions | Plugin | Free slots: srvhit 61-70, srvdo 46-54, clthit 66-80 | KB-lim |
| States | State count | Yes | Assert above 511 is harmless; D2RCore syncs wide states up to 1,024; never patch state arrays | KB-lim |
| Events | Lifecycle events | Yes | Table loads, game state, quests, monster deaths (`lifecycle_events.h`) | SDK |
| Events | Unit-death property events | Plugin | D2R raises only its own events; hook `0x44db20`, raise via `0x44d570` | KB-lim |
| Spawns | Change monsters as they spawn | Plugin | Hook spawn function `0x492cf0`; confirm which path the level populator uses | KB-sys, KB-addr |
| Spawns | Boss / herald modifiers | Yes | `ModifierPolicy` in `encounters.h`: 0-9 modifiers, 0-2 auras, 0-10000 percent damage; the game may add fewer | SDK |
| Levels | New areas | Data + Limit | Level id <= 255; `Layer` < 100; clear `Position` on new mazes; ship every tile file; portals within one act only; automap level types fixed at 36; automap cels < 1499 | KB-lim |
| Levels | Portals to ids > 255 | Plugin | Setter clears the high byte; write it afterwards | KB-lim |
| UI | Plugin panels and widgets | Limit | `panels.h`, `widgets.h`. On 1.3.1: no `setText` (fixed text only; show/hide works), `setWidgetEventMessage` is null (crash), buttons need a stock target `PanelManager:OpenPanel:<id>/<name>` | SDK, KB-loader |
| UI | Overlay drawing | Yes | Display-only, after the game UI, no input | SDK |
| UI | Widget text / tooltip size | Limit | 4,095 / 16,383 UTF-8 bytes | SDK |
| UI | UI message and tooltip hooks | Yes | `shared_events.h` | SDK |
| UI | Edit existing layouts (HD json) | Data | Grid size on every inventory row + layout `cellCount` | KB-lim, KB-sys |
| Input | Named actions and key listeners | Yes | Not during chat/text entry. Default keys are stored on first register in `input-bindings.toml`; later default changes do not reach existing players | SDK, KB-loader |
| State | Item stats | Yes | Saved with the character; needs Save Bits | KB-sys, KB-lim |
| State | Plugin memory per game | Yes | Lost on exit | KB-sys |
| State | Quest flags / waypoints | Yes | Few free bits | KB-sys |
| State | Own file | Plugin | Your format; keep next to the save; never touch a `.d2s` | KB-sys |
| Saves | Save format | Limit | `.d2s` v105 plus D2RLoader `.d2rl` sidecar (SHA-256 hashes, size, checksum). Extended format: 16-bit stat ids, more class skills, plugin-owned pages. A mismatch makes the character vanish or grey out; hand-built items are rejected | KB-save, KB-lim, SDK |
| Saves | Missing page plugin | Yes | Loader backs up the save and loads without those items | SDK |
| Saves | Loader backups | Yes | `Saved Games\Diablo II Resurrected\D2RLoader Backups\` | KB-save |
| Strings | String overrides | Yes | `localization.h`, namespaced JSON | SDK |
| Strings | Mod string IDs | Limit | Matched on low 16 bits; new IDs from `next_string_id.txt` (28106) upward; printf placeholders must match | KB-lim |
| Resources | In-memory files, companion MPQ | Yes | `resources.h`; a mod can override plugin resources | SDK |
| Engine | Inline hooks / byte patches | Yes | Atomic, with rollback; needs `NativeHooks`; RVAs are build-specific | SDK, KB-addr |
| Engine | Detect patched bytes | Yes | `diagnostics.h` | SDK |
| Engine | Low-level D2RCore exports | Yes | `core_exports.h` (1-99), `reimplementation_exports.h` (2000-2999) | SDK |
| Engine | Renderer pool | Limit | Fixed 12 MB arena; busy scenes crash; plugin heap fallback possible | KB-lim |
| Engine | Coexisting with other gameplay plugins | Limit | Same hooks conflict; detect and warn, do not try to support | KB-loader |
| Misc | HTTP | Yes | Async HTTPS and loopback HTTP | SDK |
| Misc | Threading, inter-plugin events | Yes | `threads.h`, `plugin_communication.h`; payloads <= 1 MiB; keep callbacks short | SDK |
| Misc | Item types tested by number | Limit | Keep D2R's ids (jewl 58, rune 75, ...) | KB-lim |

## 3. Multiplayer implications

| Topic | Fact | Source |
|---|---|---|
| Scope | Local and TCP/IP games; the plugin network service is not available on Battle.net | SDK |
| Server-side rules | Spawns, drops, portals, skills run on the server side, even in single player. The client sees only what D2R syncs (units, stats, states, items) | KB-sys |
| Matching | `Shared` plugins must match host and client on id, version, ABI, scope and flags; bump version every release. Client-only plugins are not recorded | SDK, KB-sys |
| Config | Settings in `[d2rl] match` (<= 64 paths) must be equal; the embedded list is authoritative | SDK |
| Compatibility token | 64-bit token must match on both sides | SDK |
| Item changes | Only local games or the TCP/IP host change items; clients message the host (`getPeerPlayer`) | SDK |
| Messages | Private plugin messages, <= 224 bytes | SDK |
| Save history | Server and Shared plugins are recorded in the character's plugin history | SDK |
| Implication | Private TCP/IP multiplayer is workable if all gameplay hooks are `Shared` and versions match. State only the server plugin knows is invisible to clients unless it maps onto D2R's sync. Battle.net-style online play is out of scope | SDK, KB-sys |

## 4. Hard limits (for triage)

- Table rows 32,768; append-only indexed tables; D2R-fixed item type numbers.
- 30 class skills unless extended save format; 62 HD icons per class strip.
- Free slots: srvdo 172-190, cltdo 110-129, item spell 16-19, missile srvhit 61-70.
- Level ids <= 255 for portals; `Layer` < 100; 36 automap level types; portals within an act.
- 511 states assert (harmless), 1,024 wide states supported, beyond that unexplored.
- Set 6 items; runeword 7 properties; formula 255 chars.
- All RVAs are build 93847 only.
- On loader 1.3.1: panel text cannot change; item tooltip getters missing.

## 5. Not found / open questions

- Per-header API detail for the 0.4.0 services (only the README summary was read).
- Whether the wiki or D2RL-Plugins repo document services beyond the SDK README.
- Behaviour above 1,024 states (unexplored per KB-lim).
- Whether client-only PD2 visuals not synced by D2R can be shown through overlay or panels alone.
- Which listed hook points D2RLoader already hooks itself (check for `FF 25` at the entry before hooking).
