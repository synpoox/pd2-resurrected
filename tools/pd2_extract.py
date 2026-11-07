"""Extract the PD2 baseline's data files into git-ignored pd2-data/ (ADR 0002).

    python tools/pd2_extract.py [--install DIR]

Refuses an install whose archives differ from pd2-baseline.json. Writes every file of pd2data.mpq, the skill
icon sheets, the item pictures, the missile DCC, the automap cels, animdata.d2, the DT1 tiles and the WAVs of pd2assets.mpq and the DS1 maps of pd2maps.mpq
(ADR 0002 amendment) to pd2-data/data/... (lower-case paths, forward slashes), then pd2-data/baseline.json last, the
baseline it was checked against plus EXTRACT, which pd2_merge reads to refuse a stale extract. With a pd2_cache root
and no --out, pd2-data/ is a junction to the cache's extract for this stamp, so worktrees share one; a run holds the
cache's lock, so extracts never run at once (#440).
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import shutil
import sys
from pathlib import Path

import pd2_cache
import pd2_ids

ROOT = Path(__file__).resolve().parent.parent
# archive -> the files taken from it (lower-case path patterns). shortcut: art only as the asset pipeline needs it
EXTRACT = {"pd2data.mpq": ["*"],
           "pd2assets.mpq": ["data/global/animdata.d2", "data/global/ui/spells/??skillicon.dc6",
                             "data/global/ui/spells/skillicon.dc6", "data/global/ui/automap/maximap*.dc6",
                             "data/global/items/*.dc6", "data/global/missiles/*.dcc",
                             "data/global/tiles/*.dt1", "data/global/sfx/*.wav"],
           "pd2maps.mpq": ["data/global/tiles/*.ds1"]}


def open_mpq(path: Path):
    """The archive, read by d2rAI-toolkit's MPQ reader (PYTHONPATH, else the nearest toolkit beside a parent folder).
    Imported here, not at the top, so the unit tests run on CI, which has no toolkit."""
    try:
        from shared.classic_import import mpq
    except ImportError:
        sys.path.insert(0, str(pd2_ids.toolkit(ROOT)))
        try:
            from shared.classic_import import mpq
        except ImportError:
            raise ValueError("d2rAI-toolkit's shared/classic_import not found: put the toolkit on PYTHONPATH") from None
    return mpq.Archive(path)


def stamp(baseline: bytes) -> bytes:
    """pd2-data/baseline.json: the baseline and the patterns, so pd2_merge also refuses an extract EXTRACT has outgrown
    (a pd2-data/ from before data/global/items/ silently lost every PD2 item picture, #351)."""
    return baseline + b"\n" + json.dumps(EXTRACT, sort_keys=True).encode()


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def check(install: Path, baseline: dict) -> list[str]:
    """Problems with the install, empty when every archive matches the baseline. DLLs only warn."""
    problems = []
    for name, want in baseline["archives"].items():
        p = install / name
        if not p.is_file():
            problems.append(f"{name}: missing from {install}")
        elif sha256(p) != want:
            problems.append(f"{name}: SHA-256 differs from pd2-baseline.json")
    for name, want in baseline.get("dlls", {}).items():
        p = install / name
        if p.is_file() and sha256(p) != want:
            print(f"warning: {name} differs from pd2-baseline.json (reference only)")
    return problems


def extract(install: Path, out: Path, baseline_file: Path) -> int:
    baseline = json.loads(baseline_file.read_text("utf-8"))
    problems = check(install, baseline)
    if problems:
        print("refusing: this install is not the PD2 baseline\n  " + "\n  ".join(problems)
              + "\nRe-baselining is a deliberate commit to pd2-baseline.json (ADR 0002).", file=sys.stderr)
        return 1
    done = out / "baseline.json"  # written last, so it only exists after a finished extract
    if done.is_file() and done.read_bytes() == stamp(baseline_file.read_bytes()):
        print(f"{out} is current, skipping extract")
        return 0
    if out.exists():
        shutil.rmtree(out)
    count = 0
    for archive, patterns in EXTRACT.items():
        a = open_mpq(install / archive)
        for name in a.names():
            rel = name.replace("\\", "/").lower()
            if not any(fnmatch.fnmatchcase(rel, p) for p in patterns):
                continue
            dest = out / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(a.read(name))
            count += 1
    (out / "baseline.json").write_bytes(stamp(baseline_file.read_bytes()))
    print(f"extracted {count} files to {out}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--install", type=Path, help="PD2 folder (default: the one named in pd2-baseline.json)")
    ap.add_argument("--out", type=Path, help="output folder (default: pd2-data/, a junction to the shared cache's "
                                                  "extract when pd2_cache has a cache root)")
    ap.add_argument("--baseline", type=Path, default=ROOT / "pd2-baseline.json")
    a = ap.parse_args(argv)
    install = a.install or Path(json.loads(a.baseline.read_text("utf-8"))["install"])
    out, cache = a.out or ROOT / "pd2-data", pd2_cache.root()
    with pd2_cache.lock((cache or out.parent) / "pd2_extract.lock"):
        if a.out or not cache or (out.exists() and not pd2_cache.is_link(out)):
            if not a.out and cache:
                print(f"{out} is a folder, not the shared extract: delete it to use {cache / 'extract'}")
            return extract(install, out, a.baseline)
        shared = cache / "extract" / hashlib.sha256(stamp(a.baseline.read_bytes())).hexdigest()[:16]
        rc = extract(install, shared, a.baseline)
        if rc == 0:
            pd2_cache.point(out, shared)
            print(f"{out} -> {shared}")
        return rc


if __name__ == "__main__":
    sys.exit(main())
