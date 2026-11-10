"""Machine-wide caches every worktree shares (#440), so ten agents don't each extract, merge and copy the same data.

The cache root is PD2R_CACHE (default C:\\pd2r-cache). Without that folder, or on CI, every tool works per worktree as
before. Under the root:
  pd2_extract.lock     held by every pd2_extract run, so two extracts never run at once
  extract/<key>/       pd2_extract's output, one per extract stamp; a worktree's pd2-data/ is a junction to it
  rules/<key>/<reads>/ what one pd2_merge rule wrote, for its code and stamps and what it read (rule, below)
A cache folder is complete before it gets its name, so no run sees another's half-written one. pd2_merge prunes rules/
to a size cap, sparing what was used in the last 15 minutes. merge/ (the whole-merge folders older tools kept) is pruned
only with PD2R_PRUNE_LEGACY=1, as a worktree on older tools may be copying one at any time; extract/ is never pruned:
delete old ones by hand when no worktree's pd2-data/ points at them.
"""
from __future__ import annotations

import ast
import contextlib
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable

try:
    import _winapi
    import msvcrt
except ImportError:  # not Windows
    _winapi = msvcrt = None
    import fcntl


def root() -> Path | None:
    """The cache root, or None: CI keeps its own layout (gate.yml's --out, release.yml's pd2-data/) and merges fresh."""
    p = Path(os.environ.get("PD2R_CACHE", r"C:\pd2r-cache"))
    return None if os.environ.get("GITHUB_ACTIONS") or not p.is_dir() else p


@contextlib.contextmanager
def lock(path: Path, poll: float = 1.0):
    """Hold an OS lock on the file path, waiting while another process holds it. The OS drops the lock when its holder
    exits, so a crashed run never leaves a stale one."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
    with open(path, "r+b") as f:
        misses, shown = 0, None
        while True:
            try:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                # not on the first miss: a holder that has just taken the lock has not written its pid yet, and the
                # file still names the holder before it. Again when the holder changes.
                misses += 1
                if misses > 1 and (who := holder(path)) != shown:
                    print(f"waiting for {path}: {who} holds it", flush=True)
                    shown = who
                time.sleep(poll)
        try:
            f.seek(1)  # byte 0 is the locked one; the holder's pid and start time follow it, for a waiter's message
            f.write(f"{os.getpid()} {int(time.time())}".encode())
            f.truncate()
            f.flush()
            yield
        finally:
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_UN)


def holder(path: Path) -> str:
    """Who wrote path's lock last: "pid N (running, holding since HH:MM:SS)" or "... (gone, ...)". The OS drops a lock when its holder dies, so a
    lock still held names a live process (an orphan of a killed shell runs on and keeps it until it ends)."""
    try:
        with open(path, "rb") as f:
            f.seek(1)  # not byte 0: Windows refuses a read of the locked byte
            pid, _, since = f.read().decode().partition(" ")
            pid, since = int(pid or 0), int(since or 0)
    except (OSError, ValueError):
        pid = since = 0
    if not pid:
        return "another run"
    try:
        if os.name == "nt":
            import ctypes
            h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
            alive = False
            if h:  # a handle can outlive its process: ask whether it still runs (259: STILL_ACTIVE)
                code = ctypes.c_ulong()
                alive = bool(ctypes.windll.kernel32.GetExitCodeProcess(h, ctypes.byref(code))) and code.value == 259
                ctypes.windll.kernel32.CloseHandle(h)
        else:
            os.kill(pid, 0)
            alive = True
    except PermissionError:
        alive = True
    except OSError:
        alive = False
    when = f", holding since {time.strftime('%H:%M:%S', time.localtime(since))}" if since else ""
    return f"pid {pid} ({'running' if alive else 'gone'}{when})"


def key(paths: list[Path], *extra: str) -> str:
    """A hash of every file under paths (its path from that path's parent, and its bytes) and of extra."""
    h = hashlib.sha256("\0".join(extra).encode())
    for base in paths:
        for p in sorted(base.rglob("*")) if base.is_dir() else [base]:
            rel = p.relative_to(base.parent)
            if p.is_file() and "__pycache__" not in rel.parts:
                h.update(rel.as_posix().encode() + b"\0" + hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()[:16]


def is_link(p: Path) -> bool:
    return p.is_symlink() or os.path.isjunction(p)


def point(link: Path, target: Path) -> None:
    """Make link a junction (a symlink off Windows) to target, replacing a link that points elsewhere."""
    if is_link(link):
        if os.path.realpath(link) == os.path.realpath(target):
            return
        os.rmdir(link) if os.name == "nt" else link.unlink()  # removes the link only, never the target's files
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True)
    else:
        link.symlink_to(target, target_is_directory=True)


def sources(fn: Callable) -> list[Path]:
    """fn's module file and every module beside it that it imports, at any depth (a rule's code). A module inside a
    package brings the whole package (every .py under its top folder): relative imports and passes found by discovery
    are code the rule runs too (#754). An import in a module's main() is left out: a merge never runs a tool's command
    line (pd2_strings' imports pd2_merge, which would put every tool in every key)."""
    todo, seen = [Path(sys.modules[fn.__module__].__file__)], set()

    def package(d: Path) -> Path | None:  # the top folder of the package d is in, or None
        top = None
        while (d / "__init__.py").is_file():
            top, d = d, d.parent
        return top

    while todo:
        p = todo.pop()
        if p in seen:
            continue
        seen.add(p)
        top = package(p.parent)
        if top is not None:
            todo += [q for q in top.rglob("*.py") if "__pycache__" not in q.parts]
        homes = {p.parent, top.parent} if top else {p.parent}
        tree = ast.parse(p.read_text("utf-8"))
        cli = {id(n) for d in tree.body if isinstance(d, ast.FunctionDef) and d.name == "main" for n in ast.walk(d)}
        for n in ast.walk(tree):
            if id(n) in cli:
                continue
            names = ([a.name for a in n.names] if isinstance(n, ast.Import) else
                     [n.module] if isinstance(n, ast.ImportFrom) and n.module and not n.level else [])
            for m in names:
                for h in homes:
                    if (q := h / f"{m.split('.')[0]}.py").is_file():
                        todo.append(q)
    return sorted(seen)


# The rule cache (#558): rules/<key>/<reads>/ holds what one merge rule wrote to generated/, under a key of its code
# (sources), the stamps and modkit, and is found again by what the rule looked at while it ran, as it was before: under
# the project ("p"), each file's bytes, path missing and folder listing; under D2R's tables ("d"), the same with a file's
# size and time for its bytes (modkit fetch only adds and replaces files there). pd2-data/ is keyed by its stamp and
# tools/ by sources, so neither is traced.
NOT_TRACED = ("pd2-data", "tools")
_digests: dict[tuple[str, int, int], str] = {}
_trace: "Trace | None" = None
used: list[Path] = []  # the entries rule() replayed or kept since the caller last cleared this (evict, below)


def _roots(roots: dict[str, Path]) -> dict[str, str]:
    return {t: os.path.normcase(os.path.abspath(r)) + os.sep for t, r in roots.items()}


def _state(roots: dict[str, str], k: str) -> str | None:
    """What trace key k ("f:<root>:<path>" a file or folder, "l:<root>:<path>" a listing) reads as now; None if missing."""
    kind, tag, rel = k.split(":", 2)
    p = os.path.join(roots[tag], rel)
    try:
        if kind == "l":
            with os.scandir(p) as it:
                return "|".join(sorted(e.name + "/" * e.is_dir() for e in it))
        if _isdir(p):
            return "dir"
        st = _stat(p)
        if tag == "d":
            return f"{st.st_size}:{st.st_mtime_ns}"
        if (p, st.st_size, st.st_mtime_ns) not in _digests:
            with open(p, "rb") as f:
                _digests[p, st.st_size, st.st_mtime_ns] = hashlib.file_digest(f, "sha256").hexdigest()
        return _digests[p, st.st_size, st.st_mtime_ns]
    except OSError:
        return None


class Trace:
    """What one rule looks at under roots (reads: trace key -> _state before the rule wrote it) and writes."""
    def __init__(self, roots: dict[str, Path]):
        self.roots = _roots(roots)
        self.order = sorted(self.roots.items(), key=lambda r: -len(r[1]))  # the deepest root a path is under
        self.reads: dict[str, str | None] = {}
        self.written: set[str] = set()
        self.created: set[str] = set()  # paths the rule made: absent before it ran, so left out of its listings
        self.busy = False

    def rel(self, path) -> str | None:
        """"<root>:<path>" for a traced path, else None."""
        if isinstance(path, int) or path is None:  # a file descriptor: its open was traced
            return None
        p = os.path.normcase(os.path.abspath(os.fsdecode(path)))
        for tag, r in self.order:
            if p.startswith(r) or p + os.sep == r:
                rel = p[len(r):].replace(os.sep, "/")
                skip = tag == "p" and rel.split("/")[0] in NOT_TRACED or "__pycache__" in rel
                return None if skip else f"{tag}:{rel}"
        return None

    def see(self, path, kind: str = "f") -> None:
        rel = self.rel(path)
        if rel is None or rel in self.written or rel in self.created or f"{kind}:{rel}" in self.reads:
            return
        self.busy = True
        try:
            s = _state(self.roots, f"{kind}:{rel}")
            if kind == "l" and s:  # as it was before: without what the rule added
                s = "|".join(n for n in s.split("|") if f"{rel}/{n.rstrip('/')}" not in self.created)
            self.reads[f"{kind}:{rel}"] = s
        finally:
            self.busy = False

    def wrote(self, path, made: bool = False) -> None:
        """path is written (made: a folder made) by the rule; one that did not exist yet is created."""
        if (rel := self.rel(path)) is None or rel in self.written or rel in self.created:
            return
        tag, p = rel.split(":", 1)
        try:
            _stat(os.path.join(self.roots[tag], p))
        except OSError:
            self.created.add(rel)
            return
        if not made:
            self.written.add(rel)


def _audit(event: str, args: tuple) -> None:
    t = _trace
    if t is None or t.busy:
        return
    if event == "open":
        path, mode, flags = args
        if mode is None:  # os.open
            rd, wr, keeps = not flags & os.O_WRONLY, bool(flags & (os.O_WRONLY | os.O_RDWR)), not flags & os.O_TRUNC
        else:
            rd, wr, keeps = "r" in mode or "+" in mode, any(c in mode for c in "wax+"), "w" not in mode
        if rd or (wr and keeps):
            t.see(path)
        if wr:
            t.wrote(path)
    elif event in ("os.listdir", "os.scandir"):
        t.see(args[0] if args[0] is not None else ".", "l")
    elif event in ("os.remove", "os.rmdir", "os.rename", "shutil.copyfile", "os.mkdir"):
        t.see(args[0])  # what it was before, then written (a folder: made)
        if event == "os.mkdir":
            t.wrote(args[0], made=True)
        else:
            t.wrote(args[1] if event in ("os.rename", "shutil.copyfile") else args[0])
            if event == "os.rename":
                t.wrote(args[0])


def _seen(fn: Callable) -> Callable:
    """fn (an os stat or os.path test) recording the path it looks at."""
    def f(path, *a, **k):
        if (t := _trace) is not None and not t.busy:
            t.see(path)
        return fn(path, *a, **k)
    return f


_stat, _isdir = os.stat, os.path.isdir
_hooked = False


def _hook() -> None:
    """The audit hook (opens, listings, removes) and the stat wrappers, once per process; idle while no rule runs."""
    global _hooked
    if not _hooked:
        sys.addaudithook(_audit)
        for mod, names in ((os, ("stat", "lstat")), (os.path, ("exists", "lexists", "isfile", "isdir", "islink",
                                                                  "isjunction", "getsize", "getmtime"))):
            for n in names:
                if hasattr(mod, n):
                    setattr(mod, n, _seen(getattr(mod, n)))
        if hasattr(_winapi, "CopyFile2"):  # shutil.copy2's copy on Windows raises no audit event
            copy = _winapi.CopyFile2

            def copied(src, dst, *a):
                if (t := _trace) is not None and not t.busy:
                    t.see(src)
                    t.wrote(dst)
                return copy(src, dst, *a)
            _winapi.CopyFile2 = copied
        _hooked = True


def _snapshot(gen: Path) -> tuple[dict[str, tuple[int, int]], set[str]]:
    """gen's files (size, modified time) and folders, by path from gen."""
    files, dirs = {}, set()
    for top, ds, fs in os.walk(gen):
        rel = Path(top).relative_to(gen)
        dirs.update((rel / d).as_posix() for d in ds)
        for f in fs:
            st = _stat(os.path.join(top, f))
            files[(rel / f).as_posix()] = (st.st_size, st.st_mtime_ns)
    return files, dirs


class _Tee(io.TextIOBase):
    def __init__(self, *outs):
        self.outs = outs

    def write(self, s: str) -> int:
        for o in self.outs:
            o.write(s)
        return len(s)

    def flush(self) -> None:
        for o in self.outs:
            o.flush()


def rule(cache: Path, key_: str, roots: dict[str, Path], run: Callable[[], None]) -> bool:
    """run() a merge rule that writes under the project (roots["p"]) folder generated/, or replay a cached run of it
    with this key that saw what roots (the project, and D2R's tables as "d") hold now. A miss is kept. True on a hit."""
    gen, home, top = roots["p"] / "generated", cache / key_, _roots(roots)
    for e in sorted(home.iterdir()) if home.is_dir() else []:
        try:
            m = json.loads((e / "manifest.json").read_text("utf-8"))
            if all(_state(top, k) == v for k, v in m["reads"].items()):
                used.append(e)
                os.utime(e)  # recently used: pruned last, and not while this replay copies it
                for d in m["dirs"]:
                    (gen / d).mkdir(parents=True, exist_ok=True)
                for f in m["files"]:
                    (gen / f).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(e / "files" / f, gen / f)
                for f in m["removed"]:
                    (gen / f).unlink(missing_ok=True)
                for d in sorted(m["rmdirs"], reverse=True):
                    (gen / d).rmdir()
                sys.stdout.write(m["stdout"])
                return True
        except OSError:  # being kept or pruned by another run (its tmp folder has no manifest): a run of the rule
            continue  # rewrites whatever a part replay copied, as it writes the same files
    global _trace
    _hook()
    files0, dirs0 = _snapshot(gen)
    out, _trace = io.StringIO(), Trace(roots)
    try:
        with contextlib.redirect_stdout(_Tee(sys.stdout, out)):
            run()
    finally:
        t, _trace = _trace, None
    # a write outside generated/ (pd2_art's new merge/art PNGs) is not replayed, so such a run is not kept
    outside = sorted(r for r in t.written | t.created if not (r + "/").startswith("p:generated/"))
    if outside:
        print(f"rule not cached: it wrote outside generated/ ({', '.join(outside[:3])})")
        return False
    files1, dirs1 = _snapshot(gen)
    m = {"reads": t.reads, "files": sorted(f for f in files1 if files0.get(f) != files1[f]),
         "removed": sorted(set(files0) - set(files1)), "dirs": sorted(dirs1 - dirs0), "rmdirs": sorted(dirs0 - dirs1),
         "stdout": out.getvalue()}
    m["bytes"] = sum(files1[f][0] for f in m["files"])
    name = hashlib.sha256(json.dumps(m["reads"], sort_keys=True).encode()).hexdigest()[:16]
    tmp = home / f"{name}.tmp{os.getpid()}"
    try:
        tmp.mkdir(parents=True)
        for f in m["files"]:
            (tmp / "files" / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(gen / f, tmp / "files" / f)
        (tmp / "manifest.json").write_text(json.dumps(m), "utf-8")
        os.rename(tmp, home / name)
        used.append(home / name)
    except OSError as e:  # FileExistsError: another run kept the same reads; anything else only costs a later hit
        if isinstance(e, FileExistsError):
            used.append(home / name)
        else:
            print(f"warning: rule not cached ({e})")
    finally:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)
    return False


def _delete(e: Path) -> None:
    """Remove entry e atomically: rename it away first, so a replay of e sees it whole or gets an OSError (and runs
    the rule), never half of it; then delete the renamed folder."""
    dead = e.with_name(f"{e.name}.dead.{os.getpid()}")
    try:
        os.rename(e, dead)
    except OSError:  # gone already (another run deleted it): nothing to do
        return
    shutil.rmtree(dead, ignore_errors=True)


def touches(e: Path, prefix: str) -> bool:
    """Whether entry e writes a file under prefix (a path from generated/, e.g. "data/global/excel/")."""
    try:
        return any(f.startswith(prefix) for f in json.loads((e / "manifest.json").read_text("utf-8"))["files"])
    except (OSError, ValueError, KeyError):
        return False


def evict(entries: list[Path], touching: str | None = None) -> None:
    """Delete rule entries (those in used) whose output a check found wrong, so the next run rebuilds and keeps a good
    one. With touching, only those that write under that path: the check looked at nothing else."""
    for e in entries:
        if touching is None or touches(e, touching):
            _delete(e)


def size(p: Path) -> int:
    return sum(f.stat().st_size for f in p.rglob("*") if f.is_file())


def prune(cache: Path, cap: int, keep_s: float = 900, legacy: bool = False) -> list[Path]:
    """Delete the least recently used rule entries (with legacy, the whole-merge folders older tools kept in merge/
    too) until the rest fit in cap bytes. One used in the last keep_s seconds stays: a run may be replaying it. merge/
    is left alone by default: an older tool copies a folder there without marking it used."""
    for e in cache.glob("rules/*/*"):  # leftovers of a run killed mid-keep or mid-delete
        if (".dead." in e.name or ".tmp" in e.name) and time.time() - e.stat().st_mtime > keep_s:
            shutil.rmtree(e, ignore_errors=True)
    entries = []
    for e in [*cache.glob("rules/*/*"), *(cache.glob("merge/*") if legacy else [])]:
        if not e.is_dir() or ".tmp" in e.name or ".dead." in e.name:
            continue
        try:
            m = e / "manifest.json"
            entries.append((e.stat().st_mtime, json.loads(m.read_text("utf-8"))["bytes"] if m.is_file() else size(e), e))
        except (OSError, ValueError, KeyError):
            continue
    total, gone = sum(b for _, b, _ in entries), []
    for mtime, b, e in sorted(entries, key=lambda x: x[0]):
        if total <= cap or time.time() - mtime < keep_s:
            break
        _delete(e)
        total -= b
        gone.append(e)
    return gone

