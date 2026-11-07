"""Package a built mod as a player release: a 7z, release notes and SHA256SUMS in dist/ (issue #109).

    python tools/release.py --game DIR [--version V] [--out dist]

Run it after the whole build in README.md (merge, companions fetch, modkit build, plugin\\build.ps1). It stages the
mod folder as an install lays it out (out/data, modinfo, loader metadata and config, the companions, our plugins in
d2rloader/plugins/, the vanilla tables the loot filter reads from DIR) in dist/mod/, then runs d2rAI-toolkit's
release/package_release.py with release.toml, and fills its notes: mod.json's description, install steps for the 7z
(no downloader yet), the changelog's `### <version>` section and the deviation list. The version defaults to
mod.json's; the changelog must have its section.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Callable

import pd2_ids
from companions import ROOT, fill_tables, overlay

PLUGINS = ["core-rules", "core-maps", "core-ui"]
DEVIATIONS = "https://github.com/synpoox/pd2-resurrected/blob/{commit}/docs/deviations.md"  # the list as built


class ReleaseError(Exception):
    pass


def notes_section(changelog: str, version: str) -> str:
    """The body of the changelog's `### <version> (<date>)` section."""
    m = re.search(rf"^### {re.escape(version)} \(.*?\)\n(.*?)(?=^### |\Z)", changelog, re.M | re.S)
    if not m:
        raise ReleaseError(f"CHANGELOG.md has no '### {version} (<date>)' section")
    return m.group(1).strip()


def stage(root: Path, out: Path, read: Callable[[str], bytes]) -> Path:
    """Lay out the installed mod folder in out/<name>/ (emptied first), as modkit install and companions install do."""
    mod = json.loads((root / "mod.json").read_text("utf-8"))
    name = mod["name"]
    dlls = [root / "plugin" / "build" / "Release" / f"{p}.dll" for p in PLUGINS]
    for p in (root / "out" / "data", root / "generated" / "companions", *dlls):
        if not p.exists():
            raise ReleaseError(f"{p} missing: run the build in README.md first")
    dest = out / name
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(root / "out" / "data", dest / f"{name}.mpq" / "data")
    modinfo = json.dumps({"name": name, "savepath": mod.get("savepath", f"{name}/")}, indent=2)
    for f in (dest / f"{name}.mpq" / "modinfo.json", dest / "modinfo.json"):
        f.write_text(modinfo, encoding="utf-8")
    d2rl = dest / "d2rloader"
    for sub in ("config", "plugins"):
        (d2rl / sub).mkdir(parents=True)
    meta = {"schemaVersion": 1, "metadata": {"name": mod.get("title", name), "modVersion": mod.get("version", ""),
                                              "author": mod.get("author", ""), "description": mod.get("description", ""),
                                              "website": mod.get("website", "")}}
    (d2rl / "metadata.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    if mod.get("extendedSaves"):
        (d2rl / "config" / "d2rloader.toml").write_text('[d2rcore.save_format]\nmode = "extended"\n', encoding="utf-8")
    overlay(root, dest)
    fill_tables(dest / f"{name}.mpq" / "data" / "global" / "excel", read)
    for dll in dlls:
        shutil.copy2(dll, d2rl / "plugins" / dll.name)
    return dest


INSTALL = r"""## Install

Into a D2R folder at the build above, with D2RLoader installed (the game must be closed):

1. Unpack `{archive}` into `<D2R folder>\mods\` so that `<D2R folder>\mods\{folder}\{folder}.mpq\modinfo.json`
   exists (Windows 11 opens .7z files; otherwise use 7-Zip, https://www.7-zip.org).
2. Make {folder} D2RLoader's default mod (once): in D2RLoader's settings, or with `default_mod = "{folder}"` in
   `<D2R folder>\d2rloader\config\d2rloader.toml`. Then start `D2RLoader.exe`. Characters go to
   `Saved Games\Diablo II Resurrected\mods\{folder}\`.
3. To update, remove `mods\{folder}\{folder}.mpq\data` before unpacking the new release, and keep
   `mods\{folder}\d2rloader\config\d2rloader.toml` if you edited it.

"""
KNOWN = ("## Known issues\n\n"
         "- What is not ported yet, or works differently from Project Diablo II: the [deviation list]({url}).\n"
         "- Until the first multiplayer release, a new build may break your characters; each break is in the changes.")


def finish_notes(dist: Path, version: str, section: str, commit: str, about: str, folder: str) -> None:
    """Fill package_release's notes skeleton (about line, 7z-only install, changes, known issues), refresh SHA256SUMS."""
    notes = dist / f"RELEASE_NOTES-{version}.md"
    text = notes.read_text("utf-8")
    archive = next(dist.glob("*.7z")).name
    fills = [(r"^.* TODO: one sentence about what this release is\.$", about),
             (r"(?s)^## Install\n.*?(?=^## Changes$)", INSTALL.format(archive=archive, folder=folder)),
             # the changelog's #### groups sit under "## Changes" here
             (r"^## Changes\n\n- TODO$", "## Changes\n\n" + re.sub(r"(?m)^#### ", "### ", section)),
             (r"^## Known issues\n\n- TODO$", KNOWN.format(url=DEVIATIONS.format(commit=commit)))]
    for pattern, repl in fills:
        text, n = re.subn(pattern, lambda _: repl, text, count=1, flags=re.M)
        if not n:
            raise ReleaseError(f"package_release's notes changed: no match for {pattern!r} in {notes}")
    notes.write_text(text, encoding="utf-8", newline="\n")
    sums = dist / "SHA256SUMS"
    names = [line.split("  ", 1)[1] for line in sums.read_text("utf-8").splitlines() if line]
    sums.write_text("".join(f"{hashlib.sha256((dist / n).read_bytes()).hexdigest()}  {n}\n" for n in names),
                    encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, required=True, help="D2R folder (the loot filter's vanilla tables)")
    ap.add_argument("--version", help="release version (default: mod.json's)")
    ap.add_argument("--out", type=Path, default=ROOT / "dist")
    ap.add_argument("--toolkit", type=Path, default=Path(os.environ.get("D2R_TOOLKIT", pd2_ids.toolkit(ROOT))))
    a = ap.parse_args(argv)
    mod_json = json.loads((ROOT / "mod.json").read_text("utf-8"))
    version = a.version or mod_json["version"]
    try:
        section = notes_section((ROOT / "CHANGELOG.md").read_text("utf-8"), version)
        from modkit.base import Base  # the sibling d2rAI-toolkit on PYTHONPATH
        mod = stage(ROOT, a.out / "mod", Base(a.game).bytes)
    except ReleaseError as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    r = subprocess.run([sys.executable, str(a.toolkit / "release" / "package_release.py"), "--mod-dir", str(mod),
                        "--version", version, "--config", str(ROOT / "release.toml"), "--out", str(a.out)])
    if r.returncode:
        return r.returncode
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    try:
        finish_notes(a.out, version, section, commit.strip(), mod_json["description"], mod_json["name"])
    except ReleaseError as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    print(f"notes: {a.out / f'RELEASE_NOTES-{version}.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
