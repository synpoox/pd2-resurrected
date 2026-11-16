"""Install, upgrade or roll back a release by its manifest, the way the launcher's Update does (issue #966).

    python tools/release_rehearsal.py apply  --src DIR --manifest M [--old-manifest OLD] --mod-dir DIR
                                             [--global-plugins DIR] [--savepath NAME/]
    python tools/release_rehearsal.py verify --mod-dir DIR --manifest M [--other-manifest OTHER] [--global-plugins DIR]

`apply` puts the package's mod folder (--src, the unpacked 7z's PD2Resurrected/) over --mod-dir: a `managed` file is
replaced when its hash differs, a `shipped_default` file only when missing or still the old release's default (the
player's edit stays), a file the old release owned and this one does not is deleted, and any plugin file either release
shipped is removed from the other plugin scope (--global-plugins, <game>/d2rloader/plugins) so one backend loads.
Files in neither manifest are never touched (saves live outside --mod-dir). --savepath rewrites both modinfo.json.
Rolling back is `apply` with the old release as --manifest/--src and the newer one as --old-manifest.
`verify` lists a managed file that is missing or differs, a file only OTHER owns that is still there, and plugin
files left in the other scope (modinfo.json is skipped: the slot's own savepath differs from the package's).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

PLUGINS = "d2rloader/plugins/"


def owned(manifest: Path | None) -> dict:
    return {f["path"]: f for f in json.loads(manifest.read_text("utf-8"))["files"]} if manifest else {}


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def inside(mod_dir: Path, rel: str) -> Path:
    """mod_dir/rel, refused when a manifest path would leave mod_dir (`..`, an absolute path)."""
    p = (mod_dir / rel).resolve()
    if not p.is_relative_to(mod_dir.resolve()):
        raise ValueError(f"manifest path {rel!r} leaves {mod_dir}")
    return mod_dir / rel


def other_scope(new: dict, old: dict) -> set[str]:
    return {Path(r).name for r in (*new, *old) if r.startswith(PLUGINS)}


def apply(src: Path, mod_dir: Path, new: dict, old: dict, global_plugins: Path | None = None,
          savepath: str | None = None) -> dict:
    n = {"written": 0, "kept": 0, "deleted": 0, "other_scope_removed": 0}
    for rel in (*new, *old):
        inside(mod_dir, rel)  # before anything is written
    for rel, f in new.items():
        dst = mod_dir / rel
        edited = f["ownership"] == "shipped_default" and dst.exists() and not (rel in old and sha(dst) == old[rel]["sha256"])
        if edited or (dst.exists() and sha(dst) == f["sha256"]):
            n["kept"] += 1
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_name(dst.name + ".new")
        shutil.copy2(src / rel, tmp)
        os.replace(tmp, dst)  # a new file: a hard-linked snapshot of the old one stays intact
        n["written"] += 1
    for rel, f in old.items():
        p = mod_dir / rel
        if rel not in new and p.is_file() and (f["ownership"] == "managed" or sha(p) == f["sha256"]):
            p.unlink()
            n["deleted"] += 1
            for d in p.parents:  # prune folders the old release emptied
                if d == mod_dir or any(d.iterdir()):
                    break
                d.rmdir()
    if global_plugins:
        for name in other_scope(new, old):
            if (global_plugins / name).is_file():
                (global_plugins / name).unlink()
                n["other_scope_removed"] += 1
    if savepath:
        for f in (mod_dir / "modinfo.json", mod_dir / f"{mod_dir.name}.mpq" / "modinfo.json"):
            info = json.loads(f.read_text("utf-8"))
            info["savepath"] = savepath
            f.write_text(json.dumps(info, indent=2), "utf-8")
    return n


def verify(mod_dir: Path, new: dict, other: dict, global_plugins: Path | None = None) -> list[str]:
    bad = []
    for rel, f in new.items():
        p = mod_dir / rel
        if rel.endswith("modinfo.json"):
            continue
        if not p.is_file():
            bad.append(f"missing {rel}")
        elif f["ownership"] == "managed" and sha(p) != f["sha256"]:
            bad.append(f"differs {rel}")
    bad += [f"stale {rel}"
            for rel, f in other.items()
            if rel not in new and (mod_dir / rel).exists()
            and not (f["ownership"] == "shipped_default" and sha(mod_dir / rel) != f["sha256"])]  # a player's edit stays
    if global_plugins:
        bad += [f"other scope {n}" for n in other_scope(new, other) if (global_plugins / n).exists()]
    return bad


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["apply", "verify"])
    ap.add_argument("--src", type=Path)
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--old-manifest", "--other-manifest", dest="old", type=Path)
    ap.add_argument("--mod-dir", type=Path, required=True)
    ap.add_argument("--global-plugins", type=Path)
    ap.add_argument("--savepath")
    a = ap.parse_args(argv)
    new, old = owned(a.manifest), owned(a.old)
    if a.command == "apply":
        if not a.src:
            ap.error("apply needs --src")
        print(apply(a.src, a.mod_dir, new, old, a.global_plugins, a.savepath))
        return 0
    bad = verify(a.mod_dir, new, old, a.global_plugins)
    print(f"verify: {len(new)} files in the manifest, {len(bad)} problems")
    for b in bad[:20]:
        print("  " + b)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
