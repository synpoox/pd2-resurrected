"""Nonstacking unlimited books use the native scroll skill path (#2124)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402


class BookSkills(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.books = {r["ScrollSpellCode"]: r for r in D.rows("books") if r.get("ScrollSpellCode")}
        self.skills = {r["skill"]: r for r in D.rows("skills")}
        self.misc = {r["code"]: r for r in D.rows("misc") if r.get("code")}

    def test_unlimited_books_link_their_skill(self):
        for code, skill in (("rtp", "Book of Unlimited Townportal"), ("rid", "Book of Unlimited Identify")):
            row = self.books[code]
            self.assertEqual(row["ScrollSkill"], skill)
            self.assertIn(skill, self.skills)
            self.assertEqual(row["pSpell"], self.misc[code]["pSpell"])

    def test_nonstacking_books_grant_one_scroll_skill_level(self):
        # D2R rejects non-book/scroll types; books with quantity zero grant no skill.
        for code in ("rtp", "rid"):
            with self.subTest(code=code):
                item = self.misc[code]
                self.assertEqual((item["type"], item["type2"]), ("ubru", "scro"))
                self.assertEqual(item["stackable"], "0")
                row = self.books[code]
                self.assertFalse(row["BookSpellCode"])
                skill = self.skills[row["ScrollSkill"]]
                self.assertEqual(skill["scroll"], "1")
                self.assertEqual(skill["rightskill"], "1")


if __name__ == "__main__":
    unittest.main()
