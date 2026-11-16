"""install.ps1 keeps cartographer.dll in a slot's plugins instead of moving it as a stray (#1017)."""
import re
import unittest
from pathlib import Path

SCRIPT = (Path(__file__).resolve().parents[1] / "install.ps1").read_text(encoding="utf-8")


class KnownPlugins(unittest.TestCase):
    def test_cartographer_is_expected(self):
        ours = re.search(r"^\$ours = @\((.*)\)", SCRIPT, re.M).group(1)
        self.assertIn('"cartographer.dll"', ours)

    def test_moves_stale_mod_plugins_and_installs_signatures(self):  # #970, #987
        for needle in (r"mods\$repoMod\d2rloader\plugins", "stale-mod-plugins",
                       r"cartographer\signatures.txt", r"excel\d2rloader\cartographer"):
            self.assertIn(needle, SCRIPT)


if __name__ == "__main__":
    unittest.main()
