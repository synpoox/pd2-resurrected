"""#965: the retired PD2 DT1/DS1/preset converter is neither wired in nor allowed to leave output behind."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_merge  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RETIRED = ("pd2_presets", "pd2_painter", "pd2_terrain", "pd2_tiles", "pd2_dt1_copies", "pd2_overrides", "pd2_ds1",
           "pd2_automap", "pd2_coverage", "pd2_fidelity", "pd2_parity", "pd2_edge_tour", "pd2_force_ds1", "pd2_warps")


class NoConverterTests(unittest.TestCase):
    def test_no_rule_file_or_tool_of_the_converter_is_left(self):
        for m in RETIRED:
            self.assertFalse((ROOT / "tools" / f"{m}.py").exists(), m)
        self.assertFalse((ROOT / "tools" / "presets").exists())
        for rule in ("level-files.tsv", "presets-hd.tsv", "presets-overrides.tsv"):
            self.assertNotIn(rule, pd2_merge.RULES)
            self.assertFalse((ROOT / "merge" / rule).exists(), rule)

    def test_every_merge_rule_file_has_a_rule_and_the_other_way_round(self):
        files = {p.name for p in (ROOT / "merge").iterdir() if p.is_file() and p.suffix != ".md"}
        self.assertEqual(files, set(pd2_merge.RULES))

    def test_the_extract_takes_no_pd2_dt1_ds1_or_automap_cels(self):
        import json
        extract = json.loads((ROOT / "pd2-baseline.json").read_text("utf-8"))["extract"]
        patterns = [p for v in extract.values() for p in v]
        self.assertEqual([p for p in patterns if p.endswith((".dt1", ".ds1")) or "maximap" in p], [])

    def test_legacy_output_is_named(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp)
            self.assertEqual(pd2_merge.legacy_output(data), [])
            (data / "global" / "tiles").mkdir(parents=True)
            (data / "hd" / "env" / "preset").mkdir(parents=True)
            self.assertEqual(pd2_merge.legacy_output(data), ["global/tiles", "hd/env/preset"])


if __name__ == "__main__":
    unittest.main()


class PrecheckTests(unittest.TestCase):
    def test_release_precheck_refuses_legacy_output(self):
        import release_precheck
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "out" / "data"
            data.mkdir(parents=True)
            self.assertEqual(release_precheck.main(["--root", tmp]), 0)
            (data / "global" / "tiles").mkdir(parents=True)
            self.assertEqual(release_precheck.main(["--root", tmp]), 1)
