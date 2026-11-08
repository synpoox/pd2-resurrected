"""Unit tests on synthetic fixtures: no PD2 install or data needed (ADR 0002). The extract and the merge driver are
d2rAI-toolkit's (shared/classic_import, tested there); these test pd2's own part: its baseline, rules and wiring."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_extract  # noqa: E402
import pd2_ids  # noqa: E402
import pd2_merge  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def toolkit_or_skip(test: unittest.TestCase):
    try:
        return pd2_extract.classic("extract")
    except ValueError as e:  # CI has no d2rAI-toolkit (private)
        test.skipTest(str(e))


class BaselineTests(unittest.TestCase):
    def test_extract_patterns_name_only_pinned_archives(self):
        baseline = json.loads((ROOT / "pd2-baseline.json").read_text("utf-8"))
        self.assertEqual(set(baseline["extract"]) - set(baseline["archives"]), set())
        self.assertEqual(baseline["extract"]["pd2data.mpq"], ["*"])


class MergeTests(unittest.TestCase):
    def project(self, tmp: Path, stamp: bytes | None) -> Path:
        (tmp / "pd2-baseline.json").write_text("B")
        if stamp is not None:
            (tmp / "pd2-data").mkdir()
            (tmp / "pd2-data" / "baseline.json").write_bytes(stamp)
        (tmp / "merge").mkdir()
        (tmp / "merge" / "README.md").write_text("notes")
        (tmp / "d2r" / "global" / "excel").mkdir(parents=True)
        return tmp

    def test_every_committed_rule_file_has_a_rule_kind(self):
        files = {p.name for p in (ROOT / "merge").glob("*") if p.is_file() and p.suffix != ".md"}
        self.assertEqual(files - set(pd2_merge.RULES), set())

    def test_refuses_a_stale_extract_and_names_pd2_extract(self):
        ex = toolkit_or_skip(self)
        for stamp, msg in [(None, "no baseline stamp"), (ex.stamp(b"A"), "another baseline")]:
            with self.subTest(msg), tempfile.TemporaryDirectory() as tmp:
                root = self.project(Path(tmp), stamp)
                with self.assertRaisesRegex(pd2_merge.MergeError, f"{msg}.*tools/pd2_extract.py"):
                    pd2_merge.merge(root, root / "d2r")
                self.assertFalse((root / "generated").exists())

    def test_runs_pd2_rules_and_reports_an_id_error(self):
        ex = toolkit_or_skip(self)
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), ex.stamp(b"B"))
            (root / "merge" / "ids.json").write_text("{}")
            seen = []
            pd2_merge.RULES["ids.json"] = lambda rule, pd2, d2r, out: seen.append((rule.name, pd2.name, out.name))
            try:
                self.assertEqual(pd2_merge.merge(root, root / "d2r"), ["ids.json"])
                self.assertEqual(seen, [("ids.json", "data", "data")])

                def bad_id(rule, pd2, d2r, out):
                    raise pd2_ids.IdError("skills: id 400 taken twice")
                pd2_merge.RULES["ids.json"] = bad_id
                with self.assertRaisesRegex(pd2_merge.MergeError, "^skills: id 400 taken twice$"):
                    pd2_merge.merge(root, root / "d2r")
                self.assertFalse((root / "generated").exists())
            finally:
                del pd2_merge.RULES["ids.json"]

    def test_main_reports_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), None)
            with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
                self.assertEqual(pd2_merge.main(["--root", str(root), "--d2r", str(root / "d2r")]), 1)


if __name__ == "__main__":
    unittest.main()
