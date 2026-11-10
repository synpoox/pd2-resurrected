# HD presets for PD2-only areas are generated from their DS1

D2R draws a level in HD from one HD preset per DS1, and none of the 144 DS1 that PD2-only areas use has one. Without a preset, a level plays on flat ground with invisible walls ([#159](https://github.com/synpoox/pd2-resurrected/issues/159)). The port imports each PD2 DS1 as it is, for layout and collision, and generates its HD preset at build time with its own generator: read the DS1's wall and floor cells, then place clones of D2R's stock entities from a chosen D2R tile set on those cells, over a stock flat terrain textured by a D2R biome. The committed part is the rule per tile set (which D2R kit and biome stand in for it); the presets themselves are generated, since they come from PD2 files (ADR 0002). PD2-only tiles are drawn with D2R stand-ins, each tile set an approximated deviation, until PD2's own art comes through the art pipeline.

The generator has two parts with one seam between them: reading a DS1 into a layout (cells, wall kinds, floors) and building a preset from a layout with a tile set's kit. D2R Reimagined's `Kit`/`Scene` builder does the second part, so it can replace ours without touching the first if the owner later accepts GPL-3.0 for the release.

Copying stock pieces tile by tile draws props and some walls, but not PD2's tree lines, streams and bridges. So the
build half layers three steps over the per-tile kit ([#293](https://github.com/synpoox/pd2-resurrected/issues/293), from
the [`lp3-border-painter` spike](https://github.com/synpoox/pd2-resurrected/tree/research/lp3-border-painter/spikes/lp3-border-painter)): a stock room whose tiles match 60% or more of a spot is pasted whole, with its
own sculpted terrain, water and bridges; a tile whose 3x3 neighbourhood (else its cross) appears in a stock preset copies
what that preset placed on the centre tile; and the border painter (`tools/pd2_painter.py`) drops the trees past the
playable area and paints them again from it out, copying the tile set's stock trees by direction and distance to the
blocked ground. Rooms and patches apply to every preset; the painter only to a DS1 that draws a `*tree*.dt1` tile, its
tree kit harvested per tile set from the stock presets that share its DT1 files. The playable area is the ground no DS1
cell marks unwalkable (bit `0x20000`), which also seals the plain-floor gaps of PD2's dashed tree lines, and no
blocked floor, except streams: a blocked floor group that touches walkable floor or draws a `water:` DT1. Other
blocked floor lies past the map's edge walls and gets trees. Last, the
water: a stock room draws its stream as one room-sized mesh, so outside pasted rooms the blocked floor of each DT1 a
`water:` line in `merge/presets-hd.d/` names gets a stock water plane, scaled to cover it in near-square rectangles.

## Considered Options

- Copy D2R Reimagined's preset builder now: proven, but binds the release to GPL-3.0, and it builds arenas from its own layouts, not from an existing DS1.
- Hand-author 74 scenes: closest to PD2's look per level, but scene work per level, redone on every re-baseline.
- Ship levels without presets: no work, but flat ground with invisible walls is not fit to ship.

## Consequences

- The alpha ships only when every imported PD2-only area has a generated preset; dev builds may run without one.
- Before a generated level ships, a game check in the largest one (Jungle Map, 251x251) compares frame rate with a D2R area of the same size and biome; more than 10% slower means the generator must merge entities or bake terrain first.
- A re-baseline regenerates every preset from the new DS1, with no hand edits to redo.
- Levels that are near-copies of a D2R room may start from D2R's preset instead, where that is cheaper.
