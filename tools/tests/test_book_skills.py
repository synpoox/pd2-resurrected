"""Unlimited books link to their skill-bar skill through books.txt, like the tomes (#1591)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import data_tables as D  # noqa: E402


class BookSkills(unittest.TestCase):
    def setUp(self):
        D.need(self)
        self.books = {r["BookSpellCode"]: r for r in D.rows("books") if r.get("BookSpellCode")}
        self.skills = {r["skill"] for r in D.rows("skills")}
        self.misc = {r["code"]: r for r in D.rows("misc") if r.get("code")}

    def test_unlimited_books_link_their_skill(self):
        for code, skill in (("rtp", "Book of Unlimited Townportal"), ("rid", "Book of Unlimited Identify")):
            row = self.books[code]
            self.assertEqual(row["BookSkill"], skill)
            self.assertIn(skill, self.skills)
            self.assertEqual(row["pSpell"], self.misc[code]["pSpell"])


if __name__ == "__main__":
    unittest.main()
