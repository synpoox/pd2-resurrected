"""Machine-wide caches every worktree shares (#440), so ten agents don't each extract, merge and copy the same data.

The cache root is PD2R_CACHE (default C:\\pd2r-cache). Without that folder, or on CI, every tool works per worktree as
before. Under the root:
  pd2_extract.lock     held by every pd2_extract run, so two extracts never run at once
  extract/<key>/       pd2_extract's output, one per extract stamp; a worktree's pd2-data/ is a junction to it
  rules/<key>/<reads>/ what one pd2_merge rule wrote, for its code and stamps and what it read (rule, below)
  rules-pr/pr-<n>/...  the same for one PR's CI gate, which keeps only there (Policy, below); pruned with rules/
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
import re
from pathlib import Path
from typing import Callable, NamedTuple

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
def lock(path: Path, poll: float = 1.0, on_wait: Callable[[], None] | None = None):
    """Hold an OS lock on the file path, waiting while another process holds it. The OS drops the lock when its holder
    exits, so a crashed run never leaves a stale one. on_wait() runs once, when the first try finds it held."""
    path.parent.mkdir(parents=True, exist_ok=True)
    # create and open in one step: a sweep that deletes idle lock files cannot slip in between a touch and the open
    with os.fdopen(os.open(path, os.O_RDWR | os.O_CREAT | getattr(os, "O_BINARY", 0)), "r+b") as f:
        misses, shown = 0, None
        while True:
            try:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if on_wait and not misses:
                    on_wait()
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


# Bump when what a cache entry stores or how replay works changes: it is in every rule's key (pd2_merge.base_key), so
# editing this file's code otherwise leaves the cache alone.
CACHE_FORMAT = 1


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


def module_files(home: Path, module: str, imported: list[str]) -> list[Path]:
    """The files under home that `import module` (dotted, "" for the home itself) or `from module import imported`
    runs: the module's .py or package __init__.py, and each imported name that is a submodule of it (#844)."""
    d = home.joinpath(*module.split(".")) if module else home
    own = [d.with_suffix(".py"), d / "__init__.py"] if module else [d / "__init__.py"]
    found = [f for f in own if f.is_file()]
    return found + [f for a in imported for f in (d / f"{a}.py", d / a / "__init__.py") if f.is_file()]


def sources(fn: Callable, leave_out: tuple[Path, ...] = ()) -> list[Path]:
    """fn's module file and every module beside it that it imports, at any depth (a rule's code). A module inside a
    package brings the whole package (every .py under its top folder): relative imports and passes found by discovery
    are code the rule runs too (#754). An import in a module's main() is left out: a merge never runs a tool's command
    line (pd2_strings' imports pd2_merge, which would put every tool in every key). leave_out: files or folders that are
    neither listed nor followed (code the caller knows its fn never runs, #870)."""
    todo, seen = [Path(sys.modules[fn.__module__].__file__)], set()
    skip = [os.path.normcase(os.path.abspath(x)) for x in leave_out]

    def left_out(q: Path) -> bool:
        n = os.path.normcase(os.path.abspath(q))
        return any(n == x or n.startswith(x + os.sep) for x in skip)

    def package(d: Path) -> Path | None:  # the top folder of the package d is in, or None
        top = None
        while (d / "__init__.py").is_file():
            top, d = d, d.parent
        return top

    while todo:
        p = todo.pop()
        if p in seen or left_out(p):
            continue
        seen.add(p)
        top = package(p.parent)
        if top is not None:
            todo += [q for q in top.rglob("*.py") if "__pycache__" not in q.parts]
        homes = {p.parent, top.parent} if top else {p.parent}
        tree = ast.parse(p.read_text("utf-8"))
        cli = {id(n) for d in tree.body if isinstance(d, ast.FunctionDef) and d.name == "main" for n in ast.walk(d)}
        for n in ast.walk(tree):
            if id(n) in cli or not isinstance(n, (ast.Import, ast.ImportFrom)):
                continue
            if isinstance(n, ast.Import):
                bases = [(h, a.name, []) for a in n.names for h in homes]
            elif n.level:  # relative: from the folder of p, one level up for each dot after the first
                d = p.parent
                for _ in range(n.level - 1):
                    d = d.parent
                bases = [(d, n.module or "", [a.name for a in n.names])]
            else:
                bases = [(h, n.module, [a.name for a in n.names]) for h in homes]
            for h, m, imported in bases:
                for q in module_files(h, m, imported):
                    todo.append(q)
    return sorted(seen)


def toolkit_sources(kit: Path, files: list[Path]) -> list[Path]:
    """The d2rAI-toolkit modules (files under kit) that files import, at any depth: each module's .py or package
    __init__.py, every parent package's __init__.py, and what those import in turn (#808). A rule's key takes these, not
    the whole toolkit, so a toolkit commit rebuilds only the rules that use a module it changed. Nothing is skipped
    that might run: an import anywhere in a toolkit module counts (a tools/ module's main() does not, as in sources()),
    and a name that is a submodule of the imported package counts as imported."""
    todo, seen, found = list(files), set(), set()
    kit = kit.resolve()
    while todo:
        p = todo.pop()
        if p in seen or not p.is_file():
            continue
        seen.add(p)
        inside = kit in p.resolve().parents
        if inside:
            found.add(p.resolve())
        tree = ast.parse(p.read_text("utf-8"))
        cli = {id(n) for d in tree.body if not inside and isinstance(d, ast.FunctionDef) and d.name == "main"
               for n in ast.walk(d)}
        for n in ast.walk(tree):
            if id(n) in cli or not isinstance(n, (ast.Import, ast.ImportFrom)):
                continue
            if isinstance(n, ast.Import):
                bases = [(kit, a.name, []) for a in n.names]
            elif n.level:  # relative, from a toolkit module only
                if not inside:
                    continue
                d = p.resolve().parent
                for _ in range(n.level - 1):
                    d = d.parent
                bases = [(d, n.module or "", [a.name for a in n.names])]
            else:
                bases = [(kit, n.module, [a.name for a in n.names])]
            for h, m, imported in bases:
                for q in module_files(h, m, imported):
                    q = q.resolve()
                    if kit not in q.parents:
                        continue
                    for d in q.parents:  # the packages it sits in run their __init__ first
                        if d == kit:
                            break
                        if (d / "__init__.py").is_file():
                            found.add(d / "__init__.py")
                            todo.append(d / "__init__.py")
                    found.add(q)
                    todo.append(q)
    return sorted(found)


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


class Policy(NamedTuple):
    """Who may replay and keep rule entries (#820). The default is the agents' local use: replay any entry under the
    rules folder, keep new ones there.
      origin   written into each kept entry's manifest, so a reader can tell who made it
      trusted  the origins an entry under the rules folder must have to be replayed (None: any, even an entry from before
               origins). CI replays only what a push to main kept: a PR's gate keeps in its own scratch folder, so a
               buggy or half-run PR merge cannot damage the entries main replays. PR code of the same repository runs
               as the runner's user and is trusted as far as the job's same-repo `if:` allows; this contains accidents,
               it is not a sandbox against a malicious same-repo author
      scratch  a folder (per PR) also read, and where a miss is kept instead of the rules folder
      write    False: keep nothing (a CI run without a known scope)"""
    origin: str = "local"
    trusted: frozenset | None = None
    scratch: Path | None = None
    write: bool = True


CI_SCOPE = re.compile(r"main|pr-\d+")


def ci_policy(home: Path) -> "Policy | None":
    """The policy of a CI run, from PD2R_CI_SCOPE (gate.yml sets it): "main" for a push to main, where the code is
    reviewed, replays only entries main kept and keeps into the shared rules folder; "pr-<n>" for a PR, which replays
    main's entries and its own scratch folder home/rules-pr/pr-<n> and keeps only there. None (no or odd scope, as in
    release.yml): no cache, merge fresh."""
    scope = os.environ.get("PD2R_CI_SCOPE", "")
    if not CI_SCOPE.fullmatch(scope):
        return None
    if scope == "main":
        return Policy("main", frozenset({"main"}))
    return Policy("pr", frozenset({"main"}), home / "rules-pr" / scope)


def rule(cache: Path, key_: str, roots: dict[str, Path], run: Callable[[], None], policy: Policy = Policy(),
         slots: "Slots | None" = None) -> bool:
    """run() a merge rule that writes under the project (roots["p"]) folder generated/, or replay a cached run of it
    with this key that saw what roots (the project, and D2R's tables as "d") hold now. A miss is kept (where policy
    says). True on a hit.

    Single-flight (#821): a miss takes an OS lock on <kept folder>.locks/<key>.<origin> (rules.locks/<key>.main, beside
    rules/ so no glob of the entries sees it; one lock per origin, as a main gate cannot replay an agent's entry and
    so must not wait for an agent's run), then looks again, so two merges that
    miss one key together run the rule once: the second waits and replays what the first kept. A holder that dies drops
    the lock (the OS does), and the waiter runs the rule itself. A waiter first gives up its slot (the machine-wide
    merge cap, run() takes it again when it needs one): a holder of the key lock may be waiting for a slot, so a
    merge that holds one must not block on a key lock (the order is key lock, then slot, and never the reverse)."""
    gen, top = roots["p"] / "generated", _roots(roots)
    dest = policy.scratch if policy.scratch else cache if policy.write else None
    home = dest / key_ if dest else None
    reads = [(cache / key_, policy.trusted)] + ([(policy.scratch / key_, None)] if policy.scratch else [])

    def replay() -> bool:
        entries = [(e, trusted) for folder, trusted in reads for e in (sorted(folder.iterdir()) if folder.is_dir() else [])]
        for e, trusted in entries:
            if ".tmp" in e.name or ".dead." in e.name:  # another run's keep or delete in
                continue  # progress: not an entry
            try:
                m = json.loads((e / "manifest.json").read_text("utf-8"))
                if trusted is not None and m.get("origin") not in trusted:
                    continue
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
            except (OSError, ValueError, KeyError):  # deleted or unreadable (OSError), a damaged manifest (the others): a
                continue  # run of the rule rewrites whatever a part replay copied, as it writes the same files
        return False

    if replay():
        return True
    if home is None:  # nothing is kept: no trace either, and no one to wait for
        run()
        return False
    waited = []

    def give_up_slot() -> None:
        waited.append(1)
        if slots:
            slots.release()  # run() takes one again when it needs it
    with lock(dest.with_name(dest.name + ".locks") / f"{key_}.{policy.origin}", on_wait=give_up_slot):  # beside rules/
        if replay():  # another run may have kept it between the first look and the lock
            if waited:
                print(f"rule {key_}: waited for another run of it and replayed what it kept", flush=True)
            return True
        return _build(gen, home, roots, run, policy)


def memo(cache: Path, key_: str, roots: dict[str, Path], compute: Callable[["Trace"], dict[str, bytes]],
         policy: Policy = Policy()) -> tuple[dict[str, bytes], bool]:
    """compute(trace) -> files (name -> bytes), or replay the files a kept run of it returned under this key that saw what
    roots hold now (the per-map scores of pd2_coverage, #854). Entries are data only (a manifest of JSON and the files
    beside it): nothing in one is run or unpickled on load (#870). Unlike rule(), nothing is hooked: compute reports every
    file it reads with trace.see(path), because the readers it calls keep their own memory (a DT1 read for one map is not
    read again for the next), so an audit of opens would miss them. Entries share rule()'s folders, manifests, origins and
    policy (a PR keeps only in its scratch folder), so prune, sweep and the PR scratch cleanup cover them.
    Single-flight, as rule(): a miss takes the key's lock (beside the kept folder, one per origin), looks again, and only
    then computes, so two jobs that miss together score once. (files, True) on a hit."""
    top = _roots(roots)
    dest = policy.scratch if policy.scratch else cache if policy.write else None
    reads = [(cache / key_, policy.trusted)] + ([(policy.scratch / key_, None)] if policy.scratch else [])

    def replay() -> dict[str, bytes] | None:
        for folder, trusted in reads:
            for e in sorted(folder.iterdir()) if folder.is_dir() else []:
                if ".tmp" in e.name or ".dead." in e.name:
                    continue
                try:
                    m = json.loads((e / "manifest.json").read_text("utf-8"))
                    if trusted is not None and m.get("origin") not in trusted:
                        continue
                    if all(_state(top, k) == v for k, v in m["reads"].items()):
                        data = {n: (e / "files" / n).read_bytes() for n in m["names"] if Path(n).name == n}
                        if len(data) != len(m["names"]):
                            continue
                        used.append(e)
                        os.utime(e)
                        return data
                except (OSError, ValueError, KeyError):
                    continue
        return None

    if (got := replay()) is not None:
        return got, True
    if dest is None:  # nothing is kept: no one to wait for
        return compute(Trace(roots)), False
    with lock(dest.with_name(dest.name + ".locks") / f"{key_}.{policy.origin}"):
        if (got := replay()) is not None:  # another run may have kept it between the first look and the lock
            return got, True
        t = Trace(roots)
        data = compute(t)
        _keep(dest / key_, t, data, policy)
        return data, False


def _keep(home: Path, t: "Trace", data: dict[str, bytes], policy: Policy) -> None:
    """Keep a memo run in home, whole or not at all."""
    if any(Path(n).name != n or n.startswith(".") for n in data):
        raise ValueError(f"memo file names must be plain: {sorted(data)}")
    m = {"reads": t.reads, "files": [], "removed": [], "dirs": [], "rmdirs": [], "stdout": "", "origin": policy.origin,
         "names": sorted(data), "bytes": sum(map(len, data.values()))}
    name = hashlib.sha256(json.dumps(m["reads"], sort_keys=True).encode()
                          + (b"" if policy.origin == "local" else policy.origin.encode())).hexdigest()[:16]
    tmp = home / f"{name}.tmp{os.getpid()}"
    try:
        (tmp / "files").mkdir(parents=True)
        for n, b in data.items():
            (tmp / "files" / n).write_bytes(b)
        (tmp / "manifest.tmp").write_text(json.dumps(m), "utf-8")
        os.replace(tmp / "manifest.tmp", tmp / "manifest.json")
        os.rename(tmp, home / name)
        used.append(home / name)
    except OSError as e:  # FileExistsError: another run kept the same reads; anything else only costs a later hit
        if not isinstance(e, FileExistsError):
            print(f"warning: result not cached ({e})")
    finally:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)


def _build(gen: Path, home: Path, roots: dict[str, Path], run: Callable[[], None], policy: Policy) -> bool:
    """Run a rule, tracing what it reads and writes, and keep it in home. False: a miss."""
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
         "stdout": out.getvalue(), "origin": policy.origin}
    m["bytes"] = sum(files1[f][0] for f in m["files"])
    # a local entry and a main one with the same reads sit side by side: main replays only its own (policy.trusted)
    name = hashlib.sha256(json.dumps(m["reads"], sort_keys=True).encode()
                          + (b"" if policy.origin == "local" else policy.origin.encode())).hexdigest()[:16]
    tmp = home / f"{name}.tmp{os.getpid()}"
    try:
        tmp.mkdir(parents=True)
        for f in m["files"]:
            (tmp / "files" / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(gen / f, tmp / "files" / f)
        (tmp / "manifest.tmp").write_text(json.dumps(m), "utf-8")
        os.replace(tmp / "manifest.tmp", tmp / "manifest.json")  # whole or absent, whatever reads this folder
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


def _delete(e: Path) -> bool:
    """Remove entry e atomically: rename it away first, so a replay of e sees it whole or gets an OSError (and runs
    the rule), never half of it; then delete the renamed folder. False if it is still there (in use by a replay)."""
    dead = e.with_name(f"{e.name}.dead.{os.getpid()}")
    try:
        os.rename(e, dead)
    except PermissionError:  # a replay has files of it open: it stays
        print(f"kept {e} (in use)")
        return False
    except OSError:  # gone already (another run deleted it): nothing to do
        return True
    shutil.rmtree(dead, ignore_errors=True)
    return True


def _newest(p: Path) -> float:
    """The newest modified time of folder p or anything in it (a deep copy into it does not touch p itself)."""
    t = p.stat().st_mtime
    for top, ds, fs in os.walk(p):
        for n in [*ds, *fs]:
            with contextlib.suppress(OSError):
                t = max(t, os.stat(os.path.join(top, n)).st_mtime)
    return t


def touches(e: Path, prefix: str) -> bool:
    """Whether entry e writes, removes or removes a folder under prefix (a path from generated/, e.g. "data/global/excel/")."""
    try:
        m = json.loads((e / "manifest.json").read_text("utf-8"))
        return any(f.startswith(prefix) for f in [*m["files"], *m["removed"], *m["rmdirs"]])
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


def own(entries: list[Path], policy: Policy) -> list[Path]:
    """The entries a run under policy may delete: with a scratch folder (a PR gate) only those in it, never the shared
    rules folder's, which main's entries live in."""
    return [e for e in entries if policy.scratch is None or policy.scratch in e.parents]


def sweep(cache: Path, stale_s: float = 3 * 86400, lock_s: float = 86400) -> None:
    """Housekeeping before pruning to the cap: delete per-PR scratch folders (rules-pr/pr-*) not used for stale_s seconds
    (the PR closed or went quiet), key folders left empty, and lock files (<root>/*.locks/*) unused for lock_s. A lock
    file that is open (a waiter has it) cannot be deleted on Windows, and one idle a day is not held."""
    now = time.time()
    for d in cache.glob("rules-pr/*"):
        try:
            if d.is_dir() and now - _newest(d) > stale_s:
                shutil.rmtree(d, ignore_errors=True)
        except OSError:
            continue
    for d in [*cache.glob("rules/*"), *cache.glob("rules-pr/*/*")]:
        if d.is_dir():
            with contextlib.suppress(OSError):
                d.rmdir()  # only if empty
    for f in [*cache.glob("rules.locks/*"), *cache.glob("rules-pr/*.locks/*")]:
        with contextlib.suppress(OSError):
            if f.is_file() and now - f.stat().st_mtime > lock_s:
                f.unlink()


def prune(cache: Path, cap: int, keep_s: float = 900, legacy: bool = False) -> list[Path]:
    """Delete the least recently used rule entries (with legacy, the whole-merge folders older tools kept in merge/
    too) until the rest fit in cap bytes. One used in the last keep_s seconds stays: a run may be replaying it. merge/
    is left alone by default: an older tool copies a folder there without marking it used."""
    sweep(cache)
    for e in [*cache.glob("rules/*/*"), *cache.glob("rules-pr/*/*/*")]:  # leftovers of a run killed mid-keep or mid-delete
        if ".dead." in e.name or ".tmp" in e.name:
            try:  # a folder another run is still filling or deleting has a recent file in it
                if time.time() - _newest(e) > keep_s:
                    shutil.rmtree(e, ignore_errors=True)
            except OSError:  # vanished meanwhile
                continue
    entries = []
    for e in [*cache.glob("rules/*/*"), *cache.glob("rules-pr/*/*/*"), *(cache.glob("merge/*") if legacy else [])]:
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
        if _delete(e):
            total -= b
            gone.append(e)
    return gone



class Slots:
    """A machine-wide counting semaphore (#760): n slot files under folder, each held by an OS lock, so a crashed holder
    frees its slot with no pid file to leak. acquire() blocks until one is free (one 'queued' line meanwhile) and is
    idempotent; release() frees it. Usable as a context manager.

    Of the n slots, the last `reserved` belong to the CI gate (#872): with reserved > 0, a holder that is not `ci` only
    tries slots 0..n-reserved-1 (at least one), so it can never hold a reserved one; a `ci` holder tries the reserved
    slots first, then the rest, so it keeps the agents' slots free when it can."""

    def __init__(self, folder: Path, n: int, what: str = "merge", poll: float = 1.0, reserved: int = 0, ci: bool = False):
        self.folder, self.n, self.what, self.poll = folder, max(1, n), what, poll
        self.ci = ci
        self.reserved = max(0, min(reserved, self.n - 1))
        self.f = None

    def _order(self) -> list[int]:
        """The slot indexes this holder may try, in order."""
        if self.ci:
            return list(range(self.n - 1, -1, -1))
        return list(range(self.n - self.reserved))

    def _try(self, i: int):
        p = self.folder / f"slot.{i}"
        p.touch()
        f = open(p, "r+b")
        try:
            msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            f.close()
            return None
        return f

    def acquire(self) -> None:
        if self.f:
            return
        self.folder.mkdir(parents=True, exist_ok=True)
        told = False
        while True:
            order = self._order()
            for i in order:
                if f := self._try(i):
                    self.f = f
                    return
            if not told:
                pool = len(order)
                note = "" if self.ci or not self.reserved else f", {self.reserved} more reserved for CI (PD2R_CI_RESERVED)"
                print(f"{self.what} queued: {pool}/{pool} slots busy (machine-wide cap, PD2R_MERGE_SLOTS{note}); waiting for one to free", file=sys.stderr, flush=True)
                told = True
            time.sleep(self.poll)

    def release(self) -> None:
        if self.f:
            f, self.f = self.f, None
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_UN)
            f.close()

    def __enter__(self):
        self.acquire()
        return self

    def __exit__(self, *exc):
        self.release()


def slot_home() -> Path | None:
    """The cache root a merge takes its slot under, on CI too (root() is None there, but the runner shares this machine
    with the agents, so the gate's merge counts against the same cap, #801). None without the folder."""
    p = Path(os.environ.get("PD2R_CACHE", r"C:\pd2r-cache"))
    return p if p.is_dir() else None


def merge_slots(cache: Path) -> Slots:
    """The merge semaphore under the cache root, shared by agents and the CI gate: PD2R_MERGE_SLOTS slots (default 4).
    The last PD2R_CI_RESERVED (default 2) are the gate's (#872): an agent merge (no GITHUB_ACTIONS) holds at most
    PD2R_MERGE_SLOTS - PD2R_CI_RESERVED slots (at least one), and CI may take any, so the gate never queues behind
    agents while a reserved slot is free. A queued merge says which pool it waits on. The slots stay per-file OS locks.
    Pick the count from RAM: (RAM left for merges) / (RAM of one merge). One merge held about 3.8 GB at one worker;
    parallel merge workers (#807, planned) may hold more each, so lower the count then, and raise it when
    a faster merge (#806) leaves RAM idle. The runners are user processes: set the same values in their `.env` or the user environment."""
    def env(name: str, default: int) -> int:
        try:
            return int(os.environ.get(name, str(default)))
        except ValueError:
            return default
    ci = bool(os.environ.get("GITHUB_ACTIONS"))
    return Slots(cache / "merge-slots", env("PD2R_MERGE_SLOTS", 4), "CI merge" if ci else "merge",
                 reserved=env("PD2R_CI_RESERVED", 2), ci=ci)
