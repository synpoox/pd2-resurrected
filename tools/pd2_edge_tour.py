"""Edge tour (#733): warp points along every PD2-only map's edges and floors, and HD/legacy screenshot pairs there.

#707 compares the generated HD presets of the PD2-only areas with legacy along each map's edges and over each floor
type. Walking there is slow, so this plans warp points from a map's DS1 and `levels.txt`, then drives a game slot to
each and saves the HD shot beside the legacy shot (`g` toggles).

Edge types are pd2_fidelity.classify's (the score types, #747; a run is a 8-connected group of edge tiles; a point stands on
the walkable tile 4-next to one):
  wall, cliff, fence, pillar, treeline   a wall layer of the DS1 (its DT1's name and orientation)
  water   a water or lava edge wall layer, and a blocked liquid floor tile next to non-liquid floor
Floor types: `floor:<stem>`, the walkable tiles whose first floor tile is in the PD2 DT1 `<stem>` (not its D2R stand-in),
interior ones first.

A point's command is `cheat goto <levelId>,<x>,<y>` with x = OffsetX*5 + tileX*5 + 2 (y alike), OffsetX/OffsetY the
level's row in the merged levels.txt (generated/data/global/excel/). A map whose preset lists several DS1 rolls one per
game: the plan covers each, named by DS1, and the shot lands on the wanted tiles only when that variant loaded.

    py -3 tools/pd2_edge_tour.py list  --map "Torture Halls" [--type wall] [--points 4]
    py -3 tools/pd2_edge_tour.py shoot --slot 7 --map "Torture Halls" --type wall [--points 4]
    py -3 tools/pd2_edge_tour.py sweep --map jungle1 [--map ...] [--slots 4,5] [--points 10]

`list` needs the merged data (pd2_merge.py) and D2R's tiles in modkit's cache (like pd2_coverage.py). `shoot` needs the
slot locked and its game running in a game (Hell, level 90+); see docs/agents/game-slots.md. It reuses d2rAI-toolkit's
testing/game_ctl.py for the console, keys and window screenshots, and writes <out>/edge-tour/<map>/<name>.png: HD on
the left, legacy on the right.

`sweep` (#840) shoots every non-Palace variant with no manual step, across the free slots: see docs/agents/game-slots.md, "Edge
tour".
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

import pd2_coverage as coverage
import pd2_painter as painter
import pd2_presets as presets
from pd2_fidelity import EDGES, classify
from pd2_ids import Table, excel, load, toolkit
from pd2_levels import col

ROOT = Path(__file__).resolve().parent.parent
Tile = tuple[int, int]

AROUND = [(i, j) for i in (-1, 0, 1) for j in (-1, 0, 1) if i or j]
SETTLE = 3.0  # seconds after a warp before the HD shot (the level loads; a same-level warp is quick)
TOGGLE = 1.5  # seconds after `g` before the shot
PALACE = 141  # levels.txt Id of the Palace (Horadrim Tower): unplayable, never swept


# ---------------------------------------------------------------------------------------------------- warp points

def subtile(offset: int, tile: int) -> int:
    """The world subtile at the middle of a tile (+2: the middle of its 5 subtiles)."""
    return offset * 5 + tile * 5 + 2


def goto(level: int, offset: tuple[int, int], tile: Tile) -> str:
    """The console line that warps to a DS1 tile of a level whose levels.txt row has `offset` (OffsetX, OffsetY)."""
    return f"cheat goto {level},{subtile(offset[0], tile[0])},{subtile(offset[1], tile[1])}"


def offsets(out_data: Path) -> dict[int, tuple[str, int, int]]:
    """levels.txt: Id -> (Name, OffsetX, OffsetY)."""
    t = Table(excel(out_data) / "levels.txt")
    i, n, x, y = (col(t, c) for c in ("Id", "Name", "OffsetX", "OffsetY"))
    return {int(r[i]): (r[n], int(r[x] or 0), int(r[y] or 0)) for r in t.rows if r[i].strip().isdigit()}


# ---------------------------------------------------------------------------------------------------- edges and floors

def components(tiles: set[Tile]) -> list[list[Tile]]:
    """The 8-connected groups of tiles, longest first (ties by position), each sorted."""
    left, out = set(tiles), []
    while left:
        stack, group = [min(left)], []
        left.discard(stack[0])
        while stack:
            x, y = stack.pop()
            group.append((x, y))
            for dx, dy in AROUND:
                if (n := (x + dx, y + dy)) in left:
                    left.discard(n)
                    stack.append(n)
        out.append(sorted(group))
    return sorted(out, key=lambda g: (-len(g), g[0]))


def spread(tiles: list[Tile], k: int) -> list[Tile]:
    """Up to k of the tiles, spread out: the one nearest the centre, then each time the one farthest from those taken."""
    if not tiles:
        return []
    cx, cy = sum(t[0] for t in tiles) / len(tiles), sum(t[1] for t in tiles) / len(tiles)
    picked = [min(tiles, key=lambda t: (math.hypot(t[0] - cx, t[1] - cy), t))]
    while len(picked) < min(k, len(tiles)):
        picked.append(max((t for t in tiles if t not in picked),
                          key=lambda t: (min(max(abs(t[0] - p[0]), abs(t[1] - p[1])) for p in picked), t)))
    return picked


def edge_tiles(lay: presets.Layout) -> dict[str, set[Tile]]:
    """The edge tiles of each type (pd2_fidelity.EDGES, by classify) that have walkable floor 4-next to them. `lay` names
    the PD2 DT1s (no stand-ins)."""
    walk = presets.walkable(lay)
    found: dict[str, set[Tile]] = {e: set() for e in EDGES}
    liquid = set()
    for c, (kind, tiles) in lay.keys.items():
        for t in tiles:
            if kind == "w" and (k := classify(t[0], t[1], "wall")) in found:
                found[k].add(c)
            elif kind == "f" and classify(t[0], 0, "floor", painter.blocked_floor(lay.flags.get(c, bytes(25)))) == "liquid":
                liquid.add(c)
    found["water"] |= {c for c in liquid if any(n in lay.keys and n not in liquid for n in
                                                ((c[0] + dx, c[1] + dy) for dx, dy in painter.STEPS))}
    return {k: {c for c in v if any((c[0] + dx, c[1] + dy) in walk for dx, dy in painter.STEPS)}
            for k, v in found.items()}


def stands(lay: presets.Layout, run: list[Tile]) -> list[Tile]:
    """The walkable tiles 4-next to a run of edge tiles."""
    walk = presets.walkable(lay)
    return sorted({(x + dx, y + dy) for x, y in run for dx, dy in painter.STEPS if (x + dx, y + dy) in walk})


def floor_types(lay: presets.Layout) -> dict[str, list[Tile]]:
    """`floor:<PD2 DT1 stem>` -> the walkable tiles drawing it, those with only same-type neighbours first."""
    walk, by = presets.walkable(lay), {}
    for c in sorted(walk):
        by.setdefault((lay.keys[c][1][0][0] or "none").rsplit("/", 1)[-1].removesuffix(".dt1"), set()).add(c)
    out = {}
    for stem, cells in by.items():
        inner = [c for c in sorted(cells) if all((c[0] + dx, c[1] + dy) in cells for dx, dy in AROUND)]
        out[f"floor:{stem}"] = inner or sorted(cells)
    return out


def pick(groups: list[list[Tile]], n: int) -> list[Tile]:
    """n points over the groups (longest first): each group's centre, then each group's next-most-spread tile."""
    spreads, out = [spread(g, n) for g in groups], []
    for r in range(n):
        for s in spreads:
            if r < len(s) and s[r] not in out:
                out.append(s[r])
            if len(out) == n:
                return out
    return out


@dataclass(frozen=True)
class Point:
    map: str  # the lvlprest Name, e.g. "Torture Halls Map"
    ds1: str  # the DS1 stem
    level: int
    type: str  # an edge type or `floor:<stem>`
    tile: Tile
    command: str

    @property
    def name(self) -> str:
        slug = re.sub(r"[^a-z0-9]+", "-", f"{self.ds1}_{self.type}".lower()).strip("-")
        return f"{slug}_{self.tile[0]}-{self.tile[1]}"


def plan(lay: presets.Layout, map_: str, ds1: str, level: int, offset: tuple[int, int], types: list[str] | None = None,
         n: int = 4) -> list[Point]:
    """The warp points of one DS1: up to n per wanted type (default: every edge type and floor type the DS1 has)."""
    edges = edge_tiles(lay)
    groups = {t: [stands(lay, run) for run in components(edges[t])] for t in EDGES if edges.get(t)}
    groups = {t: [g for g in gs if g] for t, gs in groups.items()}
    groups |= {t: [c] for t, c in floor_types(lay).items()}
    out = []
    for t, gs in groups.items():
        if gs and (not types or t in types or t.split(":")[0] in types):
            out += [Point(map_, ds1, level, t, c, goto(level, offset, c)) for c in pick([g for g in gs if g], n)]
    return out


# ---------------------------------------------------------------------------------------------------- the maps

@dataclass
class Map:
    name: str
    level: int
    ds1: str  # DS1 path as lvlprest lists it
    files: list[str]  # the DT1 files its preset loads


def maps(out_data: Path, d2r_data: Path) -> list[Map]:
    """The PD2-only presets with a generated HD preset, one per DS1 (a DS1 two presets or one preset twice lists is
    kept once, by its lower-cased path)."""
    lp = load(out_data, "lvlprest")
    level = {r[0]: int(r[col(lp, "LevelId")]) for r in lp.rows if r[0] and r[col(lp, "LevelId")].isdigit()}
    out, seen = [], set()
    for name, _, files, ds1s in presets.pd2_only_presets(out_data, len(load(d2r_data, "lvlprest").records())):
        for ds1 in ds1s:
            hd = out_data / presets.PRESETS / Path(ds1).with_suffix(".json")
            if ds1.lower() not in seen and hd.is_file() and level.get(name):
                seen.add(ds1.lower())
                out.append(Map(name, level[name], ds1, files))
    return out


def select(found: list[Map], wanted: str, exact: bool = False) -> list[Map]:
    """The maps whose preset name or DS1 stem contains `wanted` (case-insensitive), or all for an empty one. `exact`:
    the name (with or without its " Map") or the stem equals it."""
    w = wanted.lower()
    if exact:
        return [m for m in found if w in (m.name.lower(), m.name.lower().removesuffix(" map"), Path(m.ds1).stem.lower())]
    return [m for m in found if w in m.name.lower() or w in Path(m.ds1).stem.lower()]


def map_layout(m: Map, out_data: Path, tiles, pd2_tiles) -> tuple[bytes, presets.Layout]:
    """A map's DS1 bytes and its layout naming the PD2 DT1s (else D2R's own), no stand-ins."""
    data = (out_data / "global" / "tiles" / m.ds1).read_bytes()
    return data, presets.layout(data, {p: pd2_tiles[p] or tiles[p] for p in m.files})


def plan_all(out_data: Path, d2r_data: Path, wanted: str, types: list[str] | None, n: int) -> list[Point]:
    lv = offsets(out_data)
    tiles, pd2_tiles = presets.Tiles(d2r_data / "global" / "tiles"), presets.Tiles(out_data / "global" / "tiles")
    out = []
    for m in select(maps(out_data, d2r_data), wanted):
        _, lay = map_layout(m, out_data, tiles, pd2_tiles)
        _, ox, oy = lv[m.level]
        out += plan(lay, m.name, Path(m.ds1).stem, m.level, (ox, oy), types, n)
    return out


# ---------------------------------------------------------------------------------------------------- the game slot

PLAY = (0.394, 0.898)  # the character screen's Play button (fractions of the client area, 1920x1080)
HELL = (0.5, 0.543)  # the difficulty popup's Hell button
LOAD_TOP, LOAD_MID = 1.5, 30.0  # loading(): the top strip is darker than this, the middle brighter
MIN_LUMA = 10.0  # a screenshot whose mean grey is below this is a loading screen or black, not a menu
NO_LEGACY = frozenset({199})  # levels (Poisoned Well Map, all variants) where `g` hangs the game (#778): HD only, the legacy half stays blank
CHEATS = ("cheat nogethit 1", "cheat god 1", "cheat speedup 300", "cheat stat item_lightradius, 25")


def side_by_side(left, right):
    """One toolkit Image holding two of the same size, `left` on the left."""
    from game_ctl import Image
    if (left.width, left.height) != (right.width, right.height):
        raise ValueError("the two shots differ in size")
    w, stride = left.width, left.width * 3
    rows = [left.rgb[y * stride:(y + 1) * stride] + right.rgb[y * stride:(y + 1) * stride] for y in range(left.height)]
    return Image(2 * w, left.height, b"".join(rows))


def blank_like(img):
    """A mid-grey Image of the size of `img` (the legacy half of a pair that cannot be shot)."""
    from game_ctl import Image
    return Image(img.width, img.height, bytes([60]) * (img.width * img.height * 3))


def load_game_ctl():
    sys.path.insert(0, str(toolkit(ROOT) / "testing"))
    import game_ctl
    import testing_config as tc
    return game_ctl, tc


def pair(game, settle: float, legacy: bool = True):
    """The (HD, legacy) screenshots at the current spot. `g` is pressed back whatever happens to the legacy shot (#795)."""
    nap(settle)
    hd = game.screenshot()
    if not legacy:
        return hd, blank_like(hd)
    pressed = False
    try:
        game.key("g")
        pressed = True
        nap(TOGGLE)
        return hd, game.screenshot()
    finally:
        if pressed:
            game.key("g")
            time.sleep(TOGGLE)


def shoot(points: list[Point], slot: int, out: Path, settle: float = SETTLE, log=print) -> list[Path]:
    """Warp to each point on a locked slot whose game is in a game, and save the HD|legacy pair. Leaves the game in HD."""
    game_ctl, tc = load_game_ctl()
    game = game_ctl.Game(tc.load(None), slot, os.environ.get("MODKIT_RUN"))
    if not game.is_running():
        raise SystemExit(f"slot {slot}: the game is not running (game_ctl.py --slot {slot} start, then enter a game)")
    for line in ("cheat nogethit 1", "cheat god 1"):
        game.console(line)
    saved = []
    for p in points:
        if not game.console(p.command):
            log(f"skip {p.name}: the console did not take {p.command!r}")
            continue
        hd, legacy = pair(game, settle)
        if game_ctl.signature_distance(hd.signature(), legacy.signature()) == 0:
            log(f"warning {p.name}: HD and legacy shots are identical (is `g` toggling? is the game in HD?)")
        path = out / p.map.lower().replace(" ", "-") / f"{p.name}.png"
        saved.append(Path(side_by_side(hd, legacy).save(str(path))))
        log(f"{p.command}  ->  {path}")
    return saved


# ---------------------------------------------------------------------------------------------------- the sweep

STOP = threading.Event()  # set on Ctrl-C: every worker stops at its next check and runs its cleanup


class Stopped(Exception):
    """The sweep was interrupted."""


def nap(seconds: float) -> None:
    """Sleep, but raise Stopped as soon as the sweep is interrupted."""
    if STOP.wait(seconds):
        raise Stopped()


def references(m: Map, data: bytes, lay: presets.Layout, offset: tuple[int, int], classes: dict) -> list[Point]:
    """The walkable reference points of a variant (pd2_warps.reference_spots): the walkable tile nearest the centre
    (the centre itself can be rock or water) and each waypoint, with their `cheat goto` lines."""
    from pd2_warps import reference_spots  # inside: pd2_warps imports this module
    out = []
    for label, x, y in reference_spots(data, lay, classes):
        if label != "centre":
            out.append(Point(m.name, Path(m.ds1).stem, m.level, "ref-" + label.split()[0], (x, y), goto(m.level, offset, (x, y))))
    return out


def variant_points(refs: list[Point], edge_points: list[Point], n: int) -> list[Point]:
    """About n points: up to two references, then one point per edge or floor type in turn (the plan's order: edges
    first). Defect-driven points (pd2_parity) will replace the plan's."""
    by: dict[str, list[Point]] = {}
    for p in edge_points:
        by.setdefault(p.type, []).append(p)
    rest = []
    for r in range(max((len(v) for v in by.values()), default=0)):
        rest += [v[r] for v in by.values() if r < len(v)]
    return (refs[:2] + rest)[:n]


def sweep_variants(data: Path, d2r: Path, wanted: list[str], n: int, exact: bool = True) -> list[tuple[Map, list[Point]]]:
    """Every non-Palace variant matching a --map (all when none; exact names unless `exact` is off), with its points."""
    from pd2_warps import object_classes
    lv, classes = offsets(data), object_classes(data)
    tiles, pd2_tiles = presets.Tiles(d2r / "global" / "tiles"), presets.Tiles(data / "global" / "tiles")
    chosen = [m for m in maps(data, d2r) if m.level != PALACE and (not wanted or any(select([m], w, exact) for w in wanted))]
    out = []
    for m in chosen:
        raw, lay = map_layout(m, data, tiles, pd2_tiles)
        _, ox, oy = lv[m.level]
        stem = Path(m.ds1).stem
        out.append((m, variant_points(references(m, raw, lay, (ox, oy), classes),
                                      plan(lay, m.name, stem, m.level, (ox, oy), None, 2), n)))
    return out


def mean_luma(img) -> float:
    """The mean grey of a toolkit Image, from a 64x36 thumbnail (the toolkit's own stats() walks every pixel in Python)."""
    from PIL import Image
    thumb = Image.frombytes("RGB", (img.width, img.height), img.rgb).convert("L").resize((64, 36))
    return sum(thumb.tobytes()) / (64 * 36)


def region_luma(img, fx0: float, fy0: float, fx1: float, fy1: float) -> float:
    """The mean grey of a fractional region of a toolkit Image."""
    from PIL import Image
    full = Image.frombytes("RGB", (img.width, img.height), img.rgb)
    box = (int(fx0 * img.width), int(fy0 * img.height), int(fx1 * img.width), int(fy1 * img.height))
    thumb = full.crop(box).convert("L").resize((32, 18))
    return sum(thumb.tobytes()) / (32 * 18)


def loading(img) -> bool:
    """The level loading screen: a lit picture in the middle of black (the top of the frame is pure black, the middle is
    bright; a dark level's frame has scenery up top)."""
    return region_luma(img, 0.2, 0.05, 0.8, 0.25) < LOAD_TOP and region_luma(img, 0.35, 0.4, 0.65, 0.75) > LOAD_MID


def wait_loaded(game, timeout: float = 45.0, log=print) -> bool:
    """Wait (up to timeout) until the screen is no longer the loading screen; False when it still is."""
    end = time.monotonic() + timeout
    while loading(game.screenshot()):
        if time.monotonic() > end:
            log(f"warning: still loading after {timeout:g} s")
            return False
        nap(1.0)
    return True


def wait_lit(game, timeout: float = 60.0, what: str = "the game") -> None:
    """Wait for a screenshot that is not black (the luma check before a click)."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            if mean_luma(game.screenshot()) >= MIN_LUMA:
                nap(1.0)
                return
        except Stopped:
            raise
        except Exception:  # the window is not there yet
            pass
        nap(1.0)
    raise TimeoutError(f"{what}: the screen stayed black for {timeout:g} s")


def enter_hell(game, timeout: float = 90.0, log=print) -> None:
    """From the started game's character screen into a Hell game: Play, Hell, then wait for the console to answer."""
    wait_lit(game, 90, "character screen")
    game.click(*PLAY)
    nap(2.0)
    wait_lit(game, 30, "difficulty popup")
    game.click(*HELL)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        nap(2.0)
        try:
            if game.console("cheat nogethit 1", timeout=3):
                return
        except Stopped:
            raise
        except Exception:
            pass
    raise TimeoutError(f"no Hell game after {timeout:g} s")


def shoot_variant(game, m: Map, points: list[Point], out: Path, settle: float, log=print) -> list[Path]:
    """Cheats, then the HD|legacy pair of each point (after the loading screen, which the first warp shows)."""
    for c in CHEATS:
        game.console(c)
    legacy_ok = m.level not in NO_LEGACY
    saved = []
    for i, p in enumerate(points):
        nap(0)
        if not game.console(p.command):
            log(f"skip {p.name}: the console did not take {p.command!r}")
            continue
        game.console("cheat killmonster")
        nap(settle)
        wait_loaded(game, log=log)  # a warp to another level shows the loading screen first
        hd, legacy = pair(game, 0.5, legacy_ok)
        saved.append(Path(side_by_side(hd, legacy).save(str(out / f"{i:02d}_{p.name}.png"))))
    return saved


def finish_sheet(folder: Path, dest: Path, log=print) -> list[dict]:
    """The JPEG sheet of the pairs in `folder`, their metrics, and the PNGs pruned."""
    import contact_sheet
    dest.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, m in contact_sheet.sheet(folder, dest):
        rows.append({"point": name, "luma_hd": round(m["hd"][0], 1), "luma_legacy": round(m["legacy"][0], 1),
                     "luma_ratio": round(m["ratio"], 3), "black_hd": round(m["hd"][1], 3),
                     "black_legacy": round(m["legacy"][1], 3)})
    shutil.rmtree(folder, ignore_errors=True)
    return rows


def run_slot(slot: int, jobs: list[tuple[Map, list[Point]]], out: Path, owner: str, settle: float, results: list,
             log=print) -> None:
    """One slot's share of the sweep. Locks the slot (a lock tied to this process: a hard kill leaves it stale, which
    the next claim replaces), checks STOP before each variant and point, and in a `finally` stops the game, resets the
    pin and unlocks."""
    game_ctl, tc = load_game_ctl()  # puts the toolkit's testing/ on sys.path
    import game_slots
    import pd2_force_ds1 as force
    cfg = tc.load(None)

    def lock() -> bool:  # Game.stop() releases a pid lock, so the slot is claimed again at once after it
        ok, msg = game_slots.acquire(cfg, slot, owner)
        if not ok:
            log(f"slot {slot}: {msg}")
        return ok

    if not lock():
        return
    log(f"slot {slot}: locked by {owner}")
    game = game_ctl.Game(cfg, slot, owner, log=lambda *a: None)
    try:
        for m, points in jobs:
            if STOP.is_set():
                break
            stem, t0 = Path(m.ds1).stem, time.monotonic()
            row = {"map": m.name, "ds1": stem, "slot": slot, "points": len(points)}
            folder = out / "_pairs" / f"slot{slot}" / stem
            folder.mkdir(parents=True, exist_ok=True)
            try:
                game.stop()
                if not lock():
                    raise RuntimeError("the slot's lock was taken while its game stopped")
                log("slot %d: %s: %s" % (slot, stem, force.apply(force.lvlprest_path(slot, owner=owner), [Path(m.ds1).name])[0]))
                game.start()
                enter_hell(game)
                shot = shoot_variant(game, m, points, folder, settle, log)
                row["pairs"] = len(shot)
                row["metrics"] = finish_sheet(folder, out / m.name.lower().replace(" ", "-") / f"{stem}.jpg", log)
                row["sheet"] = f"{m.name.lower().replace(' ', '-')}/{stem}.jpg"
            except Stopped:
                row["error"] = "interrupted"
                log(f"slot {slot}: {stem} interrupted")
            except Exception as e:  # one variant failing must not stop the slot's others
                row["error"] = f"{type(e).__name__}: {e}"
                log(f"slot {slot}: {stem} failed: {row['error']}")
            row["seconds"] = round(time.monotonic() - t0, 1)
            log(f"slot {slot}: {stem}: {row.get('pairs', 0)} pairs in {row['seconds']} s")
            results.append(row)
    finally:
        try:
            game.stop()
        finally:
            try:
                lock()  # the pin reset's guard wants the lock held, and the game stopped
                force.reset(force.lvlprest_path(slot, owner=owner))
            except Exception as e:
                log(f"slot {slot}: pin not reset: {e}")
            log(f"slot {slot}: " + game_slots.release(cfg, slot, owner)[1])


def free_slots(cfg, candidates: list[int], keep_free: int) -> list[int]:
    """The free slots among the candidates, minus `keep_free` of them left unlocked (other work needs one)."""
    import game_slots
    free = [s for s in candidates if game_slots.lock_info(cfg, s) is None and not game_slots.slot_processes(cfg, s)]
    return free[:max(0, len(free) - max(0, keep_free))]


def sweep(variants: list[tuple[Map, list[Point]]], slots: list[int], out: Path, settle: float = SETTLE, log=print) -> list[dict]:
    """Shard the variants over the slots (one thread each) and write <out>/sweep.json. Ctrl-C sets STOP and waits for
    every worker's cleanup (game stopped, pin reset, slot unlocked) before anything is written or pruned; the rows of
    the variants that finished are kept. Returns the rows."""
    owner = f"sweep-{os.getpid()}"  # the lock's owner: unique per sweep, and `acquire` ties the lock to this pid
    STOP.clear()
    results: list[dict] = []
    shares = [variants[i::len(slots)] for i in range(len(slots))]
    threads = [threading.Thread(target=run_slot, args=(s, share, out, owner, settle, results, log), name=f"slot{s}")
               for s, share in zip(slots, shares) if share]
    t0 = time.monotonic()
    interrupted = False
    for t in threads:
        t.start()
    while any(t.is_alive() for t in threads):
        try:
            for t in threads:
                t.join(0.5)
        except KeyboardInterrupt:  # (a second Ctrl-C lands here too: keep waiting for the cleanups)
            if not STOP.is_set():
                log("interrupted: stopping each slot (game, pin, lock) before exit; Ctrl-C again does not skip that")
            STOP.set()
            interrupted = True
    out.mkdir(parents=True, exist_ok=True)
    (out / "sweep.json").write_text(json.dumps(
        {"slots": slots, "seconds": round(time.monotonic() - t0, 1), "interrupted": interrupted,
         "variants": sorted(results, key=lambda r: r["ds1"])}, indent=1), "utf-8")
    shutil.rmtree(out / "_pairs", ignore_errors=True)
    STOP.clear()
    return results


# ---------------------------------------------------------------------------------------------------- command line

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("list", "shoot", "sweep"))
    ap.add_argument("--map", action="append", default=[], help="a PD2-only preset name or DS1 stem, or part of one "
                    "(repeatable; default: all). sweep matches exactly (--contains: part of one) and skips the Palace")
    ap.add_argument("--contains", action="store_true", help="sweep: --map matches part of a name or stem")
    ap.add_argument("--type", action="append", default=[], help=f"edge type ({', '.join(EDGES)}), `floor` for every "
                    "floor type or `floor:<stem>` (repeatable; default: all)")
    ap.add_argument("--points", type=int, default=None, help="points per type and DS1 (default 4); sweep: points per "
                    "variant (default 10)")
    ap.add_argument("--data", type=Path, default=ROOT / "generated" / "data", help="merged data (pd2_merge's generated/data)")
    ap.add_argument("--d2r", type=Path, default=coverage.D2R_DATA, help="D2R data root")
    ap.add_argument("--out", type=Path, default=None, help="where `shoot` (out/edge-tour) and `sweep` (coverage/sweep) write")
    ap.add_argument("--slot", type=int, help="shoot: the locked slot, its game running in a game")
    ap.add_argument("--slots", default="4,5,6,7,8", help="sweep: the slots it may use (free ones only)")
    ap.add_argument("--keep-free", type=int, default=1, help="sweep: slots of --slots to leave unlocked for other work")
    ap.add_argument("--settle", type=float, default=SETTLE, help="seconds to wait after each warp")
    a = ap.parse_args(argv)
    if a.cmd == "sweep":
        variants = [v for v in sweep_variants(a.data, a.d2r, a.map, a.points or 10, not a.contains) if v[1]]
        if not variants:
            print(f"no variants for {a.map or 'all maps'}")
            return 1
        game_ctl, tc = load_game_ctl()
        slots = free_slots(tc.load(None), [int(s) for s in a.slots.split(",")], a.keep_free)
        if not slots:
            print("no free slot to sweep on (the others are locked or running, or --keep-free leaves none)")
            return 1
        out = a.out or ROOT / "coverage" / "sweep"
        print(f"{len(variants)} variants on slots {slots}")
        rows = sweep(variants, slots, out, a.settle)
        bad = [r for r in rows if "error" in r]
        print(f"{len(rows) - len(bad)} of {len(rows)} variants shot; sheets and sweep.json under {out}")
        return 1 if bad or not rows else 0
    points = plan_all(a.data, a.d2r, a.map[0] if a.map else "", a.type, a.points or 4)
    if not points:
        print(f"no points for map {a.map or 'any'!r}, type {a.type or 'any'}")
        return 1
    if a.cmd == "list":
        for p in points:
            print(f"{p.map}\t{p.ds1}\t{p.type}\t{p.tile[0]},{p.tile[1]}\t{p.command}")
        return 0
    if a.slot is None:
        ap.error("shoot needs --slot N")
    saved = shoot(points, a.slot, a.out or ROOT / "out" / "edge-tour", a.settle)
    print(f"{len(saved)} of {len(points)} pairs under {a.out or ROOT / 'out' / 'edge-tour'}")
    return 0 if saved else 1


if __name__ == "__main__":
    sys.exit(main())
