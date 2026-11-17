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

    def test_rolling_set_is_the_expected_102(self):
        """spawnable 1, rare 1, frequency > 0: a dead pool (e.g. a lost `rare` flag) fails here."""
        rolling = {
            (t, r["Name"])
            for t in ("magicprefix", "magicsuffix")
            for r, types in map_rows(t)
            if types != {"cmap"}  # the tierless Cartographer placeholders roll too
            if r["spawnable"] == "1" and r["rare"] == "1" and int(r.get("frequency") or 0) > 0
        }
        self.assertEqual(len(rolling), 102)  # 27 from #1305 + 75 from #1309-#1313
        self.assertLessEqual(EXPECTED_ROLLING, rolling)

    def test_splashing_stays_neutral(self):
        """No catalogue stat for map_mon_splash (#1310): the six Splashing rows keep no mods and never spawn."""
        rows = [r for t in ("magicprefix", "magicsuffix") for r in data_tables.rows(t) if r["Name"] == "Splashing"]
        self.assertEqual(len(rows), 6)
        for r in rows:
            self.assertEqual((r["spawnable"], r["mod1code"]), ("0", ""))

    def test_statroutes_name_real_stats_tcs_and_monsters(self):
        """Each Param-needing mod stat (#1309-#1313) has one route naming a real stat and a real TC or monstats Id."""
        stats = {r["Stat"]: int(r["*ID"]) for r in data_tables.rows("itemstatcost")}
        tcs = {r["Treasure Class"] for r in data_tables.rows("treasureclassex")}
        mons = {r["Id"] for r in data_tables.rows("monstats")}
        routes = data_tables.rows("d2rloader/cartographer/statroutes")
        self.assertEqual(len({r["Stat"] for r in routes}), 13)
        for r in routes:
            self.assertIn(r["Stat"], stats)
            if r["Apply"] == "drop_tc":
                self.assertIn(r["Param"], tcs)
            else:
                self.assertEqual(r["Apply"], "extra_groups")
                self.assertIn(r["Param"], mons)
        self.assertEqual(sorted(stats[r["Stat"]] for r in routes), list(range(637, 650)))


if __name__ == "__main__":
    unittest.main()
