# Hosted leagues: servers, providers, cost

Research for running pd2-resurrected leagues on our own hosted servers, the way Reimagined runs its ladder. It builds on
[multiplayer-options.md](multiplayer-options.md) (read that first: TCP/IP, the EULA, Reimagined's three launch modes).
Sources read 2026-11-18. Web pages are cited by URL, repo files by path and line. Reimagined's repos were read at their
default branches that day (`reimagined-launcher` at `7fb009b`). Nothing here was run in a game or on a server.
**Unconfirmed** marks what no primary source states; **estimate** marks our own arithmetic. Not legal advice.

## Summary

- A hosted league is five things: a **headless game server** (D2R plus a D2RLoader plugin), a **lobby** that puts
  games on those servers, **server-owned characters**, **one account** across launcher and website, and a **signed
  release** the server can require. Reimagined appears to have built all five in about two weeks (our inference from
  the Ladder wiki page's creation date, 2026-11-04, `wiki-content/Ladder.md` line 8; build dates are not public) on top
  of a website and API that existed before.
- **Nothing headless is available to us.** D2RLoader's docs and changelog never mention a dedicated mode
  (https://d2rloader.net/docs.html, https://d2rloader.net/changelog.html). Reimagined's `d2rl-d2rds.dll` lives in a
  private repo (`d2rl-plugins` returns 404), and none of its repos carries a license (GitHub API `license: null` for
  `reimagined-launcher`, `d2r-reimagined-website`, `wiki-content`), so we may read but not copy them. We build our
  own, or ask.
- **It is feasible:** D2R.exe already contains the game server (single player runs a local server; the server-side
  game table has 1024 game handles, `Pyre-Flies/D2R-Mods` `docs/reverse-engineering/README.md` line 94), and
  Reimagined proves a plugin can run it with no player session.
- **Biggest risk is Blizzard's EULA, not cost.** Section 1.C.viii forbids hosting "matchmaking services for the Game(s)"
  and 1.B.ii limits use to "personal and non-commercial entertainment"
  (https://www.blizzard.com/en-us/legal/fba4d00f-c7e4-4883-b8b9-1b4500a402ea/blizzard-end-user-license-agreement,
  revised 2024-03-21). No Blizzard action against a hosted D2R mod server was found (section 3.4).
- **Cost headline (estimates, 2026-11-18 prices, Windows boxes on OVH Game servers):** small test league (~50
  concurrent) about **$120-150/month**; mid (~500) about **$700-850/month**; large (~2,000+, three regions) about
  **$2,700-3,200/month**. The same large league on AWS on-demand Windows is about **$12,000/month**. Capacity per box
  is unmeasured; beta run 1 must measure it (section 4.3).

## 1. The suggested path, step by step

The path from [multiplayer-options.md](multiplayer-options.md#what-this-means-for-pd2-resurrected): (1) player-hosted
TCP/IP for beta run 1, (2) plus launcher and install check, (3) a hosted league for run 2. Each step below lists our
work, what breaks if we skip it, and what carries into the hosted league.

### 1.1 Step 1: player-hosted TCP/IP (beta run 1)

| Work item | Detail | Breaks if skipped |
|---|---|---|
| Keep every gameplay hook in a `Shared` plugin | Already so: `core-rules` and `core-events` are Shared (`plugin/core-rules/plugin.cpp` line 54, `plugin/core-events/plugin.cpp` line 50), `core-ui` is Client (`plugin/core-ui/plugin.cpp` line 57) | A Server-only rule runs on the host alone and clients desync, or a Client rule changes gameplay unchecked |
| Bump each Shared plugin's version every release | The loader matches Shared plugins by id, version, ABI, scope and flags (toolkit `third_party/PluginSDK/README.md` lines 193-196) | Two builds with the same version but different code join each other |
| `[d2rl] match` list in each Shared plugin | Today no plugin config in `plugin/` declares one (grep for `[d2rl]` finds none). List every gameplay setting, up to 64 paths (SDK README lines 99-121) | A host with a changed drop or event setting plays different rules than its clients, silently |
| Client-to-host plugin messages | Client-side item or game-thread work goes to the host by `NetworkService` message, <= 224 bytes (SDK README lines 1125-1150; `knowledge/d2rloader-capabilities.md` lines 117-118) | Features that work in single player do nothing on a TCP/IP client (a client has no game thread) |
| Two-player game checks | W37's shared shrines first (`origin/research/w37-server-rules`, `docs/research/w37-server-rules.md`) | Multiplayer-only rules ship untested: a single-player check never proves sync (`knowledge/d2rloader-capabilities.md` line 113) |
| Measure the host | CPU, memory and network of the host's D2R process per player and per game, and the player cap per game | Section 4's sizing stays a guess |
| Player guide "Play with friends" | Offline character, Multiplayer screen, port TCP 4000, Public/Unlisted/LAN, what must match, save backups (https://d2rloader.net/tcp-ip.html) | Support load: "cannot join" and port questions |

**Carries into the league:** the Shared/Client split, the match lists, the message protocol and the multiplayer test
habit are exactly what a dedicated server needs, since a dedicated server is a TCP/IP host with no local player
(`reimagined-launcher` `ReimaginedLauncher/Utilities/LocalD2rdsRigService.cs`, header comment: "-d2rds ... which the
d2rds plugin turns into a headless server"). The measurements size the servers.

### 1.2 Step 2: launcher and install check

The owner already has a launcher: `synpoox/resurrected-launcher` (Tauri, MIT; README *What it does*, *License*). Its
accepted contracts (issue [#7](https://github.com/synpoox/resurrected-launcher/issues/7), resolution comment) already
decide most of the league's account shape: one project account across website, launcher and services, separate from
Battle.net and Steam; browser login with authorization-code/PKCE; short-lived, one-use "hosted join" grants; offline
play never needs an account. Its ADR 0004 bars manually installed plugins from "project-hosted multiplayer and ladder
play" (`docs/adr/0004-manual-plugins-allowed-in-direct-coop.md` line 3) and ADR 0002 says only hosted services own
tradeable inventory (`docs/adr/0002-hosted-services-own-tradeable-inventory.md` line 3).

| Work item | Detail | Breaks if skipped |
|---|---|---|
| Pin and check the loader version | `release.toml` already pins D2RLoader `1.3.1-beta` and game build `93847`; the launcher shows "missing or the wrong version" (launcher README *System requirements*) | Clients older than the host's loader cannot join (1.2.1-beta changelog, https://d2rloader.net/changelog.html) |
| Turn on the TCP/IP button | Write `show_tcpip_button = true` in `d2rloader/config/d2rloader.toml`, as Reimagined's launcher does (`ReimaginedLauncher/Utilities/D2RLoaderLaunchSettings.cs`, `UpdateDefaultMod`) | Players cannot find Multiplayer |
| File hashes per release | Already produced: `release-manifest.json` schema 3 for the launcher (toolkit `release/README.md` line 12; `release.toml` `[catalog]`) | Repair cannot restore a clean mod, and "same version, different files" joins fail |
| Signed release metadata | Deferred in the launcher today (`docs/specs/launcher-v1.md` line 12: "signed game-release metadata ... remain deferred") | Needed in step 3: the server must refuse unsigned or altered packages |

**Carries into the league:** the manifest with hashes becomes the signed league package; the loader pin becomes the
server's required build; the launcher becomes the only way into hosted play (sign-in, join grant, locked settings).

### 1.3 Step 3: hosted league (beta run 2)

The components, as Reimagined built them (section 2), and ours to build:

1. Headless server plugin (our `d2rds`).
2. Lobby plugin plus a lobby API (game hosting and listing on our servers).
3. Server-saves plugin plus a save API (characters owned by the server).
4. Signed league package and plugin allowlist in the launcher.
5. Accounts, bans, admin panel, telemetry.
6. Servers in at least one region.

**Skip a part and:** no headless plugin means a staff PC must host every game (no 24/7 league); no server saves means
anyone can edit a character offline and join, so the ladder means nothing; no signed package means the server cannot
tell our build from a modified one (the loader only checks Shared plugins and data, not Client plugins, SDK README
lines 193-196, so client cheats are not stopped either way).

## 2. How a hosted D2R league works, end to end

Worked example: Reimagined's ladder. Their API is a separate, non-public service (the website calls
`api.d2r-reimagined.com`; its types name server-side classes such as `GameServer.MaximumGames`,
`d2r-reimagined-website` `src/lib/server-registry.ts` line 19).

### 2.1 Headless server

- **What D2R offers:** no dedicated server. Blizzard removed TCP/IP entirely
  (multiplayer-options.md section 1). The game server code is inside D2R.exe and runs even in single player
  (toolkit `knowledge/d2rloader-capabilities.md` line 112).
- **D2RLoader:** no dedicated or headless mode in its docs, TCP/IP page or changelog (pages above, read 2026-11-18).
  **Unconfirmed** whether its authors plan one.
- **Reimagined's `d2rds`:** a D2RLoader plugin `d2rl-d2rds.dll`. A launch with `-mod <name> -txt -d2rds` becomes the
  server; any other launch of the same folder is a normal client (`LocalD2rdsRigService.cs`, header and server
  config text "Only a launch with -d2rds becomes a server"). Its config (`d2rds.toml`, written by the same file) has
  `authoritative_saves = true`, `standing_games = false`, `cheat_operators`, `game_version = 3` and
  `max_players = 8`. Server builds are uploaded to the API as `d2rl-d2rds.dll` (<= 64 MiB) and rolled out per ladder
  with a countdown and rollback (`src/lib/server-builds.ts` lines 6-8, 43-53, 55-77). A supervisor on each box reports
  the installed build hash (same file, line 10).
- **Availability to us:** private source, no license (Summary). Options: build our own; ask Reimagined to license the
  plugin; ask D2RLoader's authors to ship a generic dedicated mode. **Unconfirmed** which is possible.
- **What our own needs** (estimate, from Reimagined's shape): start D2R with no menu and no local player, create and
  close games on request, accept TCP/IP clients on port 4000, run with no window or sound, survive for days, and
  report health. Whether D2R can skip rendering entirely (no GPU) is **unconfirmed**.

### 2.2 Game creation, lobby, regions

- **Boxes and processes:** an admin registers a box by public IPv4, region list, a server key (stored as its SHA-256),
  up to 8 `d2rds` instances per box, default 32 games per instance, hard cap 128 (`src/lib/server-registry.ts` lines
  4-24, 55-75). A box can be drained (`acceptingGames = false`: running games continue, new ones go elsewhere) or kept
  admin-only for tests (lines 8-14).
- **Hosting:** players host and join through a `d2rl-lobby-bridge.dll` plugin talking to the API
  (`LocalD2rdsRigService.cs`, required plugin list). The lobby picks a box; a ban flag `lobbyHostBanned` exists per
  account (`src/lib/auth.ts` line 17).
- **Regions:** the launcher pings every region and asks for the two fastest (`LobbyRegionSelection.cs`,
  `AutomaticRegionCount = 2`).
- **PD2 (classic D2) for comparison:** nine game servers: US-West (Las Vegas), US-Central (Iowa), US-East (South
  Carolina), Europe-West (Netherlands), Europe-North (Finland), Asia-Northeast (Seoul), Asia-Southeast (Singapore),
  South America-East (Sao Paulo), Australia-Southeast (Sydney) (https://wiki.projectdiablo2.com/wiki/FAQ). Those
  cities match Google Cloud region locations one to one; that PD2 runs on Google Cloud is our inference,
  **unconfirmed**. PD2's realm software is its own and not public (**unconfirmed**: no source describes it).

### 2.3 Server-owned characters and saves

- **Storage:** ladder characters live on the API. The `server-saves` plugin pulls the character manifest at load,
  pushes saves during play, and reconciles the local folder: a character the server does not list is removed, one
  whose bytes differ is replaced; with no API reachable the folder is emptied ("no server, no characters")
  (`reimagined-launcher` README, *Ladder* section, lines 63-105).
- **Backup and rollback:** nothing local is deleted outright; displaced files go to `.server-saves/backups/`,
  unaccepted progress to `.server-saves/pending-upload/` (README line 105). Server-side backup and rollback policy:
  **unconfirmed** (in the private API).
- **Separation:** a ladder gets its own `savepath` and its own shared stash; items cannot move between ladder and
  offline play (README lines 46-61).
- **Anti-dupe and save review:** the API flags saves by kind, among them `UnwitnessedItem`, `CrossAccountDuplicate`,
  `JournalIntegrity`, `ItemLegality`, `AffixRange`, `DropOdds`, `ExperienceRate`, `MovementSpeed`
  (`src/lib/save-review.ts` lines 4-26), per character, shared stash and account (lines 49-58), with occurrence counts,
  refusals and staff resolution notes (lines 28-47). The names suggest the server journals item events and checks each
  save against them; the method is **unconfirmed** (API not public). Saves are capped at 4 MiB on upload
  (`src/lib/character-save.ts` line 1); the launcher packs the mod's excel tables for "the API's save parser"
  (`LocalD2rdsRigService.cs`, `PackTables`).
- **Our save format:** `.d2s` v105 plus the D2RLoader `.d2rl` sidecar with SHA-256 hashes; a mismatch hides or greys
  out a character (toolkit `knowledge/d2rloader-capabilities.md` line 92). A save API must store both files together.

### 2.4 League lifecycle

- **Reimagined:** a ladder has `startDateUtc`, `endDateUtc`, `archivedAtUtc`, and can be hidden; admins may preview a
  scheduled ladder and "Nothing played in a preview is saved"; hidden tester ladders take uploaded saves
  (`src/lib/ladder-schedule.ts` lines 22-29, 56-63; `src/lib/tester-saves.ts`). Ladder-only rules are data in the
  package (rarer high runes, locked settings, `wiki-content/Ladder.md` lines 13-43). What happens to characters at a
  ladder's end: **unconfirmed** (no public page).
- **PD2:** at season end ladder characters move to non-ladder, shared-stash items do not; an open beta on the main
  realm tests the reset for 2-3 days, then downtime; beta downtime is about 5-10 days; Season 13 ran 182 days from
  2026-04-24 and Season 14 started 2026-10-23 (https://wiki.projectdiablo2.com/wiki/Seasons). The PD2 FAQ says the
  ladder resets every four months (https://wiki.projectdiablo2.com/wiki/FAQ), which the season table contradicts.
- **Ladder-only items in our data:** `runes.txt` already has `disallowCraftingInLadder`, `disallowCraftingInNonLadder`,
  `firstLadderSeason`, `lastLadderSeason` (`files/data/global/excel/runes.txt`, header columns 4-7). Whether a TCP/IP
  or `d2rds` game counts as ladder for the engine: **unconfirmed**. PD2's online recipe groups in `cubemain` were cut
  for single player (`docs/deviations.md` line 32) and come back with leagues.
- **For us:** migration to a non-league realm is a save-API job (move the files to the standard realm's manifest),
  not game code.

### 2.5 Accounts and identity

- **Reimagined:** website account with email and password, Steam sign-in, Battle.net and Discord links
  (`src/lib/auth.ts` lines 19-25, 261-283, 392-425); launcher sign-in via Nexus Mods (launcher README line 11) and a
  loopback PKCE code (`src/lib/launcher-auth.ts` lines 1-39; README line 146). The launcher writes the access token
  into `server-saves.toml`, so the plugin authenticates its own requests (README line 78). Per-account bans:
  trade, leaderboard, lobby host, global chat (`src/lib/auth.ts` lines 15-18).
- **Ours:** the launcher contract (section 1.2) already fixes the shape. Discord OAuth or email can be the login
  behind the website; no vendor is chosen, and issue #7 keeps the project account separate from Steam and Battle.net
  identity (issue #7 resolution).

### 2.6 Anti-cheat, signing, allowlists

- **Reimagined:** ECDSA-signed ladder package with SHA-256 per archive; undeclared local files block launch; only
  approved extensions load, others move to `ladder-disabled/` (launcher README lines 84-85, 121-154). The API stores
  an allowlist of plugins and patches with hashes and a required flag (`src/lib/ladder-schedule.ts` lines 4-15) and
  can pin the exact D2RLoader build (`src/lib/admin.ts` line 13). Debug and cheat tools are disabled by
  `.DISABLE_DEBUG` in the package (`LocalD2rdsRigService.cs`).
- **Limit:** all of that runs on the player's PC. The launcher's own ADR says "launcher preflight checks do not
  establish authoritative server enforcement" (`resurrected-launcher` `docs/adr/0004-manual-plugins-allowed-in-direct-coop.md`
  line 3). Real enforcement is the server: authoritative saves, D2RLoader's Shared-plugin and data match at join, and
  save review. Map hacks and other read-only client tools are not stopped by any of these (**unconfirmed**: no source
  describes a counter).

### 2.7 Trade, chat, admin, observability

- **Trade:** a website marketplace per realm (standard or a ladder) with listings filtered by mode and hardcore
  (`src/lib/trade-ladders.ts` lines 5-31); in game, `/sell` opens a sell window (`Ladder.md` line 78). The launcher
  contract keeps settlement in game: website listings help players arrange trades (issue #7 resolution).
- **Chat:** global chat, friends and DMs with a rate limit of 5 messages per 10 seconds, moderated by hand and
  automatically (`Ladder.md` lines 45-74). Admin pages for chat bans, global chat, announcements, users, servers,
  ladders, portals, feedback (`d2r-reimagined-website` `src/routes/admin/`).
- **Observability:** each `d2rds` process streams uptime, tick age and max tick time, memory, joins, leaves, and per
  game the players; logs by level; a frame every 2 s (`src/lib/server-telemetry.ts` lines 6-20, 74, 172-174). The
  supervisor reports the installed server build, bundle and D2RLoader version (lines 29-31, 56-62).

## 3. What the server needs to run D2R

### 3.1 Operating system

- **Windows** is the supported case (D2RLoader is Windows only, toolkit `docs/distribution.md`).
- **Linux with Wine:** D2RLoader 1.1.0-beta (2026-08-15) "improves Wine support" (https://d2rloader.net/changelog.html).
  Reimagined's telemetry expects `d2rds` on Linux hosts: D2RLoader.exe's version is "null on Linux hosts, where it
  cannot be read" (`src/lib/server-telemetry.ts` line 58). So a Wine server is at least planned by them; whether it is
  stable headless: **unconfirmed**. Linux saves the Windows license (section 4).

### 3.2 CPU, memory, GPU, bandwidth

- **Client requirements** (as reported; Blizzard's own support page needs a login): minimum Core i3-3250 / FX-4350,
  8 GB RAM, GTX 660; recommended 16 GB (https://www.pcgamer.com/diablo-2-resurrected-pc-system-requirements). A
  headless server should need far less, since it draws nothing; **unconfirmed** by any measurement.
- **Per game and per process:** no public figure. Reimagined's defaults (8 processes per box, 32 games per process,
  8 players per game) are limits, not measurements.
- **GPU or display:** **unconfirmed** whether D2R starts without a GPU when headless. Plan for a cheap box with
  integrated graphics until a test proves otherwise (OVH's Game range and Hetzner's AX42 have no discrete GPU).
- **Bandwidth per player:** no public figure (searches found none). D2R's game protocol is classic D2's, which ran on
  28.8 kbps modems (PC Gamer, above). Even 50 kbps per player (estimate) is 100 Mbit/s for 2,000 players, inside one
  1 Gbit/s port. Measure in run 1.

### 3.3 Licensing

- D2RLoader: "You must own Diablo II: Resurrected through Battle.net or Steam" (https://d2rloader.net/docs.html).
- Blizzard's EULA allows installing on devices "under your legitimate control", sets no device count, and limits use
  to "personal and non-commercial entertainment purposes" (sections 1.B.i and 1.B.ii, EULA URL above). Whether one
  owned copy may run several server processes for other players: **unconfirmed**, and likely outside "personal".
- Whether a directly started D2R.exe needs an online license check: **unconfirmed** (the toolkit notes patch-era
  "offline authentication" without an interval, `docs/distribution.md` lines 255-262).
- Windows Server: one license per box from the provider (section 4).

### 3.4 Blizzard exposure

- EULA 1.C.viii: no hosting of "matchmaking services for the Game(s)"; 1.C.ix: no connection to an unauthorized server
  that emulates the Platform; 1.C.ii: no unauthorized bots, cheats or hacks (EULA URL above). A lobby on our servers
  is matchmaking in plain words; `d2rds` is the game's own server code, not a Battle.net emulator, but runs it in an
  unauthorized way.
- Known Blizzard action: 2021 cease-and-desist letters to the D2ROffline and D2RModding tool authors over alpha access
  (https://www.dsogaming.com/mods/blizzard-shuts-down-diablo-2-resurrected-offline-d2rmodding-mods/; secondary). No
  action against D2RLoader, its TCP/IP, or Reimagined's ladder was found (search 2026-11-18). PD2 has run a classic D2
  realm for 14 seasons (Seasons page above), a different game and EULA.
- Keep it non-commercial: Reimagined sells or grants cosmetics (`Ladder.md` lines 100-104, `/mtx`); money raises the
  1.B.ii exposure (estimate, not legal advice).

## 4. Providers and cost

### 4.1 Hosts compared (prices read 2026-11-18, excluding VAT and taxes)

| Host | Box | Price | Windows | DDoS | Traffic | Regions |
|---|---|---|---|---|---|---|
| OVHcloud Game (Eco) | SYS-GAME-1, Ryzen 5 3600X | $55.30/mo regular; $47.01/mo first year is a Black Friday promo; setup $55.30 | Installable; license price not listed | Anti-DDoS Game included | 1 Gbit/s, unmetered except APAC (billed) | Worldwide (https://www.ovhcloud.com/en/lp/baremetal/game/) |
| OVHcloud Game (Eco) | Rise-Game-1, Ryzen 5 5600X 6c/12t | $72/mo, setup $72 | same | same | same | same page |
| OVHcloud Game (Eco) | Rise-Game-2, Ryzen 7 5800X | $100/mo, setup $100 | same | same | same | same page |
| OVHcloud Game (US) | GAME-1, Ryzen 7 9800X3D 8c/16t, 64 GB | $357/mo, setup $357 | not listed | included | 1 Gbit/s unmetered | Hillsboro, Vint Hill; Beauharnois, Toronto; Gravelines, Paris, Roubaix, Strasbourg, Frankfurt, Warsaw, London; Sydney, Mumbai, Singapore (https://us.ovhcloud.com/bare-metal/game/) |
| OVHcloud Game (US) | GAME-2, Ryzen 9 9950X3D 16c/32t, 64 GB | $401/mo, setup $401 | not listed | included | same | same |
| Hetzner dedicated | AX41, Ryzen 5 3600 6c, 64 GB | from EUR 59/mo, no setup | Standard 8 cores EUR 27.90/mo | DDoS protection (no detail on page) | Unlimited, 1 Gbit/s | Data centers in Nuremberg, Falkenstein and Helsinki; the page ties no model to a location (https://www.hetzner.com/dedicated-rootserver/matrix-ax/) |
| Hetzner dedicated | AX42, Ryzen 7 PRO 8700GE 8c, 64 GB | EUR 79-99/mo, setup EUR 49 | Standard 8 cores EUR 27.90/mo | same | same | same |
| Hetzner dedicated | AX102, Ryzen 9 7950X3D 16c, 128 GB | EUR 159-259/mo, setup EUR 129 | Standard 16 cores EUR 55.90/mo | same | same | same |
| Hetzner Cloud | Shared or dedicated vCPU | from EUR 5.49 / EUR 36.99 per month | Linux only on the page | n/a | n/a | Germany, Finland, Singapore, Ashburn, Hillsboro (https://www.hetzner.com/cloud/) |
| AWS EC2 on-demand, us-east-1 | c7i.2xlarge, 8 vCPU, 16 GiB | Windows $0.725/h (about $529/mo), Linux $0.357/h (about $261/mo) | included in the Windows rate | Shield Standard (not checked) | 100 GB/mo free, then per GB (**unconfirmed** rate: the page did not render it) | Global (AWS price list `ec2-ondemand-without-sec-sel`, published 2026-11-17T23:50Z; https://aws.amazon.com/ec2/pricing/on-demand/) |
| AWS EC2 on-demand, us-east-1 | t3.medium, 2 vCPU, 4 GiB | Windows $0.060/h, Linux $0.0416/h (about $30/mo) | | | | same |
| Vultr | any | not read: pricing pages return 403 | "an extra license fee applies based on your Compute plan" (https://docs.vultr.com/support/platform/billing/is-a-windows-license-included-in-the-monthly-price) | **unconfirmed** | **unconfirmed** | **unconfirmed** |

Hetzner Windows Server prices are its 2025 list, effective 2025-01-01, monthly, excluding VAT
(https://docs.hetzner.com/robot/general/pricing/windows-2025-pricing). OVH's dedicated Windows license price is not on
any page read; the cost table uses Hetzner's 8-core price ($31) as a stand-in, **unconfirmed** for OVH. Game-server
rental hosts (slot-based hosts for supported games) do not list D2R, so they are out; Azure and Google Cloud were not
priced (expected near AWS; **unconfirmed**).

### 4.2 Monthly cost, three sizes (estimates)

Assumptions (all **estimates** until run 1 measures them): one 6-8 core box carries about 100 concurrent players
(about 40 games at 2.5 players); one spare box per region; the API, database, chat and object storage for saves run
on Linux (Hetzner Cloud); prices from section 4.1 on 2026-11-18; USD.

| Size | Game servers | Lobby/API/DB | Monthly total | One-time setup |
|---|---|---|---|---|
| Small test league, ~50 concurrent, one region | 1 x OVH Rise-Game-1 ($72) + Windows (~$31) = ~$103 | 1-2 cloud servers, EUR 5.49-36.99 each, plus backups: ~$15-45 | **~$120-150** | ~$72 |
| Mid, ~500 concurrent, NA + EU | 6 x ~$103 = ~$620 (5 + 1 spare; EU could be Hetzner AX42 + Windows, ~$115-140 each) | API x2, DB, chat on dedicated-vCPU cloud servers: ~$80-200 | **~$700-850** | ~$430 |
| Large, ~2,000+ concurrent, NA + EU + Asia | 23 x ~$103 = ~$2,370 (20 + 3 spare); APAC traffic is billed extra on OVH, rate **unconfirmed** | API cluster, DB with replica, chat, object storage: ~$300-800 | **~$2,700-3,200** | ~$1,650 |
| Large on AWS on-demand Windows | 23 x c7i.2xlarge ~$529 = ~$12,170, plus egress | ~$500+ | **~$12,700+** | none |

If `d2rds` runs under Wine on Linux, drop about $31 per box (about $190/month mid, $710 large). Bigger boxes
(OVH GAME-2, Hetzner AX102) cost about the same per core; fewer boxes means fewer Windows licenses but larger blast
radius. Domain, email and a CDN for downloads are small and left out.

### 4.3 What to measure before buying

Players per game process, games per process before tick time rises (Reimagined tracks `tickAgeMs` and `maxTickMs`,
`src/lib/server-telemetry.ts` lines 17-19), RAM per process with no player, network per player, and whether a box needs
a GPU. A TCP/IP host in run 1 is the same code path (section 1.1).

## 5. Team and ops effort

Effort is **estimate** in person-weeks for one experienced developer with agents; no source.

| Component | Build or buy | Effort | Notes |
|---|---|---|---|
| Headless server plugin | Build (or license from Reimagined / ask D2RLoader) | 6-12 | Largest unknown: RE of the menu and game-creation path, no-render start, crash recovery |
| Lobby plugin + lobby API | Build | 3-5 | Host/list/join on our boxes, region choice, drain |
| Server-saves plugin + save API | Build | 4-6 | Pull/push/reconcile, `.d2s` + `.d2rl` pairs, versioned object storage for rollback |
| Save review and anti-dupe | Build, grow over time | 4-8 | Start with item ids and legality against our tables; journal later |
| Accounts and login | Buy the provider (Discord OAuth or email), build the account service; issue #7 keeps the account separate from Steam/Battle.net | 2-3 | Contract fixed by launcher issue #7 |
| Signed packages and allowlist | Build on the toolkit's release manifest | 1-2 | ECDSA signing key handling and launcher checks |
| Launcher hosted mode | Build in `resurrected-launcher` | 3-4 | Sign-in, join grant, locked settings, separate savepath |
| Admin panel, bans, telemetry | Build; buy log/metric hosting | 3-5 | Reimagined's admin pages are the checklist (section 2.7) |
| Chat, friends | Later; Discord first | 0-4 | Not needed for a first league |
| Trade site | Later | 3-6 | Website listings only (issue #7) |
| Ops | Buy servers | ongoing | Deploys per league patch, backups, on-call during league start |

Total to a first hosted league without chat and trade: roughly **26-45 person-weeks** (estimate).

### 5.1 Phased plan

1. **Beta run 1 (TCP/IP):** step 1 work items; measure the host; player guide.
2. **Between runs:** launcher step 2; ask D2RLoader's authors and Reimagined about a shared dedicated mode; owner
   decides on the EULA risk; spike a headless start on one Windows box and one Linux/Wine box.
3. **Beta run 2 (short hosted league):** one region, one or two boxes, headless plugin, server saves with nightly
   backups, account sign-in, signed package, minimal lobby, admin bans, telemetry. Characters wiped at the end.
4. **League 1:** two regions, league start/end and migration to a standard realm, save review, website trade
   listings, Discord-based chat first.

## Recommendation (ranked)

1. **Do step 1 now and measure.** Add the `[d2rl] match` lists and the multiplayer checks; they are needed for every
   later step and cost little.
2. **Ask before building the server.** Write to D2RLoader's authors (a generic dedicated mode helps every mod) and to
   Reimagined (license `d2rds`). A yes saves the biggest item (6-12 weeks).
3. **Owner decides the EULA risk** (matchmaking clause, non-commercial use) before money goes into servers.
4. **Start small on OVH Game servers** (DDoS included, NA/EU/APAC, unmetered outside APAC): one box for run 2, about
   $120-150/month all in. Keep the API on Linux cloud servers.
5. **Avoid hyperscalers** for game boxes: about four times the cost.

## Open questions for the owner

1. Is the EULA exposure (1.B.ii, 1.C.viii) acceptable, and must the league stay strictly non-commercial (no cosmetics,
   no donations tied to perks)?
2. Ask D2RLoader's authors for a dedicated mode, ask Reimagined to license `d2rds`, or build our own?
3. Login provider: Discord, email, or both? (Issue #7 keeps the project account separate from Steam and Battle.net
   identity, so those stay out unless that decision is reopened.)
4. Regions for run 2 and league 1 (NA only, NA + EU, or include Asia)?
5. League length and what happens at the end (PD2 moves characters to non-ladder and drops shared-stash items)?
6. Who is on call during league start and patch days?
7. Budget ceiling per month, and whether players fund it.

Unconfirmed items to resolve: TCP/IP and `d2rds` player cap; whether D2R runs headless without a GPU; Wine stability
for a server; per-player CPU, RAM and bandwidth; OVH and Vultr Windows license prices; AWS egress rate;
whether a directly started D2R needs online authentication; whether the engine treats a hosted
game as ladder; Reimagined's end-of-ladder handling and server-side backups; PD2's hosting provider.
