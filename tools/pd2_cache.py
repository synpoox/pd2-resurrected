"""Machine-wide caches every worktree shares (#440), so ten agents don't each extract, merge and copy the same data.

The cache root is PD2R_CACHE (default C:\\pd2r-cache). Without that folder, or on CI, every tool works per worktree as
before. Under the root:
  pd2_extract.lock     held by every pd2_extract run, so two extracts never run at once
  extract/<key>/       pd2_extract's output, one per extract stamp; a worktree's pd2-data/ is a junction to it
  merge/<key>/         pd2_merge's generated/, one per hash of merge/, tools/, the stamps and modkit
A cache folder is complete before it gets its name and the tools never delete one, so no run sees another's half-written
or removed cache. Delete old ones by hand when no worktree's pd2-data/ points at them.
"""
from __future__ import annotations

import contextlib
import hashlib
import os
import shutil
import subprocess
import time
from pathlib import Path

try:
    import msvcrt
except ImportError:  # not Windows
    msvcrt = None
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
    with open(path, "a+b") as f:
        waited = False
        while True:
            try:
                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if not waited:
                    print(f"waiting for {path}: another run holds it", flush=True)
                    waited = True
                time.sleep(poll)
        try:
            yield
        finally:
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1) if msvcrt else fcntl.flock(f, fcntl.LOCK_UN)


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


def reuse(cached: Path, dest: Path) -> bool:
    """Replace dest with a copy of the cached folder when there is one."""
    if not cached.is_dir():
        return False
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(cached, dest)
    return True


def keep(src: Path, cached: Path) -> None:
    """Copy src into the cache as cached, unless another run got there first. The copy only gets its name complete."""
    tmp = cached.with_name(f"{cached.name}.tmp{os.getpid()}")
    try:
        shutil.copytree(src, tmp)
        os.rename(tmp, cached)
    except OSError as e:  # FileExistsError: another run kept the same key; anything else only costs a later hit
        if not isinstance(e, FileExistsError):
            print(f"warning: not cached ({e})")
    finally:
        if tmp.exists():
            shutil.rmtree(tmp)

