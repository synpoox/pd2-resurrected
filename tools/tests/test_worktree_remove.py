"""tools/worktree_remove.ps1 on throwaway repos in temp dirs (#796). Windows PowerShell only: skipped elsewhere (CI)."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "worktree_remove.ps1"
PS = shutil.which("powershell") if os.name == "nt" else None
ENV = {**os.environ, "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
       "GIT_COMMITTER_EMAIL": "t@t"}


def git(cwd, *a):
    return subprocess.run(["git", "-C", str(cwd), *a], check=True, capture_output=True, text=True, env=ENV).stdout.strip()


def junction(link, target):
    subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, capture_output=True)


@unittest.skipUnless(PS, "needs Windows PowerShell")
class WorktreeRemove(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.main = self.tmp / "main"
        (self.main / "tools").mkdir(parents=True)
        shutil.copy(SCRIPT, self.main / "tools")
        git(self.main, "init", "-q", "-b", "main")
        git(self.main, "add", "-A")
        git(self.main, "commit", "-qm", "init")
        git(self.tmp, "clone", "-q", "--bare", str(self.main), str(self.tmp / "origin.git"))
        git(self.main, "remote", "add", "origin", str(self.tmp / "origin.git"))
        git(self.main, "fetch", "-q", "origin")
        (self.main / ".git/info/exclude").write_text("data\nplanted\nsub\n")  # links are untracked, not "dirty"
        self.wt = self.tmp / "wt"
        git(self.main, "worktree", "add", "-q", str(self.wt), "-b", "feat")
        self.stubs = self.tmp / "stubs"
        self.stubs.mkdir()

    def run_script(self, *args, cwd=None, extra_env=None):
        env = {**ENV, "PATH": f"{self.stubs}{os.pathsep}{ENV['PATH']}", **(extra_env or {})}
        r = subprocess.run([PS, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(self.main / "tools/worktree_remove.ps1"),
                            "-Path", str(self.wt), *args], cwd=cwd or self.tmp, capture_output=True, text=True, env=env)
        return r, r.stdout + r.stderr

    def shared(self, n=2):
        d = self.tmp / "shared"
        d.mkdir()
        for i in range(n):
            (d / f"f{i}.txt").write_text("x")
        return d

    def test_refuses_run_from_inside(self):
        r, out = self.run_script(cwd=self.wt)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("is inside", out)
        self.assertTrue(self.wt.exists())

    def test_removes_and_keeps_junction_target(self):
        d = self.shared()
        junction(self.wt / "data", d)
        r, out = self.run_script()
        self.assertEqual(r.returncode, 0, out)
        self.assertFalse(self.wt.exists())
        self.assertEqual(len(list(d.iterdir())), 2)

    def test_relative_symlink_target_resolves_against_link_folder(self):
        d = self.shared()
        link = self.wt / "sub" / "data"
        link.parent.mkdir()
        made = subprocess.run(["cmd", "/c", "mklink", "/D", str(link), r"..\..\shared"], capture_output=True)
        if made.returncode:
            self.skipTest("cannot create symlinks here")
        r, out = self.run_script(cwd=self.tmp / "origin.git")  # a cwd where ..\..\shared does not exist
        self.assertEqual(r.returncode, 0, out)
        self.assertIn(f"guard {d}: 2 files".lower(), out.lower())
        self.assertEqual(len(list(d.iterdir())), 2)

    def test_aborts_when_rescan_finds_links_left(self):
        d = self.shared()
        junction(self.wt / "data", d)
        # cmd stub: after the real rmdir, plant a new junction, as if something re-linked during the run.
        (self.stubs / "cmd.bat").write_text(
            '@echo off\r\n"%SystemRoot%\\System32\\cmd.exe" %*\r\n'
            'if not exist "%PLANT%" mklink /J "%PLANT%" "%SHARED%" >nul\r\n')
        plant = self.wt / "planted"
        r, out = self.run_script(extra_env={"PLANT": str(plant), "SHARED": str(d)})
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("reparse points remain", out)
        self.assertTrue(self.wt.exists())
        self.assertEqual(len(list(d.iterdir())), 2)
        subprocess.run(["cmd", "/c", "rmdir", str(plant)], capture_output=True)

    def delete_branch_run(self, gh_prints):
        (self.wt / "new.txt").write_text("x")
        git(self.wt, "add", "-A")
        git(self.wt, "commit", "-qm", "work")
        tip = git(self.wt, "rev-parse", "HEAD")
        # gh stub: no network; prints the merged PR head SHAs the test asks for.
        (self.stubs / "gh.bat").write_text("@echo off\r\necho %GH_OUT%\r\n")
        r, out = self.run_script("-DeleteBranch", extra_env={"GH_OUT": gh_prints or tip})
        self.assertEqual(r.returncode, 0, out)
        return out, git(self.main, "branch", "--list", "feat")

    def test_delete_branch_when_merged_pr_head_is_tip(self):
        out, left = self.delete_branch_run(None)
        self.assertIn("a merged PR's head is its tip", out)
        self.assertEqual(left, "")

    def test_keeps_branch_when_no_merged_pr_has_tip(self):
        out, left = self.delete_branch_run("0" * 40)
        self.assertIn("kept branch feat", out)
        self.assertIn("feat", left)


if __name__ == "__main__":
    unittest.main()
