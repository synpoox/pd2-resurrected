"""Map affixes (#1305): every cmap-family affix row names only properties that exist and only the map item types."""
import re
import unittest

import data_tables

MAP_TYPES = {"cmap", "t1m", "t2m", "t3m"}
TABLES = ("magicprefix", "magicsuffix", "rareprefix", "raresuffix")

EXPECTED_ROLLING = {("magicprefix", n) for n in (
    "Large Huge Colossal Strong Mighty Formidable Unhealthy Sickly Diseased".split())} | {
    ("magicsuffix", "of " + n) for n in (
        "Swiftness Quickness Velocity Health Vitality Vim Imbalance Inequality Uneveness "
        "Hindrance Aversion Permeability Cracking Crumbling Disintegration "
        "Fumbling Bumbling Blundering".split())}


def map_rows(table):
    """Rows with a map item type among itype1-7 (cmap, or a PD2 tier type t<n>m)."""
    for r in data_tables.rows(table):
        types = {r[f"itype{n}"] for n in range(1, 8) if r.get(f"itype{n}")}
        if types & MAP_TYPES or any(re.fullmatch(r"t\dm", t) for t in types):
            yield r, types


class MapAffixes(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_rows_use_real_properties_and_map_types_only(self):
        props = {r["code"].lower() for r in data_tables.rows("properties")}
        seen = 0
        for table in TABLES:
            for r, types in map_rows(table):
                seen += 1
                name = f"{table}.{r.get('Name') or r.get('name')}"
                self.assertLessEqual(types, MAP_TYPES, name)
                for n in (1, 2, 3):
                    code = r.get(f"mod{n}code", "")
                    if code:
                        self.assertIn(code.lower(), props, f"{name} mod{n}code")
        self.assertGreater(seen, 50)

    def test_ported_affix_is_in_its_band(self):
        suffix = [(r, t) for r, t in map_rows("magicsuffix") if r["Name"] == "of Vim"]
        self.assertTrue(suffix)
        for _, types in suffix:
            self.assertEqual(types, {"t3m"})

    def test_rolling_set_is_the_expected_27(self):
        """spawnable 1, rare 1, frequency > 0: a dead pool (e.g. a lost `rare` flag) fails here."""
        rolling = {
            (t, r["Name"])
            for t in ("magicprefix", "magicsuffix")
            for r, types in map_rows(t)
            if types != {"cmap"}  # the tierless Cartographer placeholders roll too
            if r["spawnable"] == "1" and r["rare"] == "1" and int(r.get("frequency") or 0) > 0
        }
        self.assertEqual(rolling, EXPECTED_ROLLING)


if __name__ == "__main__":
    unittest.main()
