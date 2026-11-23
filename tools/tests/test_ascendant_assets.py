"""Portable checks for the optional asset contribution and preserving recipient catalogs."""
import copy
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import ascendant_assets as A  # noqa: E402


def fixture(key=123):
    # Independently packed single-model catalog with one LOD and one opaque mesh.
    header = struct.pack("<6I", 1, 20, 1, 56, 1, 60)
    model = struct.pack("<I6f4I", key, -1, -2, -3, 1, 2, 3, 17, 1, 8, 29)
    lod = struct.pack("<3I", 7, 1, 4)
    return header + model + lod + bytes(range(32))


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.registrations = json.loads((A.ART / "model-registrations.json").read_text("utf-8"))

    def test_merge_retains_original_opaque_records_and_adds_both_models(self):
        source = fixture()
        before = A.decode(source)
        result = A.decode(A.merge_catalog(source, self.registrations))
        self.assertEqual(result[123], before[123])
        self.assertEqual(len(result), 3)
        for name, model in self.registrations.items():
            self.assertEqual(result[A.model_key(name)], model)

    def test_repeated_merge_is_byte_identical(self):
        once = A.merge_catalog(fixture(), self.registrations)
        self.assertEqual(A.merge_catalog(once, self.registrations), once)

    def test_conflicting_existing_registration_is_refused(self):
        name = next(iter(self.registrations))
        with self.assertRaisesRegex(ValueError, "conflicting"):
            A.merge_catalog(fixture(A.model_key(name)), self.registrations)

    def test_corrupt_child_pointer_is_refused(self):
        damaged = bytearray(fixture())
        struct.pack_into("<I", damaged, 60, 0xFFFFFFFC)
        with self.assertRaisesRegex(ValueError, "child range"):
            A.merge_catalog(bytes(damaged), self.registrations)

    def test_truncated_catalog_is_refused(self):
        with self.assertRaises(ValueError):
            A.merge_catalog(fixture()[:-1], self.registrations)

    def test_source_round_trip_is_byte_identical(self):
        self.assertEqual(A.encode(A.decode(fixture())), fixture())


class TextureTests(unittest.TestCase):
    def test_texture_merge_preserves_existing_entries_and_is_idempotent(self):
        original = [{"filename": "data/hd/existing.texture", "opaque": {"keep": 7}}]
        before = copy.deepcopy(original)
        additions = json.loads((A.ART / "texture-registration.json").read_text("utf-8"))
        result = A.merge_textures(original, additions)
        self.assertEqual(original, before)
        self.assertEqual(result[0], before[0])
        self.assertEqual(A.merge_textures(result, additions), result)

    def test_case_insensitive_texture_collision_is_refused(self):
        with self.assertRaisesRegex(ValueError, "conflicting"):
            A.merge_textures([{"filename": "DATA/HD/A.texture", "width": 1}],
                             [{"filename": "data/hd/a.texture", "width": 2}])

    def test_duplicate_existing_texture_names_are_refused(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            A.merge_textures([{"filename": "a"}, {"filename": "A"}], [])


class ContributionTests(unittest.TestCase):
    def test_portraits_are_the_complete_pinned_set_at_both_resolutions(self):
        manifest = json.loads((A.ART / "manifest.json").read_text("utf-8"))
        revision = manifest["portraitRevision"]
        commit = "a186ea0299c6f44c8a74ae4a97dec4b1b6118324"
        prefix = "files/data/hd/global/ui/hireables/"
        expected = {prefix + name + suffix + ".sprite"
                    for name in ("ruff_a4_portrait", "ruff_a4_light") for suffix in ("", ".lowend")}
        self.assertEqual(revision["commit"], commit)
        self.assertEqual(set(revision["files"]), expected)
        entries = {entry["path"]: entry for entry in manifest["files"]}
        for path in expected:
            entry = entries[path]
            self.assertEqual(entry["sourceCommit"], commit)
            self.assertEqual(entry["gitLfsOid"], "sha256:" + entry["sha256"])
            data = (ROOT / path).read_bytes()
            self.assertEqual(data[:4], b"SpA1")
            dimension = 60 if ".lowend." in path else 120
            self.assertEqual(struct.unpack_from("<2I", data, 8), (dimension, dimension))
            self.assertEqual(len(data), 40 + dimension * dimension * 4)
            self.assertEqual(entry["dimensions"], [dimension, dimension])

    def test_final_dark_revision_is_a_complete_pinned_binary_set(self):
        manifest = json.loads((A.ART / "manifest.json").read_text("utf-8"))
        revision = manifest["darkFinalRevision"]
        commit = "537a302c4ca1102927aa77f5cbc08f03bcaa903f"
        prefix = "files/data/hd/character/enemy/ruff_a4_dark/"
        expected = {prefix + "animation/combined.animations",
                    *[prefix + f"equipment/body_lod{i}.model" for i in range(4)]}
        self.assertEqual(revision["commit"], commit)
        self.assertEqual(set(revision["files"]), expected)
        entries = {entry["path"]: entry for entry in manifest["files"]}
        for path in expected:
            entry = entries[path]
            self.assertEqual(entry["sourceCommit"], commit)
            self.assertEqual(entry["sourceRepository"], "BRODIABLO/My-Reimagined")
            self.assertEqual(entry["gitLfsOid"], "sha256:" + entry["sha256"])
            self.assertFalse((ROOT / path).read_bytes().startswith(b"version https://git-lfs.github.com/spec/v1"))
        self.assertEqual(entries[prefix + "animation/combined.animations"]["sha256"],
                         "704e84d2741062f976c048d530ff72f3c66b1d4e6263b2f4ca816976de1cda9b")

    def test_manifest_matches_every_contributed_file(self):
        self.assertEqual(A.verify(), 26)

    def test_each_private_dependency_is_present_or_registered(self):
        data_root = ROOT / "files/data"
        models = json.loads((A.ART / "model-registrations.json").read_text("utf-8"))
        paths = [*data_root.glob("hd/character/enemy/ruff_a4_*/**/*.json"),
                 *data_root.glob("hd/character/enemy/ruff_a4_*.json"),
                 *data_root.glob("hd/character/enemy/ruff_a4_*/animation/*.timelines")]
        checked = 0
        for path in paths:
            doc = json.loads(path.read_text("utf-8-sig"))
            def visit(value):
                nonlocal checked
                if isinstance(value, dict):
                    for item in value.values():
                        visit(item)
                elif isinstance(value, list):
                    for item in value:
                        visit(item)
                elif isinstance(value, str) and value.startswith("data/") and "ruff_a4" in value:
                    checked += 1
                    if value.endswith(".animation"):
                        self.assertTrue((data_root / value[5:]).parent.joinpath("combined.animations").is_file(), value)
                    elif value.endswith(".model"):
                        self.assertIn(value, models)
                        for index in range(len(models[value]["lods"])):
                            self.assertTrue((data_root / (value[5:-6] + f"_lod{index}.model")).is_file(), value)
                    else:
                        self.assertTrue((data_root / value[5:]).is_file(), value)
            visit(doc)
        self.assertGreater(checked, 25)

    def test_descriptor_conflict_does_not_create_any_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "catalog.bin").write_bytes(fixture())
            (folder / "textures.json").write_text('[{"filename":"a"},{"filename":"A"}]', "utf-8")
            result = subprocess.run([sys.executable, str(ROOT / "tools/ascendant_assets.py"),
                                     "--catalog", str(folder / "catalog.bin"), "--output", str(folder / "out.bin"),
                                     "--texture-cache", str(folder / "textures.json"),
                                     "--texture-output", str(folder / "out.json")], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((folder / "out.bin").exists())
            self.assertFalse((folder / "out.json").exists())

    def test_cli_refuses_to_overwrite_a_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "keep.bin"
            target.write_bytes(b"preserve recipient file")
            result = subprocess.run([sys.executable, str(ROOT / "tools/ascendant_assets.py"),
                                     "--catalog", str(target), "--output", str(target)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_bytes(), b"preserve recipient file")


if __name__ == "__main__":
    unittest.main()
