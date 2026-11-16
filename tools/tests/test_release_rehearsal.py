import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import release_rehearsal as rr  # noqa: E402


def package(root: Path, files: dict[str, tuple[str, str]]) -> dict:
    """Write files {rel: (text, ownership)} under root; return the manifest's owned() view."""
    out = {}
    for rel, (text, own) in files.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text)
        out[rel] = {"path": rel, "sha256": hashlib.sha256(text.encode()).hexdigest(), "ownership": own}
    return out


class RehearsalTests(unittest.TestCase):
    def test_upgrade_cleans_stale_files_keeps_user_files_and_rolls_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            mod, glob = t / "game/mods/Mod", t / "game/d2rloader/plugins"
            glob.mkdir(parents=True)
            old = package(t / "old", {"d2rloader/plugins/core-events.dll": ("old maps", "managed"),
                                      "d2rloader/config/d2rloader.toml": ("default", "shipped_default"),
                                      "Mod.mpq/modinfo.json": ("{}", "managed"), "modinfo.json": ("{}", "managed")})
            new = package(t / "new", {"d2rloader/plugins/cartographer.dll": ("carto", "managed"),
                                      "d2rloader/config/d2rloader.toml": ("default 2", "shipped_default"),
                                      "Mod.mpq/modinfo.json": ("{}", "managed"), "modinfo.json": ("{}", "managed")})
            rr.apply(t / "old", mod, old, {}, glob, "Mod-Slot5/")
            (mod / "d2rloader/config/d2rloader.toml").write_text("player edit")
            (mod / "d2rloader/plugins/user.dll").write_text("unrelated")
            (glob / "core-events.dll").write_text("stray global copy")
            n = rr.apply(t / "new", mod, new, old, glob, "Mod-Slot5/")
            self.assertEqual((n["deleted"], n["other_scope_removed"]), (1, 1))
            self.assertEqual(rr.verify(mod, new, old, glob), [])
            self.assertEqual((mod / "d2rloader/config/d2rloader.toml").read_text(), "player edit")
            self.assertTrue((mod / "d2rloader/plugins/user.dll").exists())
            self.assertEqual(json.loads((mod / "modinfo.json").read_text())["savepath"], "Mod-Slot5/")
            rr.apply(t / "old", mod, old, new, glob, "Mod-Slot5/")
            self.assertEqual(rr.verify(mod, old, new, glob), [])
            self.assertFalse((mod / "d2rloader/plugins/cartographer.dll").exists())
            self.assertTrue((mod / "d2rloader/plugins/user.dll").exists())

    def test_an_edited_old_only_default_is_not_stale_and_a_path_out_of_mod_dir_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            mod = t / "game/mods/Mod"
            old = package(t / "old", {"cfg.toml": ("default", "shipped_default")})
            new = package(t / "new", {"a.txt": ("a", "managed")})
            rr.apply(t / "old", mod, old, {})
            self.assertEqual(rr.verify(mod, new, old), ["missing a.txt", "stale cfg.toml"])
            (mod / "cfg.toml").write_text("player edit")
            self.assertEqual(rr.verify(mod, new, old), ["missing a.txt"])
            with self.assertRaises(ValueError):
                rr.apply(t / "new", mod, {"../x.txt": {"sha256": "0", "ownership": "managed"}}, {})
            self.assertFalse((t / "game/mods/x.txt").exists())


if __name__ == "__main__":
    unittest.main()
