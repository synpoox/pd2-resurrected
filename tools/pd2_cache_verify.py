"""Compare two merged trees byte for byte (SHA-256 per file) and fail loudly, naming every file that differs (#849).

The nightly cache-verify workflow merges a commit fresh (no rule cache) and again replaying the shared rule cache
(#820), then runs `pd2_cache_verify.py <fresh generated/> <cached generated/>`. The replay is only as correct as the
rule key, so a key gap (#844) shows up here as a file that differs. Exit 0: identical; 1: differ."""
import argparse
import hashlib
import sys
from pathlib import Path


def digests(root: Path) -> dict[str, str]:
    """relative posix path -> SHA-256 of every file under root."""
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def diff(fresh: dict[str, str], cached: dict[str, str]) -> list[str]:
    """One line per file that differs, is missing from the cached tree, or only the cached tree has."""
    out = []
    for f in sorted(fresh.keys() | cached.keys()):
        if f not in cached:
            out.append(f"{f}: only in the fresh merge")
        elif f not in fresh:
            out.append(f"{f}: only in the cached merge")
        elif fresh[f] != cached[f]:
            out.append(f"{f}: fresh {fresh[f][:12]} != cached {cached[f][:12]}")
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fresh", type=Path)
    ap.add_argument("cached", type=Path)
    a = ap.parse_args(argv)
    for d in (a.fresh, a.cached):
        if not d.is_dir():
            print(f"pd2_cache_verify: no folder {d}", file=sys.stderr)
            return 1
    fresh, cached = digests(a.fresh), digests(a.cached)
    if not fresh:
        print("pd2_cache_verify: the fresh merge is empty", file=sys.stderr)
        return 1
    bad = diff(fresh, cached)
    if bad:
        print(f"::error::cached merge differs from fresh merge in {len(bad)} file(s): the rule cache key misses an input")
        for line in bad:
            print(line)
        return 1
    print(f"cached merge identical to fresh merge: {len(fresh)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
