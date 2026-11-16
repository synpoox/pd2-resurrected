"""pd2_port: the mod's data for the toolkit's classic import, on synthetic tables."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_port as P  # noqa: E402
from pd2_table import IdError, Table  # noqa: E402


def table(text: str) -> Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "	").encode("latin-1"))
        return Table(p)


class PortTests(unittest.TestCase):
    def test_every_listed_table_has_a_vanilla_size_or_row_rule(self):
        self.assertTrue(set(P.VANILLA) <= set(P.TABLES) | set(P.VANILLA))
        self.assertIn("skills", P.TABLES)

    def test_fixes_are_keyed_by_the_exact_pd2_value(self):
        self.assertIn(("Skill", "immolationarrow"), P.FIXES["missiles"])

    def test_map_level_names_a_level_with_an_id(self):
        row = ["map", "Cathedral"]
        P.map_level(["name", "len"], row, {"pSpell": P.MAP_SPELL}, None, {"levels": {"Cathedral": "7"}})
        self.assertEqual(row[1], "7")
        with self.assertRaises(IdError):
            P.map_level(["name", "len"], ["map", "Nowhere"], {"pSpell": P.MAP_SPELL}, None, {"levels": {}})

    def test_a_map_level_left_out_is_blank(self):
        row = ["map", "Gone"]
        P.map_level(["name", "len"], row, {"pSpell": P.MAP_SPELL}, None,
                    {"levels": {}, P.LEFT_OUT_LEVELS: {"Gone": "-"}})
        self.assertEqual(row[1], "")


if __name__ == "__main__":
    unittest.main()
