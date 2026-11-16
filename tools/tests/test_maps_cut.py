"""PD2's map system is cut with nothing dangling (#964): the data rule's pieces, and the plugin's sources and includes."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pd2_cutmaps  # noqa: E402
from pd2_table import Table  # noqa: E402

CORE_EVENTS = ROOT / "plugin" / "core-events"
REMOVED = ["map_stats", "map_deaths", "unique_maps", "unique_map_drops", "zhar", "map_bosses", "map_boss_ai"]


class Fake:
    """Just what the cut functions read of a Table."""

    def __init__(self, header, rows):
        self.header, self.rows = header, rows


class DataCut(unittest.TestCase):
    def test_cube_recipe_naming_a_cut_item_or_property_goes(self):
        header = ["description", "input 1", "output", "mod 1"]
        rows = [["keep", "jew", "r01", ""], ["map in", "t1me", "r01", ""], ["prop", "jew", "r01", "mapfoo"], ["Expansion", "", "", ""]]
        cube = Fake(header, rows)
        self.assertEqual(pd2_cutmaps.cut_cube(cube, {"t1me"}, {"mapfoo"}), 2)
        self.assertEqual([r[0] for r in cube.rows], ["keep", "Expansion"])

    def test_affix_with_a_cut_property_is_neutralised_not_deleted(self):
        header = ["Name", "spawnable", "rare", "itype1", "mod1code", "mod1param", "mod1min", "mod1max"]
        t = Fake(header, [["Mapped", "1", "1", "weap", "mapfoo", "", "1", "2"], ["Plain", "1", "1", "weap", "dmg%", "", "1", "2"]])
        self.assertEqual(pd2_cutmaps.neutralise(t, "mod{n}code", {"mapfoo"}, False), 1)
        self.assertEqual(t.rows[0], ["Mapped", "0", "0", "", "", "", "", ""])
        self.assertEqual(t.rows[1][1], "1")

    def test_undefined_map_class_is_stripped_to_nodrop(self):
        header = ["Treasure Class", "NoDrop", "Item1", "Prob1", "Item2", "Prob2"]
        tc = Fake(header, [["Map Miniboss t3", "5", "Map Uitem", "3", "gld", "2"]])
        pd2_cutmaps.undefined_maps(tc, [2, 4], {h: i for i, h in enumerate(header)})
        self.assertEqual(tc.rows[0], ["Map Miniboss t3", "8", "gld", "2", "", ""])

    def test_rule_file_names_real_types(self):
        types = pd2_cutmaps.read_rule(ROOT / "merge" / "z-maps-cut.txt")
        self.assertIn("t1me", types)
        self.assertNotIn("ubr", types)  # the uber keys stay
        self.assertNotIn("cmap", types)  # so do the native maps


class PluginCut(unittest.TestCase):
    def test_removed_sources_are_gone_and_unnamed(self):
        for name in REMOVED:
            self.assertFalse((CORE_EVENTS / f"{name}.cpp").exists(), name)
            self.assertFalse((CORE_EVENTS / f"{name}.h").exists(), name)
        pattern = re.compile(r'#include "(%s)\.h"|core-events/(%s)\.cpp' % ("|".join(REMOVED), "|".join(REMOVED)))
        for p in list((ROOT / "plugin").rglob("*.cpp")) + list((ROOT / "plugin").rglob("*.h")) + [ROOT / "plugin" / "CMakeLists.txt"]:
            if "build" in p.relative_to(ROOT / "plugin").parts[:1]:
                continue
            self.assertIsNone(pattern.search(p.read_text(encoding="utf-8", errors="replace")), p.name)

    def test_every_cmake_source_exists(self):
        for f in re.findall(r"^\s+(core-events/\S+\.(?:cpp|rc))\s*$", (ROOT / "plugin" / "CMakeLists.txt").read_text(encoding="utf-8"), re.M):
            self.assertTrue((ROOT / "plugin" / f).exists(), f)


if __name__ == "__main__":
    unittest.main()


class GateRow(unittest.TestCase):
    @unittest.skipUnless((ROOT / "pd2-data" / "data" / "global" / "excel").is_dir(), "needs the PD2 data")
    def test_deviations_gate_row_is_the_generated_one(self):
        path, row = pd2_cutmaps.deviations_row(ROOT)
        self.assertIn(row, path.read_text("utf-8").splitlines(), "run `py tools/pd2_cutmaps.py` to rewrite the Gate row")
        self.assertNotRegex(row, r"`\w+\.\*\.\*`")  # no whole-table pattern
