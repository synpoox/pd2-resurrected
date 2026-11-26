"""install.ps1 installs our plugins mod-scoped and moves our old game-wide copies out (#1017, #1180)."""
import re
import unittest
from pathlib import Path

SCRIPT = (Path(__file__).resolve().parents[1] / "install.ps1").read_text(encoding="utf-8")


class KnownPlugins(unittest.TestCase):
    def test_plugins_install_in_the_mod_folder(self):  # #1180: file-name order puts core-rules before Cast Triggers
        self.assertIn(r'$plugins = Join-Path $Game "mods\$repoMod\d2rloader\plugins"', SCRIPT)

    def test_old_game_wide_copies_always_move(self):  # a second copy would load
        ours = re.search(r"^\$isOurs = \{(.*)\}", SCRIPT, re.M).group(1)
        for name in ('"core-*.dll"', '"cartographer.dll"', '"d2rl-charm-inv.*"'):
            self.assertIn(name, ours)
        self.assertIn("(& $isOurs $_.Name) -or -not $KeepExtras", SCRIPT)

    def test_cartographer_comes_from_the_companions(self):  # a test slot gets them too, through overlay()
        self.assertIn(r"generated\data would override", SCRIPT)
        self.assertIn("release.companions import overlay", SCRIPT)
        self.assertNotIn("check_cartographer", SCRIPT)


if __name__ == "__main__":
    unittest.main()
