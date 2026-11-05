"""pd2_levels on synthetic tables and a synthetic DS1 (no PD2 or D2R data)."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_ids as I  # noqa: E402
import pd2_levels as L  # noqa: E402
from tests.test_ids import table  # noqa: E402


def ds1(objects: list[tuple[int, int]], act: int = 2) -> bytes:
    """A version 18 DS1, 2x2 cells, one wall and one floor layer, then `objects` (type, id) and a trailer."""
    head = struct.pack("<6i", 18, 1, 1, act, 0, 1) + b"act1/x.dt1\0" + struct.pack("<2i", 1, 1)
    layers = b"\1\0\0\0" * 4 * (2 * 1 + 1 + 1)
    objs = struct.pack("<i", len(objects)) + b"".join(struct.pack("<5i", t, i, 3, 4, 0) for t, i in objects)
    return head + layers + objs + b"GROUPS"


class Ds1Tests(unittest.TestCase):
    def test_object_ids_rewritten_and_left_out(self):
        data = ds1([(1, 5), (2, -10), (2, 160), (3, 7)])
        self.assertEqual(L.object_list(data)[0], 2)
        seen = []

        def fix(act, kind, oid):
            seen.append((act, kind, oid))
            return None if oid == -10 else oid + 100
        out = L.rewrite_ds1(data, fix)
        self.assertEqual(seen, [(2, 1, 5), (2, 2, -10), (2, 2, 160), (2, 3, 7)])
        self.assertEqual(out, ds1([(1, 105), (2, 260), (3, 107)]))
        self.assertTrue(out.endswith(b"GROUPS"))


class LevelTypeTests(unittest.TestCase):
    def test_files_fill_free_slots_move_on_a_clash_and_new_types_append(self):
        d2r = table("Name|Id|File 1|File 2|File 3\nTown|0|a.dt1|InvisWal.dt1|0\nExpansion||||\n")
        pd2 = table("Name|Id|File 1|File 2|File 3|Beta\nTown|0|A.dt1|dune.dt1|0|\nExpansion|||||\n"
                    "Map|1|PD2assets/x.dt1|Act4\\Fort\\ForeGate.dt1|PD2assets/guild.dt1|1\n")
        # the PD2-only presets of type 0 select slots 1 and 2: slot 1 is D2R's file, slot 2 clashes and moves to 3;
        # no in-scope preset selects type 1's slot 3, so it is blanked (D2R loads every file a type names)
        L.SLOTS, slots = 3, L.SLOTS
        try:
            types, moved = L.merge_types(pd2, d2r, {0: 0b011, 1: 0b011}, {"act4/fort/foregate.dt1"})
        finally:
            L.SLOTS = slots
        self.assertEqual(types.rows, [["Town", "0", "a.dt1", "InvisWal.dt1", "dune.dt1"], ["Expansion", "", "", "", ""],
                                      ["Map", "1", "PD2assets/x.dt1", "pd2assets/v/act4/fort/foregate.dt1", "0"]])
        self.assertEqual(moved, {0: {1: 2}})
        self.assertEqual(L.move_bits(0b011, moved[0]), 0b101)

    def test_a_path_past_the_games_41_characters_fails(self):
        d2r = table("Name|Id|File 1\nTown|0|a.dt1\n")
        pd2 = table("Name|Id|File 1\nTown|0|a.dt1\nMap|1|PD2assets/" + "x" * 28 + ".dt1\n")
        L.SLOTS, slots = 1, L.SLOTS
        try:
            with self.assertRaisesRegex(ValueError, "41"):
                L.merge_types(pd2, d2r, {1: 1}, set())
        finally:
            L.SLOTS = slots


class MonPresetTests(unittest.TestCase):
    def test_seed_check_and_merge(self):
        vanilla = dict(L.MONPRESET_VANILLA)
        L.MONPRESET_VANILLA.update({"1": 1, "5": 1})
        try:
            pd2 = {"1": ["gheed", "New1"], "5": ["larzuk", "Boss", "Boss"]}
            d2r = {"1": ["gheed"], "5": ["larzuk", "place_nothing"]}
            lines = L.seed_monpreset(pd2, d2r, [])
            self.assertEqual(lines, [("1/gheed", 0, "gheed"), ("1/New1", 1, "New1"), ("5/larzuk", 0, "larzuk"),
                                     ("5/Boss", 2, "Boss"), ("5/Boss#2", 3, "Boss")])
            L.check_monpreset(lines, pd2, d2r)
            self.assertEqual(L.seed_monpreset(pd2, d2r, lines), [])
            with self.assertRaisesRegex(I.IdError, "not a PD2 row"):  # monpreset rows can't be left out
                L.check_monpreset(lines[:-1] + [("5/Boss#2", I.LEFT_OUT, "Boss")], pd2, d2r)
            with self.assertRaisesRegex(I.IdError, "no index"):
                L.check_monpreset(lines[:-1], pd2, d2r)
            merged = L.merge_monpreset(table("Act|Place\n1|gheed\n5|larzuk\n5|place_nothing\n"), lines)
            self.assertEqual(merged.rows, [["1", "gheed"], ["1", "New1"], ["5", "larzuk"], ["5", "place_nothing"],
                                           ["5", "Boss"], ["5", "Boss"]])
        finally:
            L.MONPRESET_VANILLA.clear()
            L.MONPRESET_VANILLA.update(vanilla)


class AutomapTests(unittest.TestCase):
    def test_new_types_only_with_tile_numbers_any_as_255_and_cels_packed(self):
        pd2 = table("LevelName|TileName|Style|StartSequence|EndSequence|Type1|Cel1|Type2|Cel2|Type2|Cel3|Type4|Cel4\n"
                    "5 Lava|fl|0|1|4|x|1600|||||||\n"
                    "36|wtlr|2|-1|-1|x|1212|x|1575|x||x|1213\n"
                    "50|fi|11|42|-1||-1||-1||-1||-1\n"
                    "Expansion||||||||||||\n")
        self.assertEqual(L.automap_rows(pd2, 1576), [
            ["LevelType", "TileType", "Style", "StartSequence", "EndSequence", "CelCount", "Cel1", "Cel2", "Cel3", "Cel4"],
            ["36", "3", "2", "255", "255", "3", "1212", "1575", "1213", "0"],
            ["50", "19", "11", "42", "255", "0", "0", "0", "0", "0"],
        ])

    def test_a_cel_past_the_tiles_fails(self):
        # stored in a character's map memory, it would hang the next save
        pd2 = table("LevelName|TileName|Style|StartSequence|EndSequence|Cel1|Cel2|Cel3|Cel4\n36|wl|0|0|0|1|1576||\n")
        with self.assertRaisesRegex(ValueError, "1576 is past the 1576"):
            L.automap_rows(pd2, 1576)

    D2R = "LevelName|TileName|Style|StartSequence|EndSequence|*Type1|Cel1|*Type2|Cel2|*Type3|Cel3|*Type4|Cel4\n" \
          "1 Town|fl|0|1|46|A|0||-1||-1||-1\nExpansion||||||||||||\n5 Siege|fl|0|0|0|B|1300||-1||-1||-1\n"
    PD2 = "LevelName|TileName|Style|StartSequence|EndSequence|Type1|Cel1|Type2|Cel2|Type2|Cel3|Type4|Cel4\n"

    def test_vanilla_types_take_pd2s_rows_under_d2rs_header(self):
        pd2 = table(self.PD2 + "1 Town|fl|0|1|46|A|0||-1||-1||-1\n1 Town|wl|0|2|2|New|1471||-1||-1||-1\n"
                    "Expansion||||||||||||\n5 Siege|fl|0|0|0|B2|1361||-1||-1||-1\n36|wl|0|0|0|x|1500||-1||-1||-1\n")
        merged = L.vanilla_automap(pd2, table(self.D2R), 1576)
        self.assertEqual(merged.header[5], "*Type1")
        self.assertEqual([r[0] + "/" + r[6] for r in merged.rows],
                         ["1 Town/0", "1 Town/1471", "Expansion/", "5 Siege/1361"])

    def test_vanilla_cel_past_the_tiles_fails(self):
        pd2 = table(self.PD2 + "1 Town|fl|0|1|46|A|1576||-1||-1||-1\n")
        with self.assertRaisesRegex(ValueError, "1576 is past the 1576"):
            L.vanilla_automap(pd2, table(self.D2R), 1576)

    def test_level_name_d2r_lacks_fails(self):
        # D2R's loader asserts on a LevelName outside its 36
        pd2 = table(self.PD2 + "1 Townn|fl|0|1|46|A|0||-1||-1||-1\n")
        with self.assertRaisesRegex(ValueError, "1 Townn"):
            L.vanilla_automap(pd2, table(self.D2R), 1576)

    def test_split_level_type_fails(self):
        # D2R's loader keeps one row range per level type: a later run of it would hide the first
        pd2 = table(self.PD2 + "1 Town|fl|0|1|46|A|0||-1||-1||-1\n5 Siege|fl|0|0|0|B|1300||-1||-1||-1\n"
                    "1 Town|wl|0|2|2|C|1||-1||-1||-1\n")
        with self.assertRaisesRegex(ValueError, "1 Town"):
            L.vanilla_automap(pd2, table(self.D2R), 1576)

    def test_unknown_tile_name_fails(self):
        pd2 = table("LevelName|TileName|Style|StartSequence|EndSequence|Cel1|Cel2|Cel3|Cel4\n36|xx|0|0|0|1|||\n")
        with self.assertRaisesRegex(ValueError, "xx"):
            L.automap_rows(pd2, 1576)


if __name__ == "__main__":
    unittest.main()
