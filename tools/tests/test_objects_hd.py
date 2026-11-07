"""pd2_objects_hd on synthetic data (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_objects_hd as H  # noqa: E402

D2R_OBJECTS = "Class\tName\r\nPortalPermanent\tPortal\r\n"
PERMANENT = '{"dependencies": {"particles": [{"path": "red.particles"}]}, "vfx": "red.particles"}'


def run(rule: str, merged: str) -> Path:
    tmp = Path(tempfile.mkdtemp())
    d2r, out = tmp / "d2r", tmp / "out"
    for root, text in ((d2r, D2R_OBJECTS), (out, merged)):
        (root / "global" / "excel").mkdir(parents=True)
        (root / "global" / "excel" / "objects.txt").write_text(text, "latin-1")
    (d2r / "hd" / "objects" / "vfx_only").mkdir(parents=True)
    (d2r / "hd" / "objects" / "objects.json").write_text('{"portal_permanent": {"asset_path": "vfx_only"}}')
    (d2r / "hd" / "objects" / "vfx_only" / "portal_permanent.json").write_text(PERMANENT)
    (tmp / "rule.tsv").write_text(rule)
    H.apply(tmp / "rule.tsv", tmp, d2r, out)
    return out / "hd" / "objects"


class ObjectHdTests(unittest.TestCase):
    def test_new_object_copies_like(self):
        hd = run("# class\tlike\nxactdarkportal_grey\tportal_permanent\n",
                 D2R_OBJECTS + "xactdarkportal_grey\txactdarkportal_grey\r\n")
        self.assertEqual(json.loads((hd / "objects.json").read_text())["xactdarkportal_grey"], {"asset_path": "vfx_only"})
        self.assertEqual((hd / "vfx_only" / "xactdarkportal_grey.json").read_text(), PERMANENT)

    def test_particles_column_swaps_the_effect(self):
        hd = run("xactportal_gold\tportal_permanent\tgold.particles\n", D2R_OBJECTS + "xactportal_gold\tx\r\n")
        self.assertEqual((hd / "vfx_only" / "xactportal_gold.json").read_text(), PERMANENT.replace("red", "gold"))

    def test_bad_lines_fail(self):
        with self.assertRaisesRegex(H.ObjectHdError, "not a PD2-only"):
            run("PortalPermanent\tportal_permanent\n", D2R_OBJECTS)
        with self.assertRaisesRegex(H.ObjectHdError, "no D2R object json"):
            run("xact\tnope\n", D2R_OBJECTS + "xact\txact\r\n")


if __name__ == "__main__":
    unittest.main()
