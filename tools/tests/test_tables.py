"""pd2_tables (merge/tables.json) on synthetic tables: no PD2 install or D2R data needed."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_tables  # noqa: E402


def table(*rows: str) -> bytes:
    return "".join("\t".join(c.strip() for c in r.split("|")) + "\r\n" for r in rows).encode("latin-1")


class TablesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, side: str, name: str, data: bytes):
        p = self.root / side / "global/excel" / f"{name}.txt"
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)

    def merge(self, cfg: dict) -> dict[str, list[list[str]]]:
        rule = self.root / "tables.json"
        rule.write_text(json.dumps(cfg), "utf-8")
        pd2_tables.rule(rule, self.root / "pd2", self.root / "d2r", self.root / "out")
        out = {}
        for p in (self.root / "out/global/excel").glob("*.txt"):
            out[p.stem] = [line.split("\t") for line in p.read_bytes().decode("latin-1").split("\r\n") if line]
        return out

    def test_rows_merge_by_name_keeping_d2r_only_rows_and_columns(self):
        self.write("pd2", "events", table("event | *desc | x", "hitbymissile | a | 1", "domeleeattack | b | 2",
                                          "dospellcast | c | 3"))
        self.write("d2r", "events", table("event | *description | x | y", "hitbymissile | A | 0 | 7",
                                          "domeleeattack | B | 0 | 8", "death | C | 9 | 9"))
        self.assertEqual(self.merge({"events": {}})["events"], [
            ["event", "*description", "x", "y"],
            ["hitbymissile", "A", "1", "7"],
            ["domeleeattack", "B", "2", "8"],
            ["death", "C", "9", "9"],  # D2R's row keeps its number
            ["dospellcast", "", "3", ""],  # PD2's appends after D2R's rows: events port by name
        ])

    def test_rows_pair_by_position_where_the_first_columns_differ(self):
        self.write("pd2", "shrines", table("Shrine Type | Code | Arg0", "None | 0 | 0", "Recharge | 1 | 5",
                                           "Booster | 2 | 9"))
        self.write("d2r", "shrines", table("Name | Code | Arg0", "None | 0 | 0", "Refilling Shrine | 1 | 4"))
        self.assertEqual(self.merge({"shrines": {"rows": "position"}})["shrines"], [
            ["Name", "Code", "Arg0"], ["None", "0", "0"], ["Refilling Shrine", "1", "5"], ["", "2", "9"]])

    def test_rows_pair_by_key_columns(self):
        self.write("pd2", "hireling", table("Hireling | Id | Level | Gold", "Barbarian | 30 | 3 | 1", "Barbarian | 30 | 25 | 2"))
        self.write("d2r", "hireling", table("Hireling | Id | Level | Gold", "Barbarian | 30 | 25 | 9", "Barbarian | 30 | 28 | 9"))
        self.assertEqual(self.merge({"hireling": {"rows": ["Id", "Level"], "pd2_only_rows": "leave out"}})["hireling"], [
            ["Hireling", "Id", "Level", "Gold"], ["Barbarian", "30", "25", "2"], ["Barbarian", "30", "28", "9"]])

    def test_a_repeated_name_can_take_pd2s_whole_group_in_place(self):
        self.write("pd2", "monequip", table("monster | level | item1", "shadowwarrior | 20 | axe", "shadowwarrior | 0 | club",
                                            "fallen | 0 | sword"))
        self.write("d2r", "monequip", table("monster | level | item1 | x", "shadowwarrior | 0 | dagger | 1",
                                            "zombie | 0 | none | 2", "fallen | 0 | club | 3"))
        self.assertEqual(self.merge({"monequip": {"repeats": "replace group"}})["monequip"], [
            ["monster", "level", "item1", "x"], ["shadowwarrior", "20", "axe", "1"], ["shadowwarrior", "0", "club", ""],
            ["zombie", "0", "none", "2"], ["fallen", "0", "sword", "3"]])

    def test_named_columns_keep_d2rs_values(self):
        self.write("pd2", "objects", table("Name | Token | Parm0", "Cain Portal | tP | 5", "Trophy | qq | 1"))
        self.write("d2r", "objects", table("Name | Token | Parm0", "Cain Portal | tp | 4"))
        self.assertEqual(self.merge({"objects": {"d2r_columns": ["token"]}})["objects"], [
            ["Name", "Token", "Parm0"], ["Cain Portal", "tp", "5"], ["Trophy", "", "1"]])

    def test_class_skill_line_keeps_d2rs_string_with_its_number(self):
        """PD2's ModStre8a_PD2 is "to Druid Skill Levels": D2R's descfunc 13 prints no number for it (#255)."""
        cfg = json.loads((Path(pd2_tables.__file__).parent.parent / "merge/tables.json").read_text("utf-8"))
        self.write("pd2", "charstats", table("class | StrAllSkills | hpadd", "Druid | ModStre8a_PD2 | 9"))
        self.write("d2r", "charstats", table("class | StrAllSkills | hpadd", "Druid | ModStre8a | 1"))
        self.assertEqual(self.merge({"charstats": cfg["charstats"]})["charstats"], [
            ["class", "StrAllSkills", "hpadd"], ["Druid", "ModStre8a", "9"]])

    def test_named_cells_take_the_rules_value(self):
        self.write("pd2", "monstats", table("Id | NameStr | Level", "roguehire | Dummy | 5", "fallen | Fallen | 2",
                                            "act4hire | dummy | 9"))
        self.write("d2r", "monstats", table("Id | NameStr | Level", "roguehire | Rogue | 1", "fallen | Fallen1 | 1"))
        self.assertEqual(self.merge({"monstats": {"cells": {"RogueHire": {"namestr": "Rogue"},
                                                            "act4hire": {"NameStr": "Ascendant"}}}})["monstats"], [
            ["Id", "NameStr", "Level"], ["roguehire", "Rogue", "5"], ["fallen", "Fallen", "2"],
            ["act4hire", "Ascendant", "9"]])  # a PD2-only row too; other rows keep PD2's NameStr
        with self.assertRaisesRegex(ValueError, "no row nosuch"):
            self.merge({"monstats": {"cells": {"nosuch": {"NameStr": "x"}}}})
        with self.assertRaisesRegex(ValueError, "no column NoCol"):
            self.merge({"monstats": {"cells": {"fallen": {"NoCol": "x"}}}})

    def test_pd2_only_sounds_take_d2rs_sound_of_the_same_file_or_none(self):
        """D2R's sounds table ships as it is, so a PD2-only sound past it crashed the client (Leoric's taunt, #267)."""
        self.write("pd2", "sounds", table("Sound | Index | FileName", "fallen_rakanishu | 1 | monster\\fallen\\warcry1.wav",
                                          "leoric_taunt | 2 | pd2\\leoric\\leoric_remaster.wav"))
        self.write("d2r", "sounds", table("Sound | *Index | FileName", "fallen_warcry_1 | 1 | monster\\fallen\\warcry1.flac"))
        self.write("pd2", "monsounds", table("Id | Neutral | Taunt", "treasurefallen | fallen_rakanishu | leoric_taunt"))
        self.write("d2r", "monsounds", table("Id | Neutral | Taunt", "fallen | fallen_warcry_1 |"))
        self.assertEqual(self.merge({"monsounds": {}})["monsounds"], [
            ["Id", "Neutral", "Taunt"], ["fallen", "fallen_warcry_1", ""], ["treasurefallen", "fallen_warcry_1", ""]])
        self.write("pd2", "monsounds", table("Id | Neutral | Taunt", "leoric | nosuch |"))
        with self.assertRaisesRegex(ValueError, "leoric Neutral nosuch"):
            self.merge({"monsounds": {}})

    def test_moved_ids_append_with_pd2s_hire_description_and_merged_monster_numbers(self):
        self.write("pd2", "hiredesc", table("Hireling Description | Code | *EOL", "None |  | 0", "Fire Arrow | farw | 0",
                                            "Ascendant Dark | ascd | 0"))
        self.write("pd2", "monstats", table("Id | hcIdx", "kashya | 0", "Expansion | ", "tyrael2 | 1", "act4hire | 2"))
        self.write("d2r", "monstats", table("Id | *hcIdx", "kashya | 0", "Expansion | ", "tyrael2 | 1", "warlockhire | ",
                                            "Expansion | "))
        self.write("pd2", "hireling", table("Hireling | Id | Level | Class | Seller | HireDesc", "Rogue | 0 | 3 | 0 | 0 | farw",
                                            "Rogue | 0 | 9 | 0 | 0 | farw", "Ascendant | 30 | 3 | 2 | 1 | ascd",
                                            "Ascendant | 31 | 3 | 2 | 1 | farw"))
        ui = self.root / "d2r/local/lng/strings/ui.json"
        ui.parent.mkdir(parents=True)
        ui.write_text(json.dumps([{"id": 3371, "Key": "strhirespecial1", "enUS": "Fire Arrow"}]), "utf-8")
        self.write("d2r", "hireling", table("Hireling | Id | Level | Class | Seller | HireDesc", "Rogue | 0 | 3 | 0 | 0 | strhire1",
                                            "Desert | 30 | 43 | 1 | 1 | skillname99"))
        out = self.merge({"monstats": {}, "hireling": {"rows": ["Id", "Level"], "pd2_only_rows": "leave out",
                                                       "d2r_columns": ["HireDesc"], "move": {"Id": {"30": "39", "31": "40"}}}})
        self.assertEqual(out["hireling"], [
            ["Hireling", "Id", "Level", "Class", "Seller", "HireDesc"], ["Rogue", "0", "3", "0", "0", "strhire1"],
            ["Desert", "30", "43", "1", "1", "skillname99"],  # D2R's own id 30 is kept, PD2's Rogue 0/9 left out
            ["Ascendant", "39", "3", "3", "1", "hiredesc_ascd"],  # act4hire follows D2R's warlockhire: monster 3
            ["Ascendant", "40", "3", "3", "1", "strhirespecial1"]])  # D2R's own string for the same text

    def test_pd2_only_rows_can_be_left_out(self):
        self.write("pd2", "levels", table("Name | Id", "Act 1 | 1", "Mesa | 139"))
        self.write("d2r", "levels", table("Name | Id", "Act 1 | 1"))
        self.assertEqual(self.merge({"levels": {"pd2_only_rows": "leave out"}})["levels"], [["Name", "Id"], ["Act 1", "1"]])

    def test_only_named_rows_merge(self):
        self.write("pd2", "inventory", table("class | gridX", "Amazon | 10", "Transmogrify Box2 | 4", "Trophy | 10"))
        self.write("d2r", "inventory", table("class | gridX", "Amazon | 9", "Transmogrify Box2 | 3", "Warlock | 10"))
        self.assertEqual(self.merge({"inventory": {"only_rows": ["transmogrify box2"]}})["inventory"], [
            ["class", "gridX"], ["Amazon", "9"], ["Transmogrify Box2", "4"], ["Warlock", "10"]])
        with self.assertRaisesRegex(ValueError, "no row nosuch"):
            self.merge({"inventory": {"only_rows": ["nosuch"]}})


if __name__ == "__main__":
    unittest.main()
