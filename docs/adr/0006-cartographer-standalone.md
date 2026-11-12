# Cartographer is standalone, one map per game, and simple to author

Cartographer is a D2RLoader plugin for procedurally laid-out maps (plan: `docs/plans/cartographer-plugin.md` on branch `docs/map-kit-plan`, epic [#935](https://github.com/synpoox/pd2-resurrected/issues/935)). It is a separate product from `core-maps`, and any D2R mod can drop it in.

- **Fully standalone.** It carries no PD2 content and has no content interface for other plugins. It does not link `pd2_engine.h`. This project's content does not move onto it; `core-maps` keeps its own map code.
- **Never loaded together with `core-maps`.** Both hook item spell 16 and the same spawn functions. A mod ships one or the other.
- **One map per game.** A map item used in Harrogath opens a red portal into the map. Once a map is open, any further map is refused for the rest of the game.
- **Simplicity is a release requirement.** The author side follows the rules in plan section 3.0: one required file (`maps.txt`), plain words only, blank means default, no edits to vanilla tables (the one exception is the advanced option C, where an expert adds `LvlPrest` rows for their own room pieces), plain-English errors, and a familiar tab-separated format. Every new column needs a default and a plain-English check. The phase 8 novice test gates the release: a fresh agent with no project context, given only the release zip and the README, adds a new map to a clean mod and opens it in game without reading any code.

## Considered Options

- Move `core-maps` onto Cartographer: one map system, but it ties a general plugin to PD2 content and its scripted events.
- Load both together: both hook item spell 16 and the same spawn functions.
- A content interface for other plugins: scripted events (puzzles, stages) would then be possible, but the plugin would stop being drop-in.

## Consequences

- Two map systems live in the repo. A fix to a shared idea is made twice.
- Events are limited to five generic kinds driven by data. Scripted events stay in `core-maps`.
- Release is blocked until the novice test passes, even if every feature works.
