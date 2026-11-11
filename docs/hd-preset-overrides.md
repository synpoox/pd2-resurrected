# HD preset overrides

A per-map layer on top of the generated HD presets (P3, [#839](https://github.com/synpoox/pd2-resurrected/issues/839),
part of [#707](https://github.com/synpoox/pd2-resurrected/issues/707); plan: `docs/research/hd-parity-strategy.md`
section 4 on `research/hd-parity-strategy`). The generator (`tools/pd2_presets.py`) makes every PD2-only map's preset
from its DS1. Where the result is wrong in a way no general rule should fix (a floating prefab, a rock on open floor, a
piece in the wrong place), an override edits that one map after generation, so the fix is data, reviewed in a diff, and
the generator stays rule-only.

Code: `tools/pd2_overrides.py`. Tests: `tools/tests/test_overrides.py`.

## Files

| File | Holds |
|---|---|
| `merge/presets-overrides.tsv` | the `pd2_merge` anchor, no lines (`RULES["presets-overrides.tsv"]`). It sorts after `presets-hd.tsv`: the merge driver runs rule files in name order, and the rule edits `presets-hd`'s output |
| `merge/presets-overrides.d/<family>/<ds1 stem>.json` | the ops for one generated preset (`generated/data/hd/env/preset/**/<ds1 stem>.json`), in a folder per map family (`jungle`, `tomb`, `cistern`, ...) |

A file's `ds1` is the preset's stem, or its path under `hd/env/preset/` (`expansion/map/guild4`) when two presets share
a stem (the merge fails on an ambiguous stem); its last part must equal the file name (case-insensitive). It must name a
generated preset: a file for a preset that does not exist fails the merge (a stale file). Add a family by making its
folder; no list changes.

```json
{"ds1": "jungle1",
 "ops": [
   {"op": "ground", "area": [30, 160, 50, 190], "model": "*/act3_jungle_debris/*", "why": "sticks float 6-7 units"},
   {"op": "remove", "at": [212, 154], "model": "*/act3_jungle_rocks/*", "expect": 1, "why": "rock on open floor"}
 ]}
```

`why` (any extra key) is documentation; the rule ignores it. Ops run in file order, each on the result of the last.

## Selectors

Every op but `add` and `entity` needs at least one; all given must match.

| Key | Selects |
|---|---|
| `at` `[x, y]` | entities on one DS1 tile (tile = top-level position // 10 HD units) |
| `area` `[x0, y0, x1, y1]` | entities on tiles in that rectangle, inclusive |
| `model` | fnmatch glob (case-insensitive) on the entity's model or prefab path |
| `name` | fnmatch glob on the entity name |
| `pos` `[x, z]` | one entity by its generated position in HD units (tolerance 0.01): the stable identity an editor's output is diffed by |
| `expect` | how many entities the op must match (default: at least one) |

The ground `terrain` entity is never selected. Never select by `id` or array index: a re-serialised preset renumbers them.

## Ops

| Op | Does |
|---|---|
| `remove` | drops the matched entities |
| `ground` | sets their y to the ground plane under them (`pd2_coverage.plane_ground`): the floating fix |
| `move` | `by` `[dx, dy, dz]` HD units, or `to` `[x, y]` tile (the entity keeps its offset in the tile, y re-grounded) |
| `swap` | the matched entities' model (or prefab) path becomes `to`; with `from` (a stock preset path, `data/hd/...`) its dependencies are merged in, else `to` is added to the models |
| `add` | needs `at`, `from` and `pick`: copies the first entity of stock preset `from` whose model matches `pick` to the centre of tile `at` (plus `offset` `[dx, dy, dz]`), y on the ground plane, a fresh id, named `pd2r_override:...`; that preset's dependencies are merged in |
| `entity` | an inline entity, kept as is with a fresh id: what `diff` writes for an entity an editor added. Its optional `dependencies` (`{kind: [{"path": ...}]}`) are merged in, and its model is added to the models if missing |

A malformed op (unknown op, no selector, a `move` with both or neither of `by`/`to`, a missing key) fails the merge.

## Lint: a stale op fails

An op that matches nothing, or not its `expect`, is **stale**: the generator moved or replaced what it aimed at. It
changes nothing, the merge prints `STALE ...`, and the rule writes the list to `generated/presets-overrides-report.tsv` (a skipped rule writes an empty one).
`py -3 tools/pd2_overrides.py lint` reads that report and exits 1 on any stale op (and when there is no report). The gate
workflow runs it after the merge, so a PR that makes an override stale (or adds one that is) fails. Re-aim the op or drop
it.

## Tooling

```
py -3 tools/pd2_overrides.py lint
py -3 tools/pd2_overrides.py diff <generated.json> <edited.json> [--ds1 stem]
py -3 tools/pd2_overrides.py roundtrip <generated.json> <edited.json>
```

`diff` writes the ops that turn a generated preset into an edited copy (any editor's output): entities only in the
generated one become `remove` ops (by tile, model, `pos`, `expect` 1), a removed/added pair of one model within two tiles
becomes a `move`, the rest are inline `entity` adds. `roundtrip` replays `diff`'s ops on the generated preset and checks
the result equals the edited one (exit 1 if not).

Workflow for an editor round trip: take `generated/data/hd/env/preset/.../<ds1>.json`, edit a copy, `diff` it, and commit
the output as `merge/presets-overrides.d/<family>/<ds1>.json`. Prefer a selector-based op over inline `entity` when the
fix is a rule of the map (all ferns on this tile block float) rather than a one-off.

## Proving an override

Overrides change what players see, so an override needs a game check: pin the variant with `tools/pd2_force_ds1.py`,
warp with `docs/agents/warp-locations.md`, and shoot the spot before and after (`docs/agents/game-check.md`). The first
one is `jungle/jungle1.json`: the floating debris, cypress (with its 3 rock piles) and ferns grounded, and a
full-size `rock_pile04` (scale 1, from stock `boge.json`) added at the cypress; nothing is removed. An `add` keeps the
picked entity's scale, so pick a piece that reads at that scale (the `rock_pile03` copy, scale 0.63, was a few pebbles).
