# Multiplayer: TCP/IP, hosted servers, and how Reimagined does it

Research for the beta's multiplayer runs ([roadmap.md](../../roadmap.md): run 1 over TCP/IP, run 2 on hosted servers).
Sources read 2026-11-18. Web pages are cited by URL; repo files by path. Nothing here was run in a game.
"Unconfirmed" marks what no primary source states.

## 1. TCP/IP in D2R 3.x

**Blizzard's game has none.** Blizzard dropped TCP/IP before launch: its beta FAQ said TCP/IP "will not be available in
the upcoming Beta or the final game" (August 2021, reported by
https://www.gameinformer.com/2021/08/11/diablo-2-resurrected-open-beta-details-revealed-quietly-removes-peer-to-peer-multiplayer),
and a community-team post in September 2021 gave the reason as security
(https://www.diablofans.com/news/49602-diablo-ii-resurrected-console-lobbies-tcp-ip, quoting the Blizzard forum post,
whose own URL no longer loads). No later patch put it back (unconfirmed beyond the toolkit's own review,
`d2rAI-toolkit/docs/distribution.md`, *Play modes and policy*).

**D2RLoader adds it back.** Its TCP/IP page (https://d2rloader.net/tcp-ip.html) and changelog
(https://d2rloader.net/changelog.html) say:

- Turn on *Show TCP/IP Button* (Settings, General), pick an **offline character**, then *Multiplayer* beside *Play*
  (https://d2rloader.net/docs.html). Battle.net characters are not used.
- Since 1.2.0-beta (2026-08-30) a Multiplayer screen with *Browse Games* and *Host Game*. Three host modes: **Public**
  (listed in the browser, filter by mod, difficulty, Terrorized), **Unlisted/Direct** (join by Game ID or address),
  **LAN Only** (the old behaviour, join by Direct Join). Listed games stay registered by heartbeats, so the browser is a
  D2RLoader-run listing service; the game itself still runs on the host's PC.
- **Port TCP 4000** by default, changeable with `game_port`. The loader tries automatic router setup (with
  NAT-PMP since 1.2.1) and shows a *Hosting status* that says whether outside players can reach the game. Some routers
  still need a manual port forward; the page gives no steps. Hosting uses IPv4 only (1.2.1-beta).
- **What must match** (checked by the host before a client enters): loader version, the active mod, Shared plugins,
  the `ItemStatCost` schema (1.1.0), memory patches, map layout, movement, collision and animation timing data
  (1.2.1), and the save format (1.3.0). The check "does not make different mods compatible". Clients older than
  1.2.1-beta cannot join a 1.2.1 host.
- With mods: the mod is the one started with `-mod <name> -txt` (Reimagined's launcher does exactly this, section 3).
  The TCP/IP button was hidden for mods that ship a `d2rloader.ini` before alpha-0.4.0's `force_tcpip` option.

**Plugins on each client** (toolkit SDK, `d2rAI-toolkit/third_party/PluginSDK/README.md`, lines 186-203 and 99-120):

- Every plugin sets one role: `Client` (UI, input), `Server` (gameplay rules and saves) or `Shared` (both). "The host
  performs both roles."
- `Shared` plugins must match host and client by id, version, plugin ABI, scope and compatibility flags. Client-only
  plugins are not checked and not recorded in the save.
- A Shared plugin can list up to 64 config paths under `[d2rl] match`; those values must be equal on both sides.
- Only the local game or the TCP/IP host may change items; a client sends a plugin message (<= 224 bytes) to the host.
  A TCP/IP client has no game thread (`d2rAI-toolkit/knowledge/d2rloader-capabilities.md`, *Multiplayer implications*).
- The plugin network service does not exist on Battle.net (same file).

**Saves.** TCP/IP uses the player's own local offline characters (https://d2rloader.net/tcp-ip.html). Server and Shared
plugins are written into the character's plugin history; the loader's extended save format is checked in multiplayer
since 1.3.0 (`d2rAI-toolkit/knowledge/saves.md`, *Loader updates and extended saves*). Saves are on each player's disk,
so nothing stops an edited character joining (no source claims otherwise).

**Limits and VPNs.**

- Player count per TCP/IP game: **unconfirmed**. No D2RLoader page states it (the toolkit lists it as open,
  `knowledge/d2rloader-capabilities.md`, *Open*).
- VPN tools are not mentioned by D2RLoader. Reimagined's wiki keeps a *legacy* Radmin VPN page for players whose
  firewall or router blocks the direct route
  (https://github.com/D2R-Reimagined/wiki-content/blob/main/Installation/VPN.md). ZeroTier or Tailscale would work the
  same way (a virtual LAN plus LAN Only or Direct Join) but no source tested them: **unconfirmed**.
- Windows only for the loader (`d2rAI-toolkit/docs/distribution.md`); consoles: no route.

## 2. Hosted servers

**Battle.net: not possible.** Blizzard's EULA forbids connections to "any unauthorized server that emulates" its
platform (section 1.C.ix, https://www.blizzard.com/en-us/legal/fba4d00f-c7e4-4883-b8b9-1b4500a402ea/blizzard-end-user-license-agreement).
No Blizzard source gives mod authors realms, private ladders or modded Battle.net play
(`d2rAI-toolkit/docs/distribution.md`). Mods are offline only on Blizzard's side.

**Server emulators for D2R: none found.** No public D2R Battle.net emulator turned up. In 2021 Blizzard sent
cease-and-desist letters to D2R modders over tools for the technical alpha, "citing security concerns"
(https://www.wowhead.com/tbc/es/news/blizzard-issues-cease-and-desist-letters-to-modders-citing-security-concerns-322566;
whether one was a server emulator: secondary sources only, **unconfirmed**). The classic bnetd case went Blizzard's way
in 2004 (https://gamedeveloper.com/game-platforms/blizzard-wins-latest-round-of-bnetd-court-battle; a news report,
not the ruling). Not legal advice.

**What "hosted" can mean without Blizzard:**

1. **Player-hosted TCP/IP** (section 1). The host's game process runs the game server and its own client. All
   server-side rules (spawns, drops, skills, cube, shrines) run on the host's PC with the host's plugins; clients only
   see what D2R syncs (units, stats, states, items) (`d2rAI-toolkit/knowledge/d2rloader-capabilities.md`). The host's
   saves and the clients' saves are each on their own PC, so the host is authoritative for the game but not for anyone's
   character.
2. **A headless dedicated server on D2RLoader.** Reimagined runs this today (section 3): the same game plus a plugin
   that turns it into a server with no player session. D2RLoader's own docs and changelog do not mention a dedicated
   mode, so it is the mod's own plugin on top of the loader's TCP/IP, not a loader feature (**unconfirmed** whether the
   plugin is public; its repo `d2rl-plugins` is not visible).
3. **Server-held characters.** A plugin that pulls and pushes the save files to a web API, so the server, not the
   player's disk, owns ladder characters (Reimagined's `server-saves`, section 3). This is what makes a league
   enforceable.

**Tie to our plugin split** ([ADR 0004](../../adr/0004-plugins-split-by-multiplayer-scope.md)):

- `core-rules` and `core-events` are `Shared` (`plugin/core-rules/plugin.cpp` line 54, `plugin/core-events/plugin.cpp`
  line 50); `core-ui` is `Client` (`plugin/core-ui/plugin.cpp` line 57). That is the split D2RLoader rewards: gameplay
  runs on the host or dedicated server, and every peer must run the same `core-rules`/`core-events` build; `core-ui`
  can differ. Companions are pinned per release so they match by construction (ADR 0004, 2026-11-03 amendment).
- PD2's own server rules decoded on `origin/research/w37-server-rules` (`docs/research/w37-server-rules.md`) are of this
  kind: shared shrines that also buff every other player within about 21 subtiles, resist shrines that raise max
  resist, a "Perfect Gem Shrine activated" chat line sent to every client. They only show their multiplayer side with
  two or more players, so a single-player check never proves them (as the toolkit notes: a passing local check does not
  prove a change syncs).
- What a client must do through the host (item changes, anything on a game thread) needs a plugin message, since a
  TCP/IP client has no game thread.

## 3. How D2R Reimagined runs multiplayer

From their public repos, wiki and launcher (org https://github.com/D2R-Reimagined). Nothing from `reimagined-secrets`
was read.

**Three launch modes.** The launcher offers per install "Offline, Online (D2RLoader TCP/IP), and active Ladder"
(https://github.com/D2R-Reimagined/reimagined-launcher, README *Features*; repo pushed 2026-11-15).

- **Online** = D2RLoader TCP/IP. The launcher starts `D2RLoader.exe` directly with `-mod Reimagined -txt`, not through
  Battle.net; players host or join with D2RLoader's own Multiplayer screen (README *D2RLoader support*). It sets
  `default_mod` and `show_tcpip_button = true` in `d2rloader/config/d2rloader.toml` for Online and Ladder
  (`ReimaginedLauncher/Utilities/D2RLoaderLaunchSettings.cs`, `UpdateDefaultMod`). If D2RLoader is missing the launcher
  downloads and installs it
  (https://github.com/D2R-Reimagined/wiki-content/blob/main/Installs.md, *Reimagined and D2RLoader*, step 2).
- **What players must match**, in their words: D2R, D2RLoader, D2RLoader plugins/patches, the Reimagined
  compatibility build, and any launcher tweaks (Installs.md, *Before you begin*). Hosting is Public or Unlisted with a
  Game ID and password (Installs.md, step 4.5). Radmin VPN is the legacy fallback (section 1).
- **Ladder** = dedicated servers run by the Reimagined team ("Play on dedicated servers with trade",
  https://github.com/D2R-Reimagined/wiki-content/blob/main/Ladder.md). Needs a signed-in website account. Parts:
  - **Signed ladder package.** The launcher verifies a signed package (ECDSA, SHA-256 per archive), loads only the
    ladder's approved extensions (others moved to `ladder-disabled/`), and restores clean mod files (README
    *Order of operations*, *Ladder download recovery*). Locked settings are reset per launch (Ladder.md, *Ladder Locked
    Settings*), and debug/cheat tools are off (`.DISABLE_DEBUG` in the packed MPQ,
    `ReimaginedLauncher/Utilities/LocalD2rdsRigService.cs`).
  - **Server-held characters.** A `server-saves` plugin pulls the ladder's character manifest from
    `api.d2r-reimagined.com` before the character screen draws, pushes each save during play, and with no API reachable
    leaves the character list empty: "no server, no characters". Ladder characters use their own `savepath`
    (`ReimaginedThree-<ladder>-<id>`), with their own shared stash (README *Ladder*).
  - **Dedicated servers (`d2rds`).** A plugin `d2rl-d2rds.dll` turns the game started with `-d2rds` into "a headless
    server"; a `d2rl-lobby-bridge.dll` plugin lists and hosts games through the API; game port 4000
    (`LocalD2rdsRigService.cs`, header comment and constants). The website's admin code registers boxes by IPv4
    address and region, up to 8 `d2rds` processes per box and 32 games per server by default (cap 128)
    (https://github.com/D2R-Reimagined/d2r-reimagined-website/blob/main/src/lib/server-registry.ts). The launcher pings
    regions and picks the two fastest (`ReimaginedLauncher/Utilities/LobbyRegionSelection.cs`). Both processes must
    "hash to the same environment (D2RLoader refuses a join otherwise)" (`LocalD2rdsRigService.cs`).
  - **Online services on top:** global chat, friends, DMs, a player count, bug reports, cosmetics, all gated on being
    signed in through the launcher (Ladder.md, *Chat Commands*).
- **Keeping clients in sync:** launcher auto-update (README *Features*), one-click mod install from Nexus or their site
  (Installs.md), a schedule and policy API (`/ladders/schedule`, `/ladders/active`) whose server-issued version decides
  when cached policy is stale (README *API configuration*), and for ladder the signed package plus allowlist.
- **Older side builds.** `d2r-reimagined-LAN` and `d2r-reimagined-online` are unofficial compatibility builds for
  an older D2RLoader alpha (its README says 3.0-3.2) and for "D2RLAN", a tool on an older D2R client without the Warlock
  (READMEs of https://github.com/D2R-Reimagined/d2r-reimagined-LAN and
  https://github.com/D2R-Reimagined/d2r-reimagined-online). D2RLAN itself: no primary page found, **unconfirmed**.
  Installs.md now leads with the launcher's Online mode; whether these builds are still used: **unconfirmed**.
- **Support channel:** their Discord, linked from Installs.md.
- **ToS:** none of their pages addresses Blizzard's EULA for the ladder servers. Whether Blizzard has authorized it:
  **unconfirmed** (no source either way).

## What this means for pd2-resurrected

Ranked by what we can ship soonest with the least of our own code.

1. **D2RLoader TCP/IP, player-hosted (beta run 1).** Works today. Needs from us: keep every gameplay hook in the
   `Shared` plugins and bump their version every release (already the rule); list gameplay config in each Shared
   plugin's `[d2rl] match`; send client-side item work to the host by plugin message; a two-player game check for
   rules with a multiplayer side (W37's shared shrines first). Docs: a player page "play with friends" (offline
   character, Multiplayer screen, port 4000, what must match, back up saves). No launcher needed if the release 7z
   pins one loader version and every companion.
2. **Same, plus a small launcher or install check.** Reimagined's experience shows the real pain is version drift. A
   launcher (or `install.ps1` step) that fetches our release, checks the loader version and turns on the TCP/IP
   button removes most "cannot join" reports. Needs: a release manifest with hashes; no server.
3. **Hosted league (beta run 2).** Needs, as Reimagined built it: a headless-server plugin on D2RLoader, a lobby
   plugin and listing service, a server-saves plugin and web API owning characters, signed release packages with a
   plugin allowlist, hosting boxes, accounts, and moderation. That is a separate project of its own; the EULA clause
   on unauthorized servers is a risk the owner must weigh first. Before building, ask whether D2RLoader's authors will
   offer a dedicated mode or the `d2rds` approach as a shared component.
4. **Battle.net.** Not an option.

Open questions: TCP/IP player cap; whether `d2rds` or any headless mode is public; whether Blizzard has said anything
about D2RLoader multiplayer or community ladders.
