# pd2_merge speed (#803)

Research for #803: why a full `tools/pd2_merge.py` run takes about 25 minutes, and how to make it much faster. Measured
on 2026-11-10 at main `f7f2391`, on the Ryzen 7 9800X3D (8C/16T, 64 GB).

## The short answer

- **One rule is the whole merge.** `presets-hd.tsv` (`tools/presets/`, the HD preset generator) takes 1334 s of a
  1373 s run (97%). The other 33 rules together take 36 s.
- **It runs on one core.** It is CPU-bound pure Python: one core at 99% for 22 minutes, 6.0 GB working set, almost no
  disk IO. During the run the machine as a whole sat at 10-27% CPU.
- **The cache key is not the main problem, but it is coarse in another way.** Only the presets rule's key covers
  `tools/presets/`, and no later rule reads what it writes. A change to `tools/presets/` therefore reruns that one rule,
  not every rule, but that rule is the whole 22 minutes. Its key also covers 12 other tool modules. `pd2_ids.py` alone
  changed in 53 of the 385 main commits since 2026-10-27, and each of those reruns every rule, the presets rule
  included. In all, 109 of those 385 commits (28%) invalidate the presets rule.
- **Cheap hot-spot fixes give 2.6x on the presets rule with byte-identical output** (prototyped and measured on a full
  run: 1334 s to 515 s). Running DS1 builds in parallel should give a further ~4x (estimated). Together they should
  bring a full merge from ~23 min to about 3-4 min.

## Method

- `pd2_merge.merge()` ran with each rule wrapped in a timer (wall time, CPU time), with the rule cache pointed at an
  empty scratch folder (`PD2R_CACHE` set to it, so every rule ran with the cache's file tracing, as a real miss runs it).
  The shared `C:\pd2r-cache` was not written. A PowerShell sampler logged CPU per core, working set and IO every 10 s.
- `presets.apply` then ran by itself on scratch copies of the merged `generated/data` (`global/`, `local/`), under
  cProfile and without it. It ran on all 135 DS1, and on a 7-DS1 subset (jungle1, kurastopenwide, urehcity,
  monastery, palace1, throne1, tristmap: the slow outdoor tree maps plus indoor ones). In the subset runs the setup
  still runs whole, the other DS1 get a stub, and a tile set's tree harvest runs only when one of its DS1 is built.
- Contention: during the full run one other `pd2_merge.py` was running for the first ~12 minutes. During the subset
  baseline, one other profiled run was running. The prototype subset runs had the machine to themselves.

## Measured breakdown

### Per rule (full merge, empty scratch cache, all 34 rules run)

| rule | wall s | CPU s |
|---|---:|---:|
| presets-hd.tsv | 1334.0 | 1314.0 |
| item-tables.txt | 14.7 | 12.9 |
| tree-backgrounds.json | 9.3 | 9.1 |
| skill-icons.txt | 4.3 | 4.0 |
| level-files.tsv | 3.6 | 2.3 |
| the other 29 rules | 3.7 | |
| driver (snapshots, keep, check) | 3.3 | |
| **total** | **1372.9** | **1348.1** |

Working set: 6.0 GB peak, almost all of it the presets rule. CPU: one core, mean 99%, peak 145% (numpy in a few
spots). Disk: 130 MB/s while the extract and tables are read in the first seconds, then under 10 MB/s. The presets rule
writes 134 preset JSON files, 1.93 GB.

The cache's tracing (the audit hook and the wrapped `os.stat`) costs little. The same DS1 built just as fast with
tracing (run 1) as without it (the subset runs).

### Inside the presets rule

From the preset files' write times (run 1) and the subset runs:

- **Setup, ~30 s** before the first DS1. It reads 2041 stock D2R presets (877 MB of JSON, `json.loads` ~10 s),
  makes a `layout()` of every stock DS1 and every PD2 DS1 (2311 calls), harvests the kit (`Kit.add` of each stock
  preset) and runs `wall_pass` (the legacy layouts and contexts).
- **DS1 builds, ~1250 s** for 135 DS1 in 34 tile sets. The median DS1 takes 6.7 s. The slowest take: jungle1 62 s,
  kurastopenwide 49 s, jungle2 47 s, urehcity 44 s, jungle3 43 s. The 25 slowest DS1 take half the time. The figure
  includes each tile set's tree harvest (`pd2_painter.harvest`, 35 s for the subset's 5 groups).

cProfile of the 7-DS1 subset, original code (483 s under the profiler; plain run 261 s):

| where | cumulative s | share of the build | what |
|---|---:|---:|---|
| `trees` pass, `painter.paint` | 172 | 46% | the inner `ok` generator: `fits()` called 329 M times, per candidate subtile, per distance bucket, per option |
| `edges_walls` pass | 85 | 23% | `pick()` 30 s (a set built per option per bad tile), `legacy_edges()` 25 s (`wall_px()` re-sorted per tile: 57 k calls), `trim()` 18 s |
| `tiles` pass | 50 | 14% | `patches()` re-harvests the tile set's stock (two `Kit.add` per stock preset) for every DS1; `place()` repeats `json.dumps` tests per tile |
| `paste` pass | 13 | 3% | `rooms()` matching |
| setup `layout()` | 58 | | a linear search of the DS1's DT1 list per tile layer |
| `painter.harvest` | 46 | | `nearest()` BFS over the whole subtile grid per stock preset, repeated in every tile set whose stock holds it |
| `copy.deepcopy` | 28 | | entity copies in `place()` and `Kit.add` |
| `json.dumps` | 17 | | 1.5 M calls, almost all `is_tree()`, `structure()`, `fades()` regex tests on dumped components |

Writing the JSON output is not a hot spot: one `json.dumps` per DS1, about 1 s for all of them.

## Ranked speed-ups

Gains are for a full merge (~1373 s today). They multiply roughly: the hot-spot fixes shrink the work that parallelism
then splits.

| # | speed-up | est. gain | cost | risk |
|---|---|---|---|---|
| 1 | Hot-spot fixes in the presets rule (prototyped: below) | measured 2.6x on the presets rule (1334 s to 515 s), merge ~23 to ~9 min | small (~110 lines in 8 files) | low: byte-identical output, checked |
| 2 | Build DS1 in parallel worker processes, one merge slot per worker | ~4x more with 4 workers | medium | medium: RAM per worker |
| 3 | Rule cache key by the code a rule runs, not every module it imports | avoids ~60% of presets-rule reruns | medium | medium: a stale replay |
| 4 | More presets hot spots: the patch kit once per tile set, the BFS with numpy, `rooms()` | ~1.2-1.4x more | medium | low-medium |
| 5 | Run the 33 small rules alongside the presets rule | ~35 s | small-medium | low |

Not worth doing now: faster JSON encoding (orjson) of the output, or caching decoded DT1/DS1 across runs. Each is
under 2% of the run. A per-DS1 cache inside the presets rule is not worth it either: the common change (a pass in
`tools/presets/`) invalidates every DS1 at once.

### 1. Hot-spot fixes (prototyped)

Each change keeps the same result, and the same order of options where a hash picks from them:

- `pd2_painter.paint`: the trees pass's `fits()` filter checks the reach bound first, skips a distance bucket whose
  closest option is out of reach, and checks shading once per footprint per subtile instead of once per option. The
  dd search order is computed once per distance.
- `pd2_painter.harvest`: a stock preset's trees are learned once (keyed by the preset and the wet DT1), not again in
  every tile set whose stock holds it.
- `pd2_fidelity.legacy_edges`: `wall_px()` once per (DT1, own height), not per tile.
- `presets.layout`: each distinct (orientation, main, sub) is named once per DS1, and each distinct layer stack's
  flags are OR'd once.
- `presets.place`: the `structure`, `is_tree` and `fades` tests (each a `json.dumps` and a regex) once per kit
  instance.
- `presets.hooks.far`: `far()` once per stock entity (`Kit.add` asks again in every DS1's patch kit).
- `edges_walls.pick`: the options of one class are listed once per list; no set built per option; the crc32 only
  breaks ties.
- `presets.entity.clone`: a JSON-only deep copy in place of `copy.deepcopy` in `place()` and `Kit.add`.

Subset (7 DS1), no profiler:

| | original | prototype |
|---|---:|---:|
| setup | 31 s | ~23 s |
| tree harvest (5 tile sets) | 35.4 s | 16.8 s |
| the 7 builds | 194.0 s | 69.7 s |
| total | 260.7 s | 109.1 s (2.4x) |

All seven preset files are byte-identical to the original code's (SHA-256). The prototype diff is in
[`pd2-merge-speed-prototype.diff`](pd2-merge-speed-prototype.diff) on this branch. It is a measurement aid, not
mergeable code: `_BY_CLASS` and `_HARVESTED` are module-level caches with no reset.

**Full run (all 135 DS1, no profiler, no other merge running): the presets rule took 515 s with the prototype, against
1334 s in run 1 (2.6x).** All 134 written preset files are byte-identical to run 1's merge output. A whole merge would
then take about 515 + 39 ≈ 554 s (9.2 min, from 22.9).

### 2. Parallel DS1 builds

The 135 DS1 builds share only read-only inputs (the kit, the stock, the layouts, the wall contexts, a tile set's tree
harvest), so they can run in separate processes. Windows has no fork, so a worker cannot inherit the parent's 6 GB.
Two designs:

- **Each worker runs the setup itself** (~15-25 s after #1) and builds its share of the DS1. The output is identical by
  construction (the same code path), and it is the simplest design. Each worker holds the same ~6 GB.
- The parent does the setup and sends each task only what one tile set needs (the kit cut to the keys its DS1 want,
  its own stock, its tree harvest). This uses less RAM, but it is more code and more risk (`Kit.sources` indices and
  option order must survive).

Take one `pd2_cache.merge_slots` slot per worker, beyond the first, without waiting: `PD2R_MERGE_SLOTS` then still caps
machine-wide RAM (4 slots ≈ 24 GB). A lone merge gets 4 workers, and on a busy machine it falls back to one. Hand out
the DS1 longest first (by size, or the last run's times). The longest DS1 (jungle1, ~15 s after #1) then bounds the
critical path.

Estimate: after #1 the presets rule is ~500 s of CPU (measured 515 s). With 4 workers that is ~20 s setup + ~150 s, so the merge
takes about 3-3.5 min. With 8 workers (48 GB) it takes about 2 min.

### 3. A finer rule-cache key

`pd2_cache.sources()` keys a rule by every module it imports, at any depth. The presets rule imports `pd2_levels`, so
it gets `pd2_ids`, `pd2_tables`, `pd2_strings`, `pd2_skilldesc` and `pd2_automap` too. It also imports `pd2_coverage`
and `pd2_fidelity`, so their report and gate code (#787, #793) counts as well. Main commits since 2026-10-27 that touch
these files (some touch several): `pd2_ids` 53, `tools/presets` and `pd2_presets` 24, `pd2_coverage`/`pd2_fidelity` 15,
`pd2_tables`/`pd2_strings`/`pd2_skilldesc`/`pd2_automap` 14, `merge/presets-hd.tsv` 10, `pd2_levels` 7,
`pd2_cache` 6, `pd2_painter` 5.

Proposal: record the code a rule runs, the way the cache already records the files it reads. During a cache miss,
`sys.monitoring` (Python 3.12+) reports each `PY_START` of a code object in a project module once, and the callback
returns `DISABLE` after that, so the cost is near zero. The manifest stores a hash of each function's source and of
each touched module's top level (constants, class bodies, defaults: the module without its function bodies). A replay
is valid when all of them match. The run then stays valid across a `pd2_ids` change that touches no function the
presets rule calls. The gate runs uncached on CI, so a wrong replay could not reach a merge unnoticed.

A cheaper partial step: move what the presets rule needs from `pd2_coverage`/`pd2_fidelity` (boxes, footprints, the
edge classifier) into a small module of its own, and read tables through a slim module instead of `pd2_levels`/`pd2_ids`.

### 4. More presets hot spots (after #1)

- `presets.patches` re-harvests the tile set's stock twice (two shapes) for every DS1. A `Kit.add` per key does not
  depend on the other keys in `keep`, so one patch kit per tile set, with `keep` = the union of its DS1's patch keys,
  gives the same instances. That saves (n-1)/n of `patches()` in a tile set of n DS1.
- `pd2_painter.nearest` (a BFS in Python over 5x5 subtiles) and `nearest_blocked`: numpy, or scipy's distance
  transform. The tie-breaking of the source a subtile reaches must stay the same.
- `rooms.rooms`: `walkable(s)` is recomputed for every stock room for every DS1; the anchor search can be cut down.

### 5. Small rules alongside

`item-tables.txt` (14.7 s, art), `tree-backgrounds.json` (9.3 s) and `skill-icons.txt` (4.3 s) do not depend on the
presets rule. With #2 they matter more: ~35 s of a ~3 min merge.

## Open

- #2 is estimated, not prototyped.
- A cProfile of the prototype on the full run, to rank what is left after #1 (#4 is ranked from the subset).
- Per-worker RAM with the setup duplicated (expected ~6 GB). Measure it with the #2 prototype.
- A replay (all rules from the cache) was not timed. It copies ~2.3 GB of entries.
