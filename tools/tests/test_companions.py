"""Unit tests for tools/companions.py on synthetic zips: no network or game needed."""
import hashlib
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import companions  # noqa: E402


def make_zip(files: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buf.getvalue()


class CompanionTests(unittest.TestCase):
    def project(self, tmp: Path, zipped: bytes, sha: str | None = None) -> Path:
        (tmp / "companions").mkdir()
        (tmp / "companions" / "filter.json").write_text('{"version": 3, "rules": []}')
        manifest = {"companions": [{"name": "X", "repo": "o/r", "tag": "t", "asset": "x.zip",
                                    "sha256": sha or hashlib.sha256(zipped).hexdigest(),
                                    "files": {"d2rloader/plugins/x.dll": "d2rloader/plugins/x.dll",
                                              "Other.mpq/data/sfx/": "Mod.mpq/data/sfx/"}}]}
        (tmp / "companions" / "manifest.json").write_text(json.dumps(manifest))
        return tmp

    def test_unpacks_only_listed_files(self):
        zipped = make_zip({"d2rloader/plugins/x.dll": b"dll", "Other.mpq/data/sfx/a.flac": b"a",
                           "Other.mpq/data/global/excel/sounds.txt": b"not ours"})
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), zipped)
            written = companions.fetch(root, download=lambda *a: zipped)
            out = root / "generated" / "companions"
            self.assertEqual(sorted(written), ["Mod.mpq/data/sfx/a.flac", "d2rloader/config/filter.json",
                                               "d2rloader/plugins/x.dll"])
            self.assertEqual((out / "d2rloader/plugins/x.dll").read_bytes(), b"dll")
            self.assertFalse((out / "Other.mpq").exists())

    def test_commit_entry_takes_the_raw_file(self):
        dll = b"raw dll"
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), b"")
            manifest = {"companions": [{"name": "Y", "repo": "o/r", "commit": "abc", "asset": "release/y.dll",
                                        "sha256": hashlib.sha256(dll).hexdigest(),
                                        "files": {"release/y.dll": "d2rloader/plugins/y.dll"}}]}
            (root / "companions" / "manifest.json").write_text(json.dumps(manifest))
            calls = []
            written = companions.fetch(root, download=lambda *a: calls.append(a) or dll)
            self.assertEqual(calls, [("o/r", "abc", "release/y.dll", True)])
            self.assertEqual(sorted(written), ["d2rloader/config/filter.json", "d2rloader/plugins/y.dll"])
            self.assertEqual((root / "generated/companions/d2rloader/plugins/y.dll").read_bytes(), dll)

    def test_our_configs_replace_the_zip_defaults(self):
        zipped = make_zip({"d2rloader/plugins/x.dll": b"dll", "Other.mpq/data/sfx/a": b"",
                           "config/x.toml": b"theirs"})
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), zipped)
            manifest = json.loads((root / "companions" / "manifest.json").read_text())
            manifest["companions"][0]["files"]["config/x.toml"] = "d2rloader/config/x.toml"
            (root / "companions" / "manifest.json").write_text(json.dumps(manifest))
            (root / "companions" / "config").mkdir()
            (root / "companions" / "config" / "x.toml").write_text("ours")
            written = companions.fetch(root, download=lambda *a: zipped)
            self.assertEqual((root / "generated/companions/d2rloader/config/x.toml").read_text(), "ours")
            self.assertEqual(written.count("d2rloader/config/x.toml"), 1)

    def test_refuses_a_hash_mismatch_or_missing_file(self):
        for zipped, sha, msg in [(make_zip({"d2rloader/plugins/x.dll": b"1", "Other.mpq/data/sfx/a": b""}), "0" * 64,
                                  "SHA-256"),
                                 (make_zip({"d2rloader/plugins/x.dll": b"1"}), None, "no Other.mpq/data/sfx/")]:
            with self.subTest(msg), tempfile.TemporaryDirectory() as tmp:
                root = self.project(Path(tmp), zipped, sha)
                with self.assertRaisesRegex(companions.CompanionError, msg):
                    companions.fetch(root, download=lambda *a: zipped)
                self.assertFalse((root / "generated" / "companions").exists())

    def test_overlay_turns_on_item_ranges_once_and_keeps_settings(self):
        zipped = make_zip({"d2rloader/plugins/x.dll": b"dll", "Other.mpq/data/sfx/a.flac": b"a"})
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), zipped)
            companions.fetch(root, download=lambda *a: zipped)
            mod = root / "game" / "mods" / "Mod"
            toml = mod / "d2rloader" / "config" / "d2rloader.toml"
            toml.parent.mkdir(parents=True)
            toml.write_text('[d2rcore.save_format]\nmode = "extended"')
            companions.overlay(root, mod)
            companions.overlay(root, mod)
            self.assertEqual(toml.read_text(), '[d2rcore.save_format]\nmode = "extended"\n' + companions.ITEM_RANGES)
            self.assertTrue((mod / "d2rloader" / "config" / "filter.json").is_file())
            # the loader's own template: section present, key commented out
            toml.write_text("[d2rcore.items]\n# item_stat_ranges = false\n\n[d2rcore.stash]\n")
            companions.overlay(root, mod)
            self.assertEqual(toml.read_text(), companions.ITEM_RANGES + "# item_stat_ranges = false\n\n[d2rcore.stash]\n")

    def test_fill_tables_adds_only_missing_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            excel = Path(tmp) / "excel"
            excel.mkdir()
            (excel / "armor.txt").write_bytes(b"ours")
            filled = companions.fill_tables(excel, lambda rel: rel.encode())
            self.assertEqual(filled, [t for t in companions.FILTER_TABLES if t != "armor"])
            self.assertEqual((excel / "armor.txt").read_bytes(), b"ours")
            self.assertEqual((excel / "misc.txt").read_bytes(), b"data/global/excel/misc.txt")


if __name__ == "__main__":
    unittest.main()
