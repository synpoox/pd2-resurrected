"""presets.hooks.blockers: vis_box blockers stay out of the kit (#717)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import pd2_presets as P  # noqa: E402
from presets.hooks import blockers  # noqa: E402
from presets_fixtures import FLOOR, entity  # noqa: E402


class BlockerTests(unittest.TestCase):
    def test_a_vis_box_blocker_stays_out_of_the_kit(self):
        blocker = entity("blocker02", 3.0, 4.0)
        blocker["components"][1]["filename"] = "data/hd/env/light/vis_box/blocker/blocker02.model"
        self.assertTrue(blockers.blocker(blocker))
        lay = P.Layout(1, 1, {(0, 0): ("f", (FLOOR,))})
        kit = P.Kit()
        kit.add(lay, {"dependencies": {}, "entities": [blocker, entity("sack", 4.0, 4.0)]}, {("f", (FLOOR,))})
        self.assertEqual([e["name"] for _, ents in kit.instances[("f", (FLOOR,))] for e, _, _ in ents], ["sack"])


if __name__ == "__main__":
    unittest.main()
