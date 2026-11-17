"""The Sacrifice Altar's panel layout (#1192) matches the names in plugin/core-events/altar_panel.cpp."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "plugin/core-events/altar_panel.cpp").read_text("utf-8")
LAYOUT = ROOT / "files/data/global/ui/layouts/core-maps/SacrificeAltarhd.json"


def constant(name):
    return re.search(rf'constexpr char {name}\[\] = "([^"]+)";', SOURCE).group(1)


class AltarPanel(unittest.TestCase):
    def test_layout_matches_plugin(self):
        root = json.loads(LAYOUT.read_text("utf-8"))  # strict: a plugin layout allows no trailing commas
        self.assertEqual(root["type"], "Panel")
        self.assertEqual(root["name"], "core-maps/" + constant("PanelId"))
        kids = {c["name"]: c for c in root["children"]}
        grid = kids[constant("SlotName")]
        self.assertEqual(grid["type"], "InventoryGridWidget")
        self.assertEqual(grid["fields"]["cellCount"], {"x": 1, "y": 1})
        self.assertEqual(kids["confirm"]["fields"]["onClickMessage"], "PanelManager:OpenPanel:" + constant("ConfirmText"))
        for name in ("cancel", "close"):
            self.assertEqual(kids[name]["fields"]["onClickMessage"], "PanelManager:TogglePanel:" + root["name"])

    def test_no_cube_desecration(self):
        self.assertNotIn("Desecrate", (ROOT / "plugin/core-events/cube_ops.cpp").read_text("utf-8"))


if __name__ == "__main__":
    unittest.main()
