"""Fetch the pinned companion plugins and install them with the mod (issue #15).

    python tools/companions.py fetch
    python tools/companions.py install --game DIR

fetch downloads each release asset in companions/manifest.json (or, for an entry with `commit` in place of `tag`, the
file at that path in the repo at that commit; a release asset that is not a .zip is one file too), refuses one whose SHA-256 differs from the manifest, and unpacks only
the files the manifest lists into git-ignored generated/companions/ (laid out as the installed mod
folder), plus our companions/filter.json and the plugin settings in companions/config/ (over the zip's own). No
companion binary is ever committed. Run it after pd2_merge.py, which rebuilds generated/.

install runs `modkit install` and then copies generated/companions/ over <game>/mods/<Name>/, and turns on
`[d2rcore.items] item_stat_ranges` for Item Roll Ranges. It overwrites filter.json; other loader settings are kept.
The loot filter refuses filter.json unless the mod folder holds the item tables it reads (FILTER_TABLES), so install
copies any the build didn't ship from the game's own files.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import shutil
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parent.parent
ITEM_RANGES = "[d2rcore.items]\nitem_stat_ranges = true\n"
# what pyre-lootfilter reads from <mod>.mpq/data/global/excel/ (it logs excel-directory-not-found without them)
FILTER_TABLES = ["armor", "weapons", "misc", "itemtypes", "skills", "itemstatcost"]


class CompanionError(Exception):
    pass


def github_download(repo: str, ref: str, asset: str, raw: bool = False) -> bytes:
    """A release asset of tag `ref`, or with raw the file at path `asset` in the repo at commit `ref`."""
    url = (f"https://raw.githubusercontent.com/{repo}/{ref}/{urllib.parse.quote(asset)}" if raw else
           f"https://github.com/{repo}/releases/download/{urllib.parse.quote(ref)}/{urllib.parse.quote(asset)}")
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def fetch(root: Path, download: Callable[..., bytes] = github_download) -> list[str]:
    """Rebuild root/generated/companions/ and return the files written (mod-folder relative)."""
    manifest = json.loads((root / "companions" / "manifest.json").read_text("utf-8"))
    dest = root / "generated" / "companions"
    if dest.exists():
        shutil.rmtree(dest)
    written = []
    for c in manifest["companions"]:
        raw = "commit" in c
        data = download(c["repo"], c["commit"], c["asset"], True) if raw else download(c["repo"], c["tag"], c["asset"])
        got = hashlib.sha256(data).hexdigest()
        if got != c["sha256"]:
            shutil.rmtree(dest, ignore_errors=True)
            raise CompanionError(f"{c['asset']}: SHA-256 {got} differs from the manifest")
        if raw or not c["asset"].endswith(".zip"):  # one file: files maps the asset path to its place in the mod folder
            rel = c["files"][c["asset"]]
            (dest / rel).parent.mkdir(parents=True, exist_ok=True)
            (dest / rel).write_bytes(data)
            written.append(rel)
            continue
        zf = zipfile.ZipFile(io.BytesIO(data))
        names = [n for n in zf.namelist() if not n.endswith("/")]
        for src, dst in c["files"].items():
            # a key ending in / copies the folder's files, else one file
            picked = [n for n in names if n.startswith(src)] if src.endswith("/") else [n for n in names if n == src]
            if not picked:
                shutil.rmtree(dest, ignore_errors=True)
                raise CompanionError(f"{c['asset']}: no {src} in the zip")
            for n in picked:
                rel = dst + n[len(src):] if src.endswith("/") else dst
                (dest / rel).parent.mkdir(parents=True, exist_ok=True)
                (dest / rel).write_bytes(zf.read(n))
                written.append(rel)
    config = dest / "d2rloader" / "config"
    config.mkdir(parents=True, exist_ok=True)
    # our settings for the companions, over any default a zip put there
    for f in [root / "companions" / "filter.json", *sorted((root / "companions" / "config").glob("*"))]:
        shutil.copy2(f, config / f.name)
        written.append(f"d2rloader/config/{f.name}")
    return list(dict.fromkeys(written))


def overlay(root: Path, mod_dir: Path) -> None:
    """Copy generated/companions/ over an installed mod folder and turn on item stat ranges."""
    src = root / "generated" / "companions"
    if not src.is_dir():
        raise CompanionError(f"{src} missing: run tools/companions.py fetch")
    shutil.copytree(src, mod_dir, dirs_exist_ok=True)
    toml = mod_dir / "d2rloader" / "config" / "d2rloader.toml"
    text = toml.read_text("utf-8") if toml.exists() else ""
    if re.search(r"^\s*item_stat_ranges\s*=", text, re.M):
        return  # set already, by us or by the player
    if re.search(r"^\[d2rcore\.items\]", text, re.M):  # the loader writes this section with the key commented out
        text = re.sub(r"^\[d2rcore\.items\]\n", ITEM_RANGES, text, count=1, flags=re.M)
    else:
        text += ("\n" if text and not text.endswith("\n") else "") + ITEM_RANGES
    toml.write_text(text, encoding="utf-8")


def fill_tables(excel: Path, read: Callable[[str], bytes]) -> list[str]:
    """Copy each FILTER_TABLES table missing from excel/ from read("data/global/excel/<t>.txt"); return those copied."""
    missing = [t for t in FILTER_TABLES if not (excel / f"{t}.txt").exists()]
    excel.mkdir(parents=True, exist_ok=True)
    for t in missing:
        (excel / f"{t}.txt").write_bytes(read(f"data/global/excel/{t}.txt"))
    return missing


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["fetch", "install"])
    ap.add_argument("--game", type=Path, help="D2R folder for install")
    ap.add_argument("--root", type=Path, default=ROOT, help="project folder (default: this repo)")
    a = ap.parse_args(argv)
    try:
        if a.command == "fetch":
            print(f"generated/companions rebuilt: {len(fetch(a.root))} files")
        else:
            if not a.game:
                ap.error("install needs --game")
            from modkit.install import install  # the sibling d2rAI-toolkit on PYTHONPATH
            print(install(a.root, a.game))
            name = json.loads((a.root / "mod.json").read_text("utf-8"))["name"]
            overlay(a.root, a.game / "mods" / name)
            from modkit.base import Base
            filled = fill_tables(a.game / "mods" / name / f"{name}.mpq" / "data" / "global" / "excel", Base(a.game).bytes)
            if filled:
                print(f"vanilla tables added for the loot filter: {', '.join(filled)}")
            print(f"companions copied into {a.game / 'mods' / name}")
    except CompanionError as e:
        print(f"companions: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
