"""pd2_table's finder of the toolkit (the fake toolkits here hold a copy of the real one)."""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_table  # noqa: E402


class ToolkitTests(unittest.TestCase):
    """toolkit() loads the toolkit's own finder; the fake toolkits here hold a copy of the real one."""

    def fake(self, base: Path) -> None:
        try:
            real = pd2_table.toolkit(Path(__file__).resolve().parents[2]) / "tools" / "toolkit_path.py"
        except ValueError:
            self.skipTest("d2rAI-toolkit not found (set D2R_TOOLKIT)")
        (base / "d2rAI-toolkit" / "modkit").mkdir(parents=True)
        (base / "d2rAI-toolkit" / "tools").mkdir()
        shutil.copy(real, base / "d2rAI-toolkit" / "tools" / "toolkit_path.py")

    def test_finds_the_toolkit_from_a_nested_worktree(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ):
            os.environ.pop("D2R_TOOLKIT", None)
            base = Path(d).resolve()
            self.fake(base)
            tree = base / "repo" / ".claude" / "worktrees" / "w"
            tree.mkdir(parents=True)
            self.assertEqual(pd2_table.toolkit(tree), base / "d2rAI-toolkit")
            self.assertEqual(pd2_table.toolkit(base / "repo"), base / "d2rAI-toolkit")

    def test_no_toolkit_is_a_value_error(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ):
            os.environ.pop("D2R_TOOLKIT", None)
            with mock.patch.object(pd2_table, "HERE", Path(d)), self.assertRaises(ValueError):
                pd2_table.toolkit(Path(d) / "repo")

    def test_a_wrong_pin_warns_and_strict_refuses(self):
        with tempfile.TemporaryDirectory() as d, mock.patch.dict(os.environ):
            os.environ.pop("D2R_TOOLKIT", None)
            os.environ.pop("D2R_TOOLKIT_STRICT", None)  # CI exports it
            base = Path(d).resolve()
            self.fake(base)
            repo = base / "repo"
            (repo / ".git").mkdir(parents=True)
            (repo / "toolkit.version").write_text("1234567", encoding="ascii")
            kit = base / "d2rAI-toolkit"
            subprocess.run(["git", "init", "-q", str(kit)], check=True)
            subprocess.run(["git", "-C", str(kit), "add", "."], check=True)
            subprocess.run(["git", "-C", str(kit), "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "x"],
                           check=True)
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                self.assertEqual(pd2_table.toolkit(repo), kit)
            self.assertIn("pins 1234567", err.getvalue())
            os.environ["D2R_TOOLKIT_STRICT"] = "1"
            pd2_table.toolkit.cache_clear()
            with self.assertRaises(ValueError):
                pd2_table.toolkit(repo)
