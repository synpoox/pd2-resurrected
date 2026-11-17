"""The classic COFs of PD2-only monsters ship: the client's hover test reads their box (#1092)."""
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files" / "data"


class MonsterCofTests(unittest.TestCase):
    def test_cof_exists(self):
        for code in ("$1", "$2", "$3", "$4", "$7", "$8"):
            with self.subTest(code=code):
                self.assertTrue(list((DATA / "global/monsters" / code / "cof").glob(f"{code}*hth.cof")))



if __name__ == "__main__":
    unittest.main()
