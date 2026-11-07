"""Unit tests on synthetic fixtures: no PD2 install or data needed (ADR 0002)."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import mpq  # noqa: E402
import pd2_extract  # noqa: E402
import pd2_merge  # noqa: E402


class MpqTests(unittest.TestCase):
    def test_table_keys(self):
        # the well-known keys every MPQ reader uses for its hash and block tables
        self.assertEqual(mpq.hash_string("(hash table)", 3), 0xC3AF3770)
        self.assertEqual(mpq.hash_string("(block table)", 3), 0xEC83B3A3)

    def test_explode(self):
        # the test vector from zlib's contrib/blast
        self.assertEqual(mpq.explode(bytes.fromhex("00048224258f807f")), b"AIAIAIAIAIAIA")


class ExtractTests(unittest.TestCase):
    def test_refuses_an_install_that_is_not_the_baseline(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "install").mkdir()
            (tmp / "install" / "pd2data.mpq").write_bytes(b"not the baseline")
            baseline = tmp / "pd2-baseline.json"
            baseline.write_text(json.dumps({"archives": {"pd2data.mpq": "0" * 64, "pd2maps.mpq": "0" * 64}}))
            err = io.StringIO()
            with redirect_stderr(err):
                rc = pd2_extract.extract(tmp / "install", tmp / "pd2-data", baseline)
            self.assertEqual(rc, 1)
            self.assertIn("pd2data.mpq: SHA-256 differs", err.getvalue())
            self.assertIn("pd2maps.mpq: missing", err.getvalue())
            self.assertFalse((tmp / "pd2-data").exists())

    def test_skips_a_current_extract(self):
        class OneFile:  # pd2data.mpq's "*" takes it
            def __init__(self, path): pass
            def names(self): return ["data\\x.txt"]
            def read(self, name): return b""

        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            baseline = tmp / "pd2-baseline.json"
            baseline.write_text(json.dumps({"archives": {}}))
            out = tmp / "pd2-data"
            for stamp, skips in [(pd2_extract.stamp(baseline.read_bytes()), True),
                                 (pd2_extract.stamp(b"other"), False), (None, False)]:
                out.mkdir(exist_ok=True)
                (out / "keep").write_bytes(b"x")
                (out / "baseline.json").unlink(missing_ok=True)
                if stamp is not None:
                    (out / "baseline.json").write_bytes(stamp)
                log = io.StringIO()
                with mock.patch.object(pd2_extract.mpq, "Archive", OneFile), redirect_stdout(log):
                    self.assertEqual(pd2_extract.extract(tmp / "install", out, baseline), 0)
                self.assertEqual((out / "keep").exists(), skips, stamp)
                self.assertEqual("skipping" in log.getvalue(), skips, stamp)
                self.assertEqual((out / "baseline.json").read_bytes(), pd2_extract.stamp(baseline.read_bytes()))


class MergeTests(unittest.TestCase):
    def project(self, tmp: Path, stamp: bytes | None = pd2_extract.stamp(b"B")) -> Path:
        (tmp / "pd2-baseline.json").write_text("B")
        if stamp is not None:
            (tmp / "pd2-data").mkdir()
            (tmp / "pd2-data" / "baseline.json").write_bytes(stamp)
        (tmp / "merge").mkdir()
        (tmp / "merge" / "README.md").write_text("notes")
        return tmp

    def test_rebuilds_generated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp))
            stale = root / "generated" / "data" / "stale.txt"
            stale.parent.mkdir(parents=True)
            stale.write_text("old")
            self.assertEqual(pd2_merge.merge(root), [])
            self.assertTrue((root / "generated" / "data").is_dir())
            self.assertFalse(stale.exists())

    def test_every_committed_rule_file_has_a_rule_kind(self):
        rules = Path(__file__).resolve().parents[2] / "merge"
        files = {p.name for p in rules.glob("*") if p.is_file() and p.suffix != ".md"}
        self.assertEqual(files - set(pd2_merge.RULES), set())

    def test_runs_rules_by_file_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp))
            (root / "merge" / "ids.json").write_text("{}")
            seen = []
            pd2_merge.RULES["ids.json"] = lambda rule, pd2, d2r, out: seen.append((rule.name, pd2.name, out.name))
            try:
                (root / "d2r" / "global" / "excel").mkdir(parents=True)
                self.assertEqual(pd2_merge.merge(root, root / "d2r"), ["ids.json"])
            finally:
                del pd2_merge.RULES["ids.json"]
            self.assertEqual(seen, [("ids.json", "data", "data")])

    def test_refuses_missing_or_stale_extract_and_unknown_rules(self):
        for stamp, rule, msg in [(None, None, "no baseline stamp"), (pd2_extract.stamp(b"A"), None, "another baseline"),
                                 (b"B", None, "older pd2_extract"),  # the right baseline, extracted before #351
                                 (pd2_extract.stamp(b"B"), "mystery.json", "no rule kind")]:
            with self.subTest(msg), tempfile.TemporaryDirectory() as tmp:
                root = self.project(Path(tmp), stamp)
                if rule:
                    (root / "merge" / rule).write_text("{}")
                with self.assertRaisesRegex(pd2_merge.MergeError, msg):
                    pd2_merge.merge(root)
                self.assertFalse((root / "generated").exists())

    def test_main_reports_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.project(Path(tmp), None)
            with redirect_stderr(io.StringIO()), redirect_stdout(io.StringIO()):
                self.assertEqual(pd2_merge.main(["--root", str(root)]), 1)


if __name__ == "__main__":
    unittest.main()
