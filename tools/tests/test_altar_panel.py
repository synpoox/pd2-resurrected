"""The Sacrifice Altar's panel layout (#1192) matches the names in plugin/core-events/altar_panel.cpp."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "plugin/core-events/altar_panel.cpp").read_text("utf-8")
LAYOUT = ROOT / "files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json"
LEGACY = LAYOUT.with_name("SacrificeAltar.json")  # #1410: legacy (G key) loads the non-hd layout; without it the panel fails to open


def constant(name):
    return re.search(rf'constexpr char {name}\[\] = "([^"]+)";', SOURCE).group(1)


class AltarPanel(unittest.TestCase):
    def test_layout_matches_plugin(self):
        for layout in (LAYOUT, LEGACY):
            with self.subTest(layout.name):
                self.check_layout(json.loads(layout.read_text("utf-8")))  # strict: a plugin layout allows no trailing commas

    def check_layout(self, root):
        self.assertEqual(root["type"], "Panel")
        self.assertEqual(root["name"], "core-maps/" + constant("PanelId"))
        kids = {c["name"]: c for c in root["children"]}
        grid = kids[constant("SlotName")]
        self.assertEqual(grid["type"], "InventoryGridWidget")
        self.assertEqual(grid["fields"]["cellCount"], {"x": 1, "y": 1})
        self.assertEqual(kids["confirm"]["fields"]["onClickMessage"], "PanelManager:OpenPanel:" + constant("ConfirmText"))
        for name in ("close",):
            self.assertEqual(kids[name]["fields"]["onClickMessage"], "PanelManager:TogglePanel:" + root["name"])

    def test_amulet_slot_in_cube_square(self):  # #1398: cube panel, stone square (art), bordered amulet slot centred in it, no inner X
        kids = {c["name"]: c for c in json.loads(LAYOUT.read_text("utf-8"))["children"]}
        self.assertNotIn("cancel", kids)
        self.assertEqual(kids["background"]["fields"]["filename"], r"PANEL\pd2r\altar_bg")
        self.assertTrue((ROOT / "files/data/hd/global/ui/panel/pd2r/altar_bg.lowend.sprite").is_file())
        self.assertEqual(kids["slot_bg"]["fields"]["filename"], r"PANEL\Inventory\Inventory_Paperdoll_Amulet")
        self.assertEqual(kids["slot_bg"]["fields"]["rect"], kids[constant("SlotName")]["fields"]["rect"])
        cell, button = 98, 117  # $ItemCellSize, TransmuteButton frame
        # altar_bg's slot frame is baked around the cell at (529, 683): the square's opening is x 385-769, y 540-924
        self.assertEqual(kids["slot_bg"]["fields"]["rect"], {"x": 529, "y": 683})
        self.assertAlmostEqual(kids["confirm"]["fields"]["rect"]["x"] + button / 2, 529 + cell / 2, delta=0.5)  # 117 is odd
        centre = 1162 / 2  # $LeftPanelRect width, _profilehd.json
        prompt = kids["prompt"]["fields"]["rect"]
        self.assertEqual(prompt["x"] + prompt["width"] / 2, centre)

    def test_no_cube_desecration(self):
        self.assertNotIn("Desecrate", (ROOT / "plugin/core-events/cube_ops.cpp").read_text("utf-8"))

    def test_altar_once_per_game(self):  # #1357: stage 9 (set by the attempt) keeps the panel shut
        lucion = (ROOT / "plugin/core-events/lucion.cpp").read_text("utf-8")
        body = lucion.split("auto OperateSacrificeAltar", 1)[1].split("auto InstallLucion", 1)[0]
        self.assertRegex(body, r"LucionStageKey\) == AttemptedStage")
        self.assertLess(body.index("AttemptedStage"), body.index("OpenAltarPanel"))


if __name__ == "__main__":
    unittest.main()
