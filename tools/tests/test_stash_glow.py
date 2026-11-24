"""The Maps tab's glow (#2116): stash_home.h's Tiers matches its tier lists, and in both stash layouts every tier
button (pd2rTier<n>) and base button (pd2rBase<n>_<i>) has its glow (pd2rTierGlow<n>, pd2rBaseGlow<n>_<i>) at the
button's (the base's box) spot, drawn after the button, so a new tier or base glows with no code change."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOME = ROOT / "plugin/common/stash_home.h"
LAYOUTS = [ROOT / "files/data/global/ui/layouts/bankexpansionlayouthd.json",
           ROOT / "files/data/global/ui/layouts/controller/bankexpansionlayouthd.json"]
WIDGET = re.compile(r'"type": "(\w+)", "name": "(pd2r(?:Tier|TierGlow|Base|BaseBox|BaseGlow)\d[\d_]*)"')
RECT = re.compile(r'"x": (-?\d+), "y": (-?\d+)')


def widgets(text):
    """name -> (type, x, y, position in the file)"""
    out = {}
    for m in WIDGET.finditer(text):
        r = RECT.search(text, m.end())
        out[m.group(2)] = (m.group(1), int(r.group(1)), int(r.group(2)), m.start())
    return out


class StashGlow(unittest.TestCase):
    def test_tiers_match_the_lists(self):
        text = HOME.read_text("utf-8")
        tiers = int(re.search(r"constexpr int32_t Tiers = (\d+);", text).group(1))
        self.assertEqual(tiers, len(re.findall(r"const char\* Tier\d+\[\]", text)))

    def test_every_button_has_its_glow(self):
        for layout in LAYOUTS:
            w = widgets(layout.read_text("utf-8"))
            pairs = [(n, n.replace("Tier", "TierGlow"), n) for n in w if re.fullmatch(r"pd2rTier\d+", n)]
            pairs += [(n, n.replace("Base", "BaseGlow"), n.replace("Base", "BaseBox"))
                      for n in w if re.fullmatch(r"pd2rBase\d+_\d+", n)]
            self.assertGreater(len(pairs), 0, layout)
            for button, glow, spot in pairs:
                with self.subTest(layout=layout.name, glow=glow):
                    self.assertIn(glow, w)
                    self.assertEqual(w[glow][0], "ImageWidget")
                    self.assertEqual(w[glow][1:3], w[spot][1:3])
                    self.assertGreater(w[glow][3], w[button][3])  # drawn over the button


if __name__ == "__main__":
    unittest.main()
