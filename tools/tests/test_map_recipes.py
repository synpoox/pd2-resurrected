"""#1306: every new map recipe uses valid codes and types, and none outputs a magic map."""
import re
import unittest

import data_tables

MAPS = ("t1m", "t2m", "t3m")


class MapRecipeTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)
        rows = data_tables.rows("cubemain")
        i = next(k for k, r in enumerate(rows) if r["description"].startswith("native map + iwss"))
        self.rows = [r for r in rows[i + 2:] if r["output"] and r["input 1"]]
        misc = {r["code"] for r in data_tables.rows("misc")}
        types = {r["Code"] for r in data_tables.rows("itemtypes")}
        self.known = misc | types
        self.props = {r["code"] for r in data_tables.rows("properties")}

    def test_there_are_rows(self):
        assert len(self.rows) >= 50

    def test_inputs_are_valid_codes(self):
        for r in self.rows:
            for k in range(1, 8):
                c = r.get(f"input {k}", "").strip('"')
                if c:
                    code = c.split(",")[0]
                    assert code in self.known or code == "cmap", (r["description"], code)

    def test_outputs_are_valid_and_never_magic(self):
        for r in self.rows:
            parts = r["output"].split(",")
            assert parts[0] in self.known | {"useitem", "usetype"}, r["description"]
            assert "mag" not in parts[1:], r["description"]
            if parts[0] == "usetype":
                assert parts[1] in ("nor", "rar"), r["description"]
            for k in range(1, 6):
                m = r.get(f"mod {k}", "")
                assert not m or m in self.props, (r["description"], m)

    def test_no_map_input_asks_for_magic(self):
        for r in self.rows:
            for k in range(1, 8):
                c = r.get(f"input {k}", "").strip('"')
                if c.split(",")[0] in MAPS + ("cmap",):
                    assert "mag" not in c.split(",")[1:], r["description"]

    def test_new_orbs_exist(self):
        codes = {r["code"] for r in data_tables.rows("misc")}
        assert {"upmp", "fort"} <= codes


class MapRecipeShapeTests(MapRecipeTests):
    def desc(self, part):
        return [r for r in self.rows if part in r["description"]]

    def test_op18_stat_ids(self):
        for r in self.desc("heroic map"):
            assert (r["op"], r["param"], r["value"]) == ("18", "511", "0"), r["description"]  # heroic
        for r in self.desc("corrupt roll") + self.desc(" Orb)"):
            assert (r["op"], r["param"], r["value"]) == ("18", "414", "0"), r["description"]  # corruptor

    def test_ear_recipes_gate_on_treacherous_and_keep_the_map(self):
        ears = self.desc("-> treacherous map")
        assert len(ears) == 7
        for r in ears:
            assert (r["op"], r["param"], r["value"]) == ("18", "402", "0"), r["description"]  # treacherous
            assert r["input 1"] == '"cmap,rar"' and r["output"] == "usetype,rar,mod", r["description"]
            assert re.fullmatch(r'"ive[a-z],qty=1"', r["input 2"]), r["description"]
            assert r["mod 3"] == "treacherous" and r["mod 3 min"] == r["mod 3 max"], r["description"]
        # one treacherous value per class, 1-7
        assert sorted(r["mod 3 min"] for r in ears) == list("1234567")
        assert len([r for r in self.rows if "map + ear" in r["description"] and r["op"] == "30"]) == 7

    def test_each_refusal_comes_directly_before_its_real_row(self):
        for i, r in enumerate(self.rows):
            if r["op"] == "30":
                nxt = self.rows[i + 1]
                assert nxt["op"] != "30" and nxt["input 2"] == r["input 2"], r["description"]

    def test_corrupt_thresholds_rise_and_cover_the_roll(self):
        for t in ("t1", "t2", "t3"):
            roll = next(r for r in self.rows if r["description"] == f"{t} map corrupt roll")
            top = int(roll["mod 1 max"])
            thr = [int(r["value"]) for r in self.rows if r["description"].startswith(f"{t} map corrupt:")]
            assert thr == sorted(set(thr)) and thr[-1] > top, t
            done = {r["mod 2 max"] for r in self.rows if r["description"].startswith(f"{t} map corrupt:")}
            assert len(done) == 1 and top + int(done.pop()) <= 4095  # corruptor has 12 save bits

    def test_numinputs_is_the_summed_qty(self):
        for r in self.rows:
            ins = [r[f"input {k}"].strip('"') for k in range(1, 8) if r.get(f"input {k}")]
            want = sum(int(m.group(1)) if (m := re.search(r"qty=(\d+)", i)) else 1 for i in ins)
            assert int(r["numinputs"]) == want, r["description"]

    def test_normal_map_inputs_use_qty_so_a_stack_matches(self):
        for r in self.rows:
            for k in range(1, 8):
                i = r.get(f"input {k}", "").strip('"')
                if re.match(r"t\dm,nor", i) and r["op"] != "30":
                    assert "qty=" in i, r["description"]

    def test_anya_sells_upmp_and_fort_like_pd2(self):
        misc = {r["code"]: r for r in data_tables.rows("misc")}
        for code, price in (("upmp", "200000"), ("fort", "15000")):
            r = misc[code]
            assert (r["AnyaMin"], r["AnyaMax"], r["cost"], r["spawnable"]) == ("1", "1", price, "1"), code

    def test_kept_maps_use_the_mod_qualifier(self):
        for part in ("heroic map", "corrupt", "fortified"):
            for r in self.desc(part):
                if r["op"] != "30":
                    assert r["output"].endswith(",mod"), r["description"]
