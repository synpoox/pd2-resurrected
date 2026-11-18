# Map stacking (#1326)

D2R 93847, read-only RE, then implemented in `plugin/core-rules/item_rules.cpp` (`MapStackEqual`).

**Hook:** D2Common `Items.cpp` `ItemsStackEqual(Unit* target, Unit* other)` at `0x375960` (1.13c #11102), returns 1 when two items may merge. Entry
`48 89 5C 24 08 48 89 74 24 10 55 57 41 56 48 8B EC 48 83 EC 20` (21 bytes, matches once; the first 16 bytes match twice).
The native check compares class, unique/set row, items.txt stackable, ethereal, quality (only 1-3 pass) and stats 21-24, 159, 160, 194. It ignores every other stat, so
`cartographer_map_level` (564) and `map_force_event` (553, layer 1) were ignored.

**Callers (one hook covers all):** D2Game `0x4f2a80` (drop item onto item, then `0x4c1940` moves stat 70), inventory scans `0x386840` / `0x386e00` (ground pickup auto-stack, vendor buy,
the vendor UI), place-at-cell `0x38d0d0`, the client cursor `0x2c81d0`. Not used by the cube (`cube_stacks.cpp` counts itself) or the stash save.

**Rule:** original says yes and both items are `cmap` (itemtypes 245): yes only if both are quality normal (2) and every stat id 0..649 except 70 has the same `UnitStatEntries` list (layer and value) on both.

**Split and cube leftovers:** the split is Ctrl+left click with the stack held. The source keeps its stats; after `splitStack` core-ui reads the source's non-quantity stats (`UnitStatEntries`) and overwrites them on the new piece with `SetUnitStat`, so the result is the same whether `splitStack` kept them or not (`split: copied <n> stats onto the new map piece` in core-ui.log). Cube leftovers are remade by `createItem`
in `cube_stacks.cpp`: for a `cmap` source its stats 564 and 553 are passed as properties 374 (`map-level`) and 383 (`map-force-event-l`, param = layer); a map with any other non-70 stat is refused as a partial take.

## #1340: 1 map dropped onto a 2-stack does not merge (diagnosis in progress)

Game check, slot 2, diagnostic build (`MapStackEqual` now logs each pair's first 3 verdicts with the caller; native refusals of two maps are logged too):
loaded + loaded merges (server, caller image +0x4f2b3a, `merge (equal)`), and a loaded 2-stack + a loaded map made a 3-stack. A map created this session
(`cheat spawn m02`) dropped onto a loaded one: the client's guess (caller +0x2c8391) says merge but the server's call to the native check returns 0, so
`MapStackEqual` never reaches our stat compare. Both items' `UnitStatEntries` held only stats 70 and 564. So the cause is a native-check input that
differs between a fresh and a loaded item, not our stat rule. Not yet found which (item data bytes differ is the next thing to log).
