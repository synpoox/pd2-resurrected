"""No game install, CASC, save or dump files in git (ADR 0007): the only committed game data is the built files/data/."""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIRS = ("pd2-data/", "generated/", "d2r-data/", "dist/", "coverage/", "out/", "import/")
EXT = {".mpq", ".casc", ".idx", ".exe", ".dll", ".pdb", ".dmp", ".mdmp", ".d2s", ".d2i", ".ctlo", ".key",
       ".ma0", ".ma1", ".ma2", ".ma3"}
CASC = re.compile(r"(^|/)(data\.\d{3}|\.build\.info|shmem)$", re.I)
TOP = {".claude", ".github", ".gitignore", ".gitattributes", ".gitmodules", "AGENTS.md", "CHANGELOG.md", "GLOSSARY.md",
       "README.md", "changelog.d", "companions", "docs", "files", "merge", "mod.json", "pd2-baseline.json",
       "pd2-import.txt", "plugin", "release.toml", "tests", "third_party", "tools", "check_public.allow"}


def offenders(paths: list[str]) -> list[str]:
    return [p for p in paths if p.startswith(DIRS) or Path(p).suffix.lower() in EXT or CASC.search(p)
            or p.split("/")[0] not in TOP or (p.startswith("files/") and not p.startswith("files/data/"))]


class NoGameFiles(unittest.TestCase):
    def test_rules(self):
        bad = ["pd2-data/x.txt", "generated/data/a.txt", "a/b.d2s", "x/data.012", "x/.build.info", "out/data/a.txt",
               "import/a.txt", "dumps/D2R.DMP", "plugin/core.dll", "newtop/a.txt", "files/other/a.txt", "patches/010-a.json"]
        good = ["files/data/global/excel/skills.txt", "files/data/hd/x.sprite", "merge/art/items/a.png", "tools/x.py"]
        self.assertEqual(offenders(bad + good), bad)

    def test_tracked_tree(self):
        try:
            files = subprocess.run(["git", "-C", str(ROOT), "ls-files"], capture_output=True, text=True,
                                   check=True).stdout.splitlines()
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("not a git checkout")
        self.assertEqual(offenders(files), [], "game install, save, dump or unlisted files are tracked (ADR 0007)")


if __name__ == "__main__":
    unittest.main()
