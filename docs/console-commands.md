# Console commands

Every console command our plugins register (`RegisterConsoleCommand`), built from the code. Type them in the game
console, or through `game_ctl.py --slot <N> console "<cmd>"` (recipe: `docs/agents/game-check.md`). Each plugin
registers one command named after itself; `core-events` takes sub-commands, the others only report status.
A test fails when a plugin command or a `core-events event` word is missing from this page
(`tools/tests/test_console_commands_doc.py`).

Not here: Cartographer's commands (the map plugin, built in d2r-tools: see its docs there) and D2RLoader's own
`cheat ...` commands (toolkit `knowledge/cheat-commands.md`).

Log lines go to the plugin's log (`core-maps.log` for core-events, whose plugin id is `core-maps`).

## core-rules

`core-rules` : writes the plugin version, game image, mod and file, the off-file words, every part's state and the
counters (skill functions, items, merc, cube, cube stacks, stash stacks, item prices, almanac, shrines, loot, sockets)
to the console and the log. No arguments, no precondition. Example: `core-rules`.

`core-rules armageddon` : dev test aid (#1843): swaps the first Phase Blade (`7cr`) in the local player's inventory for
the Armageddon Blade, the unique the imbue makes on a hit (base `7cr2`, the one uniqueitems row on it, item level 87),
through one SDK item transaction on the game thread, with no roll. Single player / host only. Precondition: in a game,
a Phase Blade in the inventory (not the cursor). Answers `core-rules: queued, see the log` (or `core-rules: <reason>`).
Log line: `armageddon: console: Phase Blade (class <n>, bank row <n>) -> 2H Phase Blade row <n>: result 0, quality 7,
unique row <n>, item level 87`, or `armageddon: console: <reason>`. Example: `core-rules armageddon`.

## core-ui

`core-ui` : the same for the client plugin: version, image, mod, file, off-file words, parts, and the counters (stack
split, item quantity, level palette, inventory lock, stash affinity, stash tabs, monsters remaining). No arguments.
Example: `core-ui`.

`core-ui stash personal|materials|maps|bossing|<n>` : opens the stash (if closed) and selects a tab, so a game check
needs no clicks. `personal` and `materials` are the stock tabs, `maps` and `bossing` ours, `<n>` is shared page `n`
(1-20; 16 is the Maps page, 20 the Bossing page). Dev only: it opens the stash anywhere, not only in town, so use it in town for checks. Precondition: in a game; fails with a usage line if the stash tab signatures were not found. Log line:
`stash tabs: console opened <Personal|Materials|Maps|Bossing|Shared> (page <n>)`, or `stash tabs: console open failed (<result>)`. Example: `core-ui stash maps`.

## core-events

`core-events` (no arguments) : version, image, mod, file, off-file words, every part and its counters, and the entity
pool state; writes `pool now ...` and each counter line to the log. An unknown word does the same.

The test aids below run on the game thread through the SDK and act on the first game found (the local player), so they
are single player / host only. They write `test aid: ...` lines to the log and answer `core-events: queued, see the log`
(or `core-events: <reason>` on failure).

`core-events kill <class>` | `core-events kill unit <id>` : kills the nearest living monster of that merged monstats
class (or that unit id) in the player's level through the normal death path: death listeners run, the treasure class
rolls, the player gets no credit. Skips a player's merc, golem and summons. Log: `test aid: kill: class <c> unit <u>
killed, level <l> (death path, drops roll)`; or `test aid: kill: no living monster class <c> in the player's level ...`;
or `test aid: kill refused: every match in the player's level is a player's pet ...`. A bad argument prints
`usage: kill <class> | kill unit <id>`. Example: `core-events kill 800` (with `cheat god 1` on, so a boss does not die first).

`core-events life <percent 0-100> unit <id>` : sets that monster's life (in the player's level) to the percent of its
maximum (at least 256). Log: `test aid: life: class <c> unit <u> set to <p>% (<life> of <max>)`. Same refusals as
`kill`. Example: `core-events life 10 unit 1234`.

`core-events life <percent 0-100> <class>` : the older ported-boss form. Queues the percent for the next think of a
ported boss of that class (a request unused after 10 seconds is dropped). Console: `core-events: the next think of a ported
boss of class <c> sets its life to <p>%`; log: `ai hooks: test aid: class <c> unit <u> life set to <p>% (...)` or
`ai hooks: test aid: the request for class <c> expired unused`. Example: `core-events life 5 1100`.

`core-events corrupt` : corrupts the item on the cursor, else the inventory item at cell (0,0), with the normal
corruption roll (no cube). Needs an item there that is not already corrupted. Log: `test aid: corrupt: item <class> corrupted
(the normal roll)`, `... NOT corrupted (already corrupted, or no row matches)`, or `test aid: corrupt: no item on the
cursor and none at inventory (0,0)`. Example: `core-events corrupt`.

`core-events desecrate` : the altar's desecration roll on the cursor item, else the inventory item at (0,0); it sets
Lucion's dead stage itself. Log: `test aid: desecrate: item <class> desecrated`, `roll failed (no brick here)`, or
`refused, see the line above`; no item: `test aid: desecrate: no item on the cursor and none at inventory (0,0)`.
Example: `core-events desecrate`.

`core-events event <word>` : one shot for the next map rolled; it skips the quality roll. Words:
`wanderer`, `mendeln`, `treasure`, `horazon`, `gheed`, `altar`, `spire`, `tomb`, `invaders`, and `off` (clears the
forced event). Console: `core-events: the next map rolls event <word>`; a bad word prints
`core-events: usage: core-events event wanderer|mendeln|treasure|horazon|gheed|altar|spire|tomb|invaders|off`. Log when
the map rolls: `map events: level <l> (quality <q>, console event) rolled event <n> (<k> rolls)`. `tomb` rolls no event
when there is no Bitter Depths binding. Open a map item (type `cmap`) after it. Example: `core-events event invaders`.
