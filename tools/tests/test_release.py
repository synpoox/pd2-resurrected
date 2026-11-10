"""Unit tests for tools/release.py on a synthetic project: no game, PD2 data or 7-Zip needed."""
import hashlib
import json
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import release  # noqa: E402

CHANGELOG = """# Changelog

Intro.

### 0.2.0 (2026-12-01)

- Newer.

### 0.1.0-alpha (2026-11-05)

- First line.
- Second line.
"""


# package_release.py's notes skeleton, shortened (d2rAI-toolkit release/package_release.py NOTES)
SKELETON = """# Mod 1.0.0

Mod for Diablo II: Resurrected. TODO: one sentence about what this release is.

## Requirements

- Diablo II: Resurrected **build 93847** (D2R 3.3.0).

## Install

1. Run `MyMod-Downloader.exe` and choose **Install**.

By hand (offline):

1. Unpack it.

## Changes

- TODO

## Known issues

- TODO

## Checksums

See SHA256SUMS.
"""


class FoldTests(unittest.TestCase):
    LOG = ("# Changelog\n\n### Unreleased\n\n#### Maps\n\n- A.\n\n#### Items\n\n- B.\n\n"
           "### 0.1.0 (2026-01-01)\n\n#### Maps\n\n- Old.\n")

    def test_a_fragment_joins_its_categorys_bullets_in_unreleased_only(self):
        out = release.fold(self.LOG, "0.2.0", ["#### Maps\n- C.\n"])
        self.assertIn("#### Maps\n\n- A.\n- C.\n\n#### Items\n\n- B.\n\n### 0.1.0", out)
        self.assertIn("- Old.\n", out)

    def test_a_new_category_is_added_at_the_end_of_the_section(self):
        out = release.fold(self.LOG, "0.2.0", ["#### Skills\n- D.\n", "- E.\n"])
        self.assertIn("- B.\n\n#### Skills\n\n- D.\n- E.\n\n### 0.1.0", out)

    def test_a_renamed_section_takes_the_fragments_and_a_missing_one_fails(self):
        log = self.LOG.replace("Unreleased", "0.2.0 (2026-02-01)")
        self.assertIn("- C.\n", release.fold(log, "0.2.0", ["#### Maps\n- C.\n"]))
        with self.assertRaises(release.ReleaseError):
            release.fold(self.LOG.replace("Unreleased", "Soon"), "0.2.0", ["- C.\n"])

    def test_folding_fragments_deletes_them_in_file_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "changelog.d").mkdir()
            (root / "CHANGELOG.md").write_text(self.LOG, "utf-8")
            (root / "changelog.d" / "2-b.md").write_text("#### Items\n- Y.\n", "utf-8")
            (root / "changelog.d" / "1-a.md").write_text("#### Items\n- X.\n", "utf-8")
            (root / "changelog.d" / ".gitkeep").write_text("", "utf-8")
            self.assertEqual(release.fold_fragments(root, "0.2.0"), 2)
            self.assertIn("- B.\n- X.\n- Y.\n", (root / "CHANGELOG.md").read_text("utf-8"))
            self.assertEqual([f.name for f in (root / "changelog.d").iterdir()], [".gitkeep"])
            self.assertEqual(release.fold_fragments(root, "0.2.0"), 0)


    def test_a_failed_check_leaves_the_fragments_and_changelog_in_place(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "changelog.d").mkdir()
            (root / "CHANGELOG.md").write_text(self.LOG, "utf-8")
            (root / "mod.json").write_text('{"name": "M", "version": "0.2.0"}', "utf-8")
            (root / "changelog.d" / "1-a.md").write_text("#### Items\n- X.\n", "utf-8")
            with mock.patch.object(release, "ROOT", root), \
                    mock.patch.object(release, "notes_section", side_effect=release.ReleaseError("no notes")):
                self.assertEqual(release.main(["--game", str(root), "--out", str(root / "dist")]), 1)
            self.assertEqual([f.name for f in (root / "changelog.d").iterdir()], ["1-a.md"])
            self.assertEqual((root / "CHANGELOG.md").read_text("utf-8"), self.LOG)


class NotesTests(unittest.TestCase):
    def test_takes_only_the_versions_section(self):
        self.assertEqual(release.notes_section(CHANGELOG, "0.1.0-alpha"), "- First line.\n- Second line.")
        self.assertEqual(release.notes_section(CHANGELOG, "0.2.0"), "- Newer.")

    def test_refuses_a_version_without_a_section(self):
        with self.assertRaises(release.ReleaseError):
            release.notes_section(CHANGELOG, "0.1.0")  # a prefix of 0.1.0-alpha must not match
        with self.assertRaises(release.ReleaseError):
            release.notes_section("### Unreleased\n\n- x\n", "0.1.0-alpha")

    def dist(self, tmp: str, skeleton: str = SKELETON) -> Path:
        dist = Path(tmp)
        (dist / "M-1.0.0.7z").write_bytes(b"7z")
        (dist / "RELEASE_NOTES-1.0.0.md").write_text(skeleton)
        (dist / "SHA256SUMS").write_text(f"{'0' * 64}  M-1.0.0.7z\n{'0' * 64}  RELEASE_NOTES-1.0.0.md\n")
        return dist

    def test_finish_notes_lists_only_the_changes_and_keeps_sums_true(self):
        with tempfile.TemporaryDirectory() as tmp:
            dist = self.dist(tmp)
            release.finish_notes(dist, "1.0.0", "#### Items\n\n- A change.\n")
            self.assertEqual((dist / "RELEASE_NOTES-1.0.0.md").read_text(), "#### Items\n\n- A change.\n")
            for line in (dist / "SHA256SUMS").read_text().splitlines():
                digest, name = line.split("  ")
                self.assertEqual(digest, hashlib.sha256((dist / name).read_bytes()).hexdigest(), name)


class StageTests(unittest.TestCase):
    def project(self, root: Path) -> Path:
        root.mkdir()
        (root / "mod.json").write_text(json.dumps({"name": "Mod", "title": "The Mod", "version": "1.0.0",
                                                   "extendedSaves": True}))
        (root / "out/data/global/excel").mkdir(parents=True)
        (root / "out/data/global/excel/skills.txt").write_text("ours")
        comp = root / "generated/companions"
        (comp / "d2rloader/plugins").mkdir(parents=True)
        (comp / "d2rloader/plugins/x.dll").write_bytes(b"companion")
        (comp / "d2rloader/config").mkdir()
        (comp / "d2rloader/config/filter.json").write_text("{}")
        build = root / "plugin/build/Release"
        build.mkdir(parents=True)
        for name in release.PLUGINS:
            (build / f"{name}.dll").write_bytes(name.encode())
        return root

    def test_lays_out_the_installed_mod_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp) / "p")
            mod = release.stage(root, Path(tmp) / "stage", read=lambda rel: b"vanilla " + rel.encode())
            self.assertEqual(mod.name, "Mod")
            self.assertEqual((mod / "Mod.mpq/data/global/excel/skills.txt").read_text(), "ours")
            self.assertEqual((mod / "Mod.mpq/data/global/excel/armor.txt").read_bytes(),
                             b"vanilla data/global/excel/armor.txt")  # the loot filter's missing tables
            for f in ("modinfo.json", "Mod.mpq/modinfo.json"):
                self.assertEqual(json.loads((mod / f).read_text()), {"name": "Mod", "savepath": "Mod/"})
            self.assertEqual(json.loads((mod / "d2rloader/metadata.json").read_text())["metadata"]["modVersion"],
                             "1.0.0")
            toml = (mod / "d2rloader/config/d2rloader.toml").read_text()
            self.assertIn('mode = "extended"', toml)
            self.assertIn("item_stat_ranges = true", toml)
            plugins = sorted(p.name for p in (mod / "d2rloader/plugins").iterdir())
            self.assertEqual(plugins, sorted([f"{n}.dll" for n in release.PLUGINS] + ["x.dll"]))

    def test_refuses_a_missing_plugin(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp) / "p")
            (root / "plugin/build/Release/core-ui.dll").unlink()
            with self.assertRaises(release.ReleaseError):
                release.stage(root, Path(tmp) / "stage", read=lambda rel: b"")

    def test_restaging_starts_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp) / "p")
            stale = Path(tmp) / "stage/Mod/Mod.mpq/data/old.txt"
            stale.parent.mkdir(parents=True)
            stale.write_text("old")
            release.stage(root, Path(tmp) / "stage", read=lambda rel: b"")
            self.assertFalse(stale.exists())


if __name__ == "__main__":
    unittest.main()
