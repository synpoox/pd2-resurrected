"""Map affixes (#1305): every cmap-family affix row names only properties that exist and only the map item types."""
import json
import re
import unittest
from pathlib import Path

import data_tables

MAP_TYPES = {"cmap", "t1m", "t2m", "t3m"}
TABLES = ("magicprefix", "magicsuffix", "rareprefix", "raresuffix")

EXPECTED_ROLLING = {("magicprefix", n) for n in (
    "LargeMap Huge Colossal Strong Mighty Formidable Unhealthy Sickly Diseased".split())} | {
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

    def test_rolling_set_is_the_expected_103(self):
        """spawnable 1, rare 1, frequency > 0: a dead pool (e.g. a lost `rare` flag) fails here."""
        rolling = {
            (t, r["Name"])
            for t in ("magicprefix", "magicsuffix")
            for r, types in map_rows(t)
            if types != {"cmap"}  # the tierless Cartographer placeholders roll too
            if r["spawnable"] == "1" and r["rare"] == "1" and int(r.get("frequency") or 0) > 0
        }
        self.assertEqual(len(rolling), 103)  # 27 from #1305 + 75 from #1309-#1313 + Splashing (#1310)
        self.assertLessEqual(EXPECTED_ROLLING, rolling)

    def test_splashing_rows_use_the_splash_mods(self):
        """#1310: the six Splashing rows roll mon-splash (level 1) or mon-splash2 (level 2), each routed to item_splashonhit at proc_SplashDamage's layer."""
        rows = [r for t in ("magicprefix", "magicsuffix") for r in data_tables.rows(t) if r["Name"] == "Splashing"]
        got = sorted(
            (r["mod1code"], r["mod1min"], r["mod1max"], r["mod2code"], r["mod2min"], r["mod2max"],
             r["mod3code"], r["mod3min"], r["mod3max"], r["itype1"], r["itype2"], r["itype3"], r["spawnable"], r["rare"], r["frequency"])
            for r in rows
        )
        want = sorted([
            ("mon-splash", "100", "100", "map-density", "8", "13", "mag%", "5", "10", "t1m", "t2m", "", "1", "1", "20"),
            ("mon-splash", "100", "100", "map-density", "18", "23", "mag%", "20", "25", "t2m", "t3m", "", "1", "1", "20"),
            ("mon-splash", "100", "100", "map-density", "23", "25", "mag%", "35", "40", "t3m", "", "", "1", "1", "20"),
            ("mon-splash2", "150", "150", "map-density", "16", "26", "mag%", "10", "20", "t1m", "t2m", "", "1", "", "120"),
            ("mon-splash2", "150", "150", "map-density", "36", "46", "mag%", "40", "50", "t2m", "t3m", "", "1", "", "120"),
            ("mon-splash2", "150", "150", "map-density", "46", "50", "mag%", "70", "80", "t3m", "", "", "1", "", "120"),
        ])
        self.assertEqual(got, want)
        skill = next(r for r in data_tables.rows("skills") if r["skill"] == "proc_SplashDamage")
        for level, code in ((1, "mon-splash"), (2, "mon-splash2")):
            prop = next(r for r in data_tables.rows("properties") if r["code"] == code)
            route = next(r for r in data_tables.rows("d2rloader/cartographer/statroutes") if r["Stat"] == prop["stat1"])
            self.assertEqual((route["Target"], route["Apply"]), ("Monster", "item_splashonhit"))
            self.assertEqual(int(route["Param"]), (int(skill["*Id"]) << 6) | level)  # item_splashonhit's layer: skill << 6 | level

    def test_statroutes_name_real_stats_tcs_and_monsters(self):
        """Each Param-needing mod stat (#1309-#1313) has one route naming a real stat and a real TC or monstats Id."""
        stats = {r["Stat"]: int(r["*ID"]) for r in data_tables.rows("itemstatcost")}
        tcs = {r["Treasure Class"] for r in data_tables.rows("treasureclassex")}
        mons = {r["Id"] for r in data_tables.rows("monstats")}
        routes = [r for r in data_tables.rows("d2rloader/cartographer/statroutes") if r["Target"] == "Behaviour"]
        self.assertEqual(len({r["Stat"] for r in routes}), 13)
        for r in routes:
            self.assertIn(r["Stat"], stats)
            if r["Apply"] == "drop_tc":
                self.assertIn(r["Param"], tcs)
            else:
                self.assertEqual(r["Apply"], "extra_groups")
                self.assertIn(r["Param"], mons)
        self.assertEqual(sorted(stats[r["Stat"]] for r in routes), list(range(637, 650)))

    def test_corruption_stats_hidden_from_panel(self):
        """The corrupted and corruptor marks route to None so the map panel's Players have list skips them (#1349)."""
        none = {r["Stat"] for r in data_tables.rows("d2rloader/cartographer/statroutes") if r["Target"] == "None"}
        self.assertEqual(none, {"corrupted", "corruptor"})
        self.assertLessEqual(none, {r["Stat"] for r in data_tables.rows("itemstatcost")})

    def test_spawnable_affix_names_have_text_in_hd_and_legacy(self):
        """A magic affix that can spawn (spawnable 1) needs its Name key with text in both string sets (#1384, #1395), or its name shows empty."""
        lng = Path(__file__).resolve().parents[2] / "files" / "data" / "local" / "lng"
        keys = {d: {e["Key"] for f in (lng / d).glob("*.json") for e in json.loads(f.read_text("utf-8-sig")) if e["enUS"]}
                for d in ("strings", "strings-legacy")}
        missing = sorted({f"{table}.{r['Name']} in {d}" for table in ("magicprefix", "magicsuffix")
                          for r in data_tables.rows(table) if r["Name"] and r["spawnable"] == "1"
                          for d, k in keys.items() if r["Name"] not in k})
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
