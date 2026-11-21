"""Keep both quest scrolls readable and preserve the native stone order handler (#758, #1538)."""
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

    def test_almanac_leaves_the_deciphered_scroll_on_the_native_handler(self):
        almanac = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "almanac.cpp"
        source = almanac.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r'if \(item == nullptr \|\| itemCode\(item\) != D2RL::Items::MakeItemCode\("rid"\)\) \{\s*'
            r'return [^\n]+\(AlmanacUseSlotOriginal\)\(game, player, item\);',
        )


if __name__ == "__main__":
    unittest.main()
