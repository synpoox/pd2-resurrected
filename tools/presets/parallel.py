"""Farm the presets rule's DS1 builds out to worker processes (#807), and gather what they report.

Windows has no fork, so a worker cannot inherit the setup's ~6 GB: each worker runs apply.setup() itself, then takes DS1
from a shared queue (the biggest first) and builds them with apply.build_ds1, which writes the DS1's preset. One merge slot
(pd2_cache.merge_slots) holds each worker, so PD2R_MERGE_SLOTS still caps the machine's RAM: the rule's own slot is the
first worker's, the others are taken without waiting (pd2_cache.spare_slots), so a busy machine builds with fewer workers
and never queues behind itself. PD2R_PRESET_WORKERS caps the workers (default 4); with one worker, or no merge slot held
(a bare apply(), the unit tests), the DS1 build in this process, in the order apply() lists them.

The rule cache (pd2_cache.rule) traces what a rule reads: each worker traces its own reads and hands them back with its
last message, and pd2_cache.absorb() adds them, so the rule's entry is found again by everything its workers read. The
DS1 builds share nothing they write and the memos only save work, so the output does not depend on the worker count.
"""
from __future__ import annotations

import multiprocessing
import os
import queue
import sys
import traceback
from pathlib import Path

import pd2_cache

ENV = "PD2R_PRESET_WORKERS"
DEFAULT = 4
PER_WORKER = 8  # DS1 to build per worker, unless PD2R_PRESET_WORKERS asks for the workers


def wanted() -> tuple[int, bool]:
    """(the workers PD2R_PRESET_WORKERS asks for, whether it was set)."""
    raw = os.environ.get(ENV, "").strip()
    try:
        return max(1, int(raw)), True
    except ValueError:
        return DEFAULT, False


def build_all(pl, tasks: list[tuple[str, str]], box: list | None = None) -> tuple[list, int]:
    """Build every (tile set, DS1) task of the plan and return (the apply.DS1Built of each, what it opened: pd2_cache's
    sub-trace reads, {} untraced), in task order, and the number of stock presets the kit harvested. `box` may hold the
    State the caller made already: a build in this process takes it out (the caller drops its own, to free the RAM)."""
    n, asked = wanted()
    n = min(n, len(tasks)) if asked else min(n, len(tasks) // PER_WORKER)  # a worker's setup (minutes) must pay off
    spare = []
    if n > 1 and (pd2_cache.holding() or asked):
        # asked, with no merge slot to count against (no cache root): the caller's word
        spare = pd2_cache.spare_slots(n - 1) if pd2_cache.holding() else [None] * (n - 1)
        if len(spare) + 1 < n:
            print(f"  presets-hd: {len(spare) + 1} of {n} workers (merge slots busy)", file=sys.stderr, flush=True)
    if not spare:
        return _serial(pl, tasks, box)
    if box:
        box.clear()  # the workers set up themselves: this one is not used
    return _workers(pl, tasks, spare)


def _one(st, task: tuple[str, str]) -> tuple:
    """build_ds1 of one task, with what it opened (the part's reads)."""
    from . import apply
    with pd2_cache.sub_trace() as sub:
        built = apply.build_ds1(st, *task)
    reads = dict(sub.reads) if sub is not None else {}
    if built.row[3] or apply.draws_trees(st.layouts[task[1]]):  # an outdoor DS1 owns its tile set's tree harvest too
        reads = {**st.tree_reads.get(task[0], {}), **reads}
    return built, reads


def _serial(pl, tasks: list, box: list | None = None) -> tuple[list, int]:
    from . import apply
    st = box.pop() if box else apply.setup(pl)
    return [_one(st, t) for t in tasks], len(st.kit.sources)


def _cost(pl, task: tuple[str, str]) -> int:
    """What a DS1 costs to build, roughly: its file's size (the biggest maps are the slowest)."""
    try:
        return (pl.out_data / "global" / "tiles" / task[1]).stat().st_size
    except OSError:
        return 0


def _worker(pl, roots, tasks, results, i: int) -> None:
    """A worker process: set up, then build DS1 until the queue hands it None. Everything it says goes through `results`."""
    try:
        from . import apply
        if roots:
            pd2_cache.worker_start(roots)
        st = apply.setup(pl)
        results.put(("ready", i, len(st.kit.sources)))
        while (t := tasks.get()) is not None:
            results.put(("built", i, t, _one(st, t)))
        results.put(("done", i, pd2_cache.worker_finish() if roots else None))
    except BaseException as e:  # noqa: BLE001 - the parent re-raises it
        results.put(("error", i, type(e).__name__, str(e), traceback.format_exc()))


def _workers(pl, tasks: list, spare: list) -> tuple[list, int]:
    ctx = multiprocessing.get_context("spawn")
    todo, results = ctx.Queue(), ctx.Queue()
    order = sorted(tasks, key=lambda t: -_cost(pl, t))
    n = len(spare) + 1
    for t in order:
        todo.put(t)
    for _ in range(n):
        todo.put(None)
    roots = pd2_cache.trace_roots()
    procs = [ctx.Process(target=_worker, args=(pl, roots, todo, results, i), daemon=True) for i in range(n)]
    built: dict = {}
    sources, finished = 0, set()
    try:
        for p in procs:
            p.start()
        while len(finished) < n:
            try:
                msg = results.get(timeout=2)
            except queue.Empty:
                for i, p in enumerate(procs):
                    if i not in finished and not p.is_alive():
                        raise RuntimeError(f"presets-hd: worker {i} died (exit code {p.exitcode})")
                continue
            kind, i = msg[0], msg[1]
            if kind == "ready":
                sources = msg[2]
            elif kind == "built":
                built[msg[2]] = msg[3]
            elif kind == "done":
                finished.add(i)
                pd2_cache.absorb(msg[2])
                procs[i].join()
                if i and spare[i - 1] is not None:
                    spare[i - 1].release()  # this worker's RAM is free: so is its slot
            else:
                _, _, name, text, trace = msg
                if name == "ValueError":
                    raise ValueError(text)
                raise RuntimeError(f"presets-hd: worker {i} failed with {name}: {text}\n{trace}")
    finally:
        for p in procs:
            if p.is_alive():
                p.terminate()
        for p in procs:
            p.join()
        for s in spare:
            if s is not None:
                s.release()
    return [built[t] for t in tasks], sources
