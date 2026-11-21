"""#1588: every missile whose classic DCC the issue lists has an HD model in the committed missiles.json."""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pd2_missiles_hd as H  # noqa: E402
from pd2_table import Table, excel  # noqa: E402

CELS = """bigheadredlightningmissile bonefingers electricnova_red firenova greenbonespeartrail holylight001 infernotrail
iskatu_hurricane_rocks1 iskatu_hurricane_rocks2 iskatubliz iskatublizimpact iskatulighningmissile iskatumagicorb mslcpl
mslcpm mslcps msllpl msllpm msllps necrofiremedium necrofiresmall necrofissurecrack necrofissuresmoke volcano_rock_white
zhararmageddonfire zhararmageddonrock zharbolt zharexplode""".split()


class Issue1588(unittest.TestCase):
    def test_every_listed_missile_has_hd_model(self):
        data = ROOT / "files" / "data"
        models = json.loads((data / H.HD).read_text("utf-8-sig"))
        t = Table(excel(data) / "missiles.txt")
        c = t.header.index("CelFile")
        rows = [r[0] for r in t.rows if r[0] and H.cel(r, c) in CELS]
        self.assertEqual({H.cel(r, c) for r in t.rows if r[0]} & set(CELS), set(CELS))
        for name in rows:
            self.assertIn(H.hd_key(name), models, name)
            self.assertIn(H.hd_key(name, lower=False), models, name)


if __name__ == "__main__":
    unittest.main()
