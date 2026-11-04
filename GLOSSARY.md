# PD2 Resurrected

A port of Project Diablo II (PD2) onto Diablo II: Resurrected (D2R) as a mod.

## Language

**Game mode**:
The rule set a D2R character is created in: Classic, Lord of Destruction (LoD) or Reign of the Warlock (RotW). The port targets RotW only.
_Avoid_: expansion, version, ladder

**Table bank**:
One of D2R's two folders of data tables; each game mode reads only its own bank.
_Avoid_: excel folder, data set

**D2R-only content**:
Anything D2R has that PD2 does not (items, runewords, areas, features), other than the Warlock. On unless turned off by approval one by one.
_Avoid_: new content, vanilla extras

**Warlock**:
D2R's RotW class, the one class in the port not held to the PD2 baseline: D2R's Warlock on build 93847, changed only where a skill falls short of PD2's style, keeping its three trees.

**Endgame gear**:
The gear a Warlock balance test assumes: full skillers, Torch, Anni, GG corruptions, high-end runewords and chase uniques; no unique that drops from only one boss.
_Avoid_: BiS, budget gear

**Endgame build**:
One Warlock tree's main build, played on endgame gear.
_Avoid_: spec, main build

**Reference build**:
The PD2 build, on endgame gear, that an endgame build is measured against.
_Avoid_: benchmark, comparison build

**Unported list**:
The record of every deviation, its status and why.
_Avoid_: cut list, TODO list

**PD2 baseline**:
The one frozen PD2 build the port is measured against. Moving to a newer PD2 is a deliberate re-baseline.
_Avoid_: latest PD2, current season

**Exact**:
A parity tier: the player observes the same values as in the PD2 baseline (stats, rolls, rates, formulas, names). Identifiers and file layout may differ.
_Avoid_: 1:1, identical, byte-for-byte

**Function-equivalent**:
A parity tier: the player has the same capability as in PD2, in D2R's look and controls.

**Deviation**:
Any observable difference from the PD2 baseline. Every deviation is a row on the unported list, marked approximated, cut or not yet.
_Avoid_: bug, known issue

**Approximated**:
A deviation where the port reproduces a PD2 rule only roughly and states how it differs.

**Cut**:
A deviation where the port does not reproduce a PD2 rule at all.

**Not yet**:
A deviation the port intends to close but has not.

**Work item**:
A buildable slice of the port sized to about one agent session; the unit the roadmap orders and a build session picks up.
_Avoid_: task, ticket (those are map decisions)

**Engine rule**:
A PD2 behaviour that is not a table value and needs plugin code, such as a skill function, hit function, cube op, item rule or server patch. A skill's server and client halves are one engine rule.
_Avoid_: patch, function number (a function number is where one engine rule plugs in)

**Router slot**:
srvdofunc 190, one D2R table slot holding a core-rules function that picks a PD2 do function by skill id; it carries the do functions that have no free slot of their own. Not part of D2RLoader.
_Avoid_: overflow table, dispatcher

**Complete class**:
A PD2 class whose player skills reach no unbuilt engine rule, including the rules for how its skills interrupt and chain; each is built or an approximated deviation. Class-themed item rules do not count.
_Avoid_: finished class, ported class

**Companion plugin**:
A D2RLoader plugin built outside this repo and bundled with the port that stands in for a PD2 feature; what it lacks is a deviation.
_Avoid_: dependency, add-on, external mod

**Milestone**:
A playable point in the roadmap made of the work items it needs; the port has M1 (a new character in Act 1), M2 (companions, corruption, Amazon and Barbarian complete), the pre-alpha (every PD2 class complete), the alpha (PD2 complete for single-player), the beta and the launch.
_Avoid_: phase, release

**Beta**:
The milestone after the alpha, in two runs: run 1 is TCP/IP co-op, run 2 is multiplayer with a short beta league. It also holds the Warlock's three new skills and, last and at low priority, the guild halls; its other contents are not yet decided.
_Avoid_: PD2's `Beta` build (a different, older PD2 build in the install)

**Launch**:
The public release after the beta; league 1 starts with it.
_Avoid_: release, 1.0

**League**:
A fresh-start play period in multiplayer: the short beta league, then league 1 from the launch.
_Avoid_: season (PD2's word), ladder

**Corruption**:
PD2's one-time cube action that changes an item, for better or worse, and marks it so it cannot be done again.
_Avoid_: corruption roll, corrupt recipe

**Desecration**:
PD2's second Corruption, for amulets only and unlocked by killing Lucion. It adds one more modifier on success, and on failure turns the amulet into a rare.
_Avoid_: desecrated zone, Desecrate (the Necromancer skill)

**Terror Zone**:
D2R's rotating zone that grows harder and gives better rewards; it stands in for PD2's corrupted zones.
_Avoid_: corrupted zone, desecrated zone

**PD2-only area**:
A level PD2 has and D2R does not: map areas, uber boss areas, event areas, PvP areas and guild halls.
_Avoid_: map (a map item or a wayfinder map), zone, new area

**Uber boss**:
One of PD2's pinnacle fights in the port: Uber Tristram, Diablo Clone, Rathma or Lucion. Each replaces D2R's fight of the same name, if D2R has one.
_Avoid_: pinnacle boss, D2R's world-event Diablo Clone or Pandemonium Event (both off)

**Uber key**:
The item that opens an uber boss's area at the Blood Fountain in Harrogath. It is not a map item.
_Avoid_: uber map, entry item

**Uber tier**:
The tier on an uber key, 0, 1 or 2, cycled in the cube; higher tiers make the fight harder and its drops better.
_Avoid_: map tier

**Map item**:
A PD2 item that opens a map area when used in Harrogath: a tiered map, a dungeon or a unique map. Uber keys and PvP arenas are not map items.
_Avoid_: map

**Map tier**:
The tier a player sees on a tiered map: 1, 2 or 3. It comes from the item's type, not from its item code. Dungeons and unique maps have no tier.
_Avoid_: T4, T5, tier from the code

**Map area**:
A PD2-only area that a map item opens.
_Avoid_: map, map level

**Map stat**:
A modifier on a map item that changes its map area, the area's monsters or the players in it while it is open.
_Avoid_: map mod, map affix (an affix is how a map stat rolls)

**Map event**:
A random happening inside a map area, such as a boss, a portal, a shop or a timed lair, rolled when the area opens.
_Avoid_: map-event notification (cut; the events are not)

**Invader**:
A monster that plays a PD2 class, coming out of portals in the Corrupted Heroes map event.
_Avoid_: corrupted hero, PK

**HD preset**:
The HD scene D2R draws for one DS1 file, found by the DS1's path; a DS1 without one plays on flat ground with invisible walls.
_Avoid_: HD map, tile art

**PvP**:
Player versus player: players fighting each other, in PD2's PvP and battle royale areas. Not the same as multiplayer. The port has no PvP.
_Avoid_: multiplayer, dueling

**Multiplayer**:
Players connecting to an online server the port hosts, with its economy and leagues.
_Avoid_: online, hosted servers (say multiplayer), TCP/IP co-op

**TCP/IP co-op**:
Players joining each other's games directly over TCP/IP, with no hosted server, economy or league.
_Avoid_: multiplayer, LAN
