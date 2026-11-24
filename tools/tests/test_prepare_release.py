import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_release import prepare


class PrepareRelease(unittest.TestCase):
    def test_packaging_uses_a_seed_copy_and_keeps_sources_and_notes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "mod.json").write_text(json.dumps({"version": "0.2.5-alpha"}))
            original = "# Changelog\n\n### 0.2.4-alpha (2026-11-22)\n\n- Previous fix.\n"
            (root / "CHANGELOG.md").write_text(original)
            (root / "release.toml").write_text('[repos]\nchangelog = "CHANGELOG.md"\nfragments = "changelog.d"\n[stage]\nnotes = "release/notes/{version}.md"\n')
            (root / "changelog.d").mkdir()
            fragment = root / "changelog.d/1-fix.md"
            fragment.write_text("- New fix (#1).\n")
            config = tomllib.loads(prepare(root).read_text())
            seed = (root / config["repos"]["changelog"]).read_text()
            self.assertIn("### 0.2.5-alpha (", seed)
            self.assertIn(original[original.index("### "):], seed)
            self.assertEqual((root / "CHANGELOG.md").read_text(), original)
            self.assertEqual(fragment.read_text(), "- New fix (#1).\n")
            self.assertEqual((root / config["repos"]["fragments"] / fragment.name).read_text(), fragment.read_text())
            self.assertNotEqual(config["repos"]["fragments"], "changelog.d")
            self.assertEqual(config["stage"]["notes"], "release/notes/{version}.md")
