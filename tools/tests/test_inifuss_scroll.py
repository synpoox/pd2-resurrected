"""#758: the Scroll of Inifuss (bks) and the Key to the Cairn Stones (bkd) can be read, as in D2R."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402


class InifussScroll(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.misc = {r["code"]: r for r in D.rows("misc") if r.get("code")}

    def test_scroll_is_useable(self):
        r = self.misc["bks"]
        self.assertEqual((r["useable"], r["pSpell"]), ("1", "0"))

    def test_deciphered_scroll_reads_as_a_scroll(self):
        r = self.misc["bkd"]
        self.assertEqual((r["useable"], r["pSpell"], r["spelldesc"], r["spelldescstr"]), ("1", "14", "1", "ReadScroll"))


if __name__ == "__main__":
    unittest.main()
