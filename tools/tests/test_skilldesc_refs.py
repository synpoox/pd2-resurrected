"""Every skills.txt `skilldesc` names an exact-case skilldesc.txt row; D2R asserts on an index it cannot resolve (#1908)."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tests import data_tables as dt  # noqa: E402


class SkillDescRefs(unittest.TestCase):
    def test_skills_name_existing_skilldesc_rows(self):
        dt.need(self)
        rows = {r["skilldesc"] for r in dt.rows("skilldesc")}
        bad = [(r["skill"], r["skilldesc"]) for r in dt.rows("skills") if r["skilldesc"] and r["skilldesc"] not in rows]
        self.assertEqual(bad, [])


if __name__ == "__main__":
    unittest.main()
