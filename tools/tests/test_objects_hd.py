"""pd2_objects_hd on synthetic data (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_objects_hd as H  # noqa: E402

D2R_OBJECTS = "Class\tName\r\nPortalPermanent\tPortal\r\n"


def run(rule: str, merged: str) -> Path:
    tmp = Path(tempfile.mkdtemp())
    d2r, out = tmp / "d2r", tmp / "out"
    for root, text in ((d2r, D2R_OBJECTS), (out, merged)):
        (root / "global" / "excel").mkdir(parents=True)
        (root / "global" / "excel" / "objects.txt").write_text(text, "latin-1")
    (d2r / "hd" / "objects" / "vfx_only").mkdir(parents=True)
    (d2r / "hd" / "objects" / "objects.json").write_text('{"portal_permanent": {"asset_path": "vfx_only"}}')
    (d2r / "hd" / "objects" / "vfx_only" / "portal_permanent.json").write_text('{"name": "portal_permanent"}')
    (tmp / "rule.tsv").write_text(rule)
    H.apply(tmp / "rule.tsv", tmp, d2r, out)
    return out / "hd" / "objects"


class ObjectHdTests(unittest.TestCase):
    def test_new_object_copies_like(self):
        hd = run("# class\tlike\nxactdarkportal_grey\tportal_permanent\n",
                 D2R_OBJECTS + "xactdarkportal_grey\txactdarkportal_grey\r\n")
        self.assertEqual(json.loads((hd / "objects.json").read_text())["xactdarkportal_grey"], {"asset_path": "vfx_only"})
        self.assertEqual((hd / "vfx_only" / "xactdarkportal_grey.json").read_text(), '{"name": "portal_permanent"}')

    def test_camel_class_gets_d2r_key(self):
        hd = run("LucionChest\tportal_permanent\n", D2R_OBJECTS + "LucionChest\tLucionChest\r\n")
        self.assertEqual(json.loads((hd / "objects.json").read_text())["lucion_chest"], {"asset_path": "vfx_only"})
        self.assertTrue((hd / "vfx_only" / "lucion_chest.json").is_file())
        self.assertEqual([H.key(c) for c in ("PortalPermanent", "Chest1B", "TownAct3GidBInnAltar", "SpecialChest100")],
                         ["portal_permanent", "chest_1_b", "town_act_3_gid_b_inn_altar", "special_chest_100"])

    def test_bad_lines_fail(self):
        with self.assertRaisesRegex(H.ObjectHdError, "not a PD2-only"):
            run("PortalPermanent\tportal_permanent\n", D2R_OBJECTS)
        with self.assertRaisesRegex(H.ObjectHdError, "no D2R object json"):
            run("xact\tnope\n", D2R_OBJECTS + "xact\txact\r\n")


if __name__ == "__main__":
    unittest.main()
