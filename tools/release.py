"""Package a built mod as a player release: a 7z, release notes and SHA256SUMS in dist/ (issue #109).

    python tools/release.py --game DIR [--version V] [--out dist]

Run it after the whole build in README.md (merge, companions fetch, modkit build, plugin\\build.ps1). It stages the
mod folder as an install lays it out (out/data, modinfo, loader metadata and config, the companions, our plugins in
d2rloader/plugins/, the vanilla tables the loot filter reads from DIR) in dist/mod/, then runs d2rAI-toolkit's
release/package_release.py with release.toml, and replaces its notes with the changelog's `### <version>` section
(the README has the requirements and install steps). The version defaults to
mod.json's; the changelog must have its section. Before that it folds the fragments in changelog.d/ into the changelog
(fold_fragments), so a PR adds a fragment file instead of editing CHANGELOG.md.
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


class ReleaseError(Exception):
    pass


def fold(changelog: str, version: str, fragments: list[str]) -> str:
    """The changelog with each fragment's bullet lines added at the end of the `### Unreleased` section (else the
    `### <version> (<date>)` one). A fragment may open with a `#### <Category>` line: its bullets then join that
    heading's bullets in the section, or start it. Fragments fold in the order given."""
    m = re.search(rf"^### (?:Unreleased|{re.escape(version)} \(.*?\))[ \t]*\n(.*?)(?=^### |\Z)", changelog, re.M | re.S)
    if not m:
        raise ReleaseError(f"CHANGELOG.md has no '### Unreleased' or '### {version} (<date>)' section")
    body = m.group(1)
    for text in fragments:
        lines = [ln for ln in text.strip().splitlines() if ln.strip()]
        head = lines.pop(0) if lines and lines[0].startswith("#### ") else None
        if not lines:
            continue
        add = "\n".join(lines)
        if head is None:
            body = body.rstrip("\n") + "\n" + add + "\n\n"
            continue
        h = re.search(rf"^{re.escape(head)}[ \t]*\n(.*?)(?=^#### |\Z)", body, re.M | re.S)
        if h:
            body = body[:h.start(1)] + h.group(1).rstrip("\n") + "\n" + add + "\n\n" + body[h.end(1):]
        else:
            body = body.rstrip("\n") + f"\n\n{head}\n\n{add}\n\n"
    return changelog[:m.start(1)] + body.rstrip("\n") + "\n\n" + changelog[m.end(1):]


def pending_fragments(root: Path, version: str) -> tuple[str, list[Path]]:
    """CHANGELOG.md's text with changelog.d/*.md (by file name) folded in, and those files. Changes nothing on disk."""
    files = sorted((root / "changelog.d").glob("*.md"))
    text = (root / "CHANGELOG.md").read_text("utf-8")
    return (fold(text, version, [f.read_text("utf-8") for f in files]) if files else text), files


def fold_fragments(root: Path, version: str) -> int:
    """Fold changelog.d/*.md into CHANGELOG.md and delete them; how many were folded."""
    text, files = pending_fragments(root, version)
    if files:
        (root / "CHANGELOG.md").write_text(text, "utf-8", newline="\n")
        for f in files:
            f.unlink()
    return len(files)


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


def finish_notes(dist: Path, version: str, section: str) -> None:
    """Replace package_release's notes with the changelog section alone, refresh SHA256SUMS.

    Requirements and install steps live in the README, so the release page lists only the changes."""
    notes = dist / f"RELEASE_NOTES-{version}.md"
    if not notes.exists():
        raise ReleaseError(f"package_release wrote no {notes}")
    notes.write_text(section.strip() + "\n", encoding="utf-8", newline="\n")
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
    log = ROOT / "CHANGELOG.md"
    before = log.read_text("utf-8")
    try:
        text, fragments = pending_fragments(ROOT, version)
        section = notes_section(text, version)  # checked before anything is written or deleted
        from modkit.base import Base  # the sibling d2rAI-toolkit on PYTHONPATH
        mod = stage(ROOT, a.out / "mod", Base(a.game).bytes)
    except ReleaseError as e:
        print(f"release: {e}", file=sys.stderr)
        return 1
    if fragments:  # package_release reads CHANGELOG.md, so fold it now; the fragments go only once the release is done
        log.write_text(text, "utf-8", newline="\n")
        print(f"changelog: folded {len(fragments)} fragments from changelog.d/")
    code = 0
    try:
        r = subprocess.run([sys.executable, str(a.toolkit / "release" / "package_release.py"), "--mod-dir", str(mod),
                            "--version", version, "--config", str(ROOT / "release.toml"), "--out", str(a.out),
                            "--game-exe", str(a.game / "D2R.exe")])  # release.toml's [catalog] (schema 3) needs it
        code = r.returncode
        if not code:
            finish_notes(a.out, version, section)
    except ReleaseError as e:
        print(f"release: {e}", file=sys.stderr)
        code = 1
    if code:  # a failed release leaves CHANGELOG.md and the fragments as they were
        log.write_text(before, "utf-8", newline="\n")
        return code
    for f in fragments:
        f.unlink()
    print(f"notes: {a.out / f'RELEASE_NOTES-{version}.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
