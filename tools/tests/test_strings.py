"""pd2_strings on synthetic fixtures: a hand-built .tbl and a fake D2R string folder (needs d2rAI-toolkit's modkit)."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_strings  # noqa: E402

try:
    _, mkstrings = pd2_strings.modkit()
except ValueError as e:  # CI has no d2rAI-toolkit (private)
    raise unittest.SkipTest(str(e))


def tbl(entries: list[tuple[str, str, int, int]]) -> bytes:
    """A classic .tbl: entries are (key, text, hash value, hash slot); listed in order, table size 8."""
    n, size, head = len(entries), 8, 21
    pool_at = head + 2 * n + 17 * size
    slots, pool = [bytes(17)] * size, b""
    for i, (k, v, hv, slot) in enumerate(entries):
        ko = pool_at + len(pool)
        pool += k.encode() + b"\0"
        vo = pool_at + len(pool)
        pool += v.encode() + b"\0"
        slots[slot] = struct.pack("<BHIIIH", 1, i, hv, ko, vo, len(v) + 1)
    return (struct.pack("<HHIBIII", 0, n, size, 0, pool_at, 1, pool_at + len(pool))
            + struct.pack(f"<{n}H", *(s for *_, s in entries)) + b"".join(slots) + pool)


def record(sid, key, text):
    return {"id": sid, "Key": key, **{lang: text for lang in mkstrings.LANGS}}


class StringsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        files = {
            "strings/skills.json": [record(10, "skillld1", "line one\nline two")],
            "strings/item-runes.json": [record(11, "r08", "Ral Rune"), record(12, "Runeword171", "Hysteria"),
                                        record(50003 + 0x10000, "big", "x")],
            "strings/item-modifiers.json": [record(13, "ModStr", "%d%% Strike")],
            "strings-legacy/skills.json": [record(10, "skillld1", "line one\nline two")],
        }
        for rel, rows in files.items():
            p = self.root / "d2r/data/local/lng" / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("﻿" + json.dumps(rows), "utf-8")
        self.pd2 = {"skillld1": "bottom\ntop", "r08": "ÿc8Ral Runeÿc/","Runeword171": "Dominion",
                    "NewSkill": "b\na", "NewAffix": "of Wit", "ModStr": "Chance of Strike"}

    def tearDown(self):
        self.tmp.cleanup()

    def strings(self):
        return mkstrings.Strings(pd2_strings.DataRoot(self.root / "d2r" / "data"))

    def built(self, rel):
        return {e["Key"]: e for e in json.loads((self.root / "out" / "data/local/lng" / rel).read_text("utf-8-sig"))}

    def test_read_tbl_resolves_repeats_by_hash_probe(self):
        # "k" listed first at slot 3, again at slot 1; both hash to slot 1, so the probe meets the second first
        data = tbl([("k", "far", 9, 3), ("k", "near", 9, 1), ("other", "o", 2, 2)])
        self.assertEqual(pd2_strings.read_tbl(data), {"k": "near", "other": "o"})

    def test_build_overrides_adds_and_renames(self):
        cfg = {"overrides": ["skillld1", "r08"], "renames": {"Runeword171PD2": "Runeword171"},
               "ids": {"Runeword171PD2": [50000, "item-runes.json"], "NewSkill": [50001, "skills.json"]}}
        pd2_strings.build(cfg, self.pd2, self.strings(), self.root / "out")
        skills, legacy, runes = (self.built(r) for r in ("strings/skills.json", "strings-legacy/skills.json",
                                                         "strings/item-runes.json"))
        self.assertEqual(skills["skillld1"]["enUS"], "top\nbottom")  # flipped, enUS only
        self.assertEqual(skills["skillld1"]["deDE"], "line one\nline two")
        self.assertEqual(legacy["skillld1"]["enUS"], "top\nbottom")
        self.assertEqual(skills["NewSkill"]["id"], 50001)
        self.assertEqual({skills["NewSkill"][la] for la in mkstrings.LANGS}, {"a\nb"})
        self.assertEqual(legacy["NewSkill"]["id"], 50001)
        self.assertEqual(runes["r08"]["enUS"], "ÿc8Ral Rune")  # the trailing ÿc/ dropped
        self.assertEqual(runes["Runeword171"]["enUS"], "Hysteria")
        self.assertEqual(runes["Runeword171PD2"]["enUS"], "Dominion")
        self.assertFalse((self.root / "out/data/local/lng/strings-legacy/item-runes.json").exists())

    def test_build_writes_the_mods_own_texts(self):
        cfg = {"overrides": [], "texts": {"OwnLine": "Own: %d per second"}, "ids": {"OwnLine": [50000, "skills.json"]}}
        pd2_strings.build(cfg, self.pd2, self.strings(), self.root / "out")
        self.assertEqual({self.built("strings/skills.json")["OwnLine"][la] for la in mkstrings.LANGS},
                         {"Own: %d per second"})

    def test_build_refuses_unsafe_data(self):
        base = {"overrides": [], "ids": {}}
        for change, msg in [({"ids": {"r08": [50000, "ui.json"]}}, "D2R now has key"),
                            ({"ids": {"NewAffix": [50003, "ui.json"]}}, "used by D2R"),  # low 16 bits of "big"
                            ({"ids": {"NewAffix": [70000, "ui.json"]}}, "above 65535"),
                            ({"ids": {"Gone": [50000, "ui.json"]}}, "PD2 has no key"),
                            ({"overrides": ["ModStr"]}, "placeholders")]:
            with self.subTest(msg), self.assertRaisesRegex(ValueError, msg):
                pd2_strings.build({**base, **change}, self.pd2, self.strings(), self.root / "out")
        pd2_strings.build({"overrides": ["ModStr"], "literal_percent": ["ModStr"], "ids": {}}, self.pd2,
                          self.strings(), self.root / "out")

    def test_assign_numbers_referenced_new_keys_by_column(self):
        excel = self.root / "excel"
        excel.mkdir()
        (excel / "skilldesc.txt").write_bytes(b"skilldesc\tstr long\t*comment\r\nx\tskillld1\tNewAffix\r\n"
                                              b"y\tNewSkill\t\r\n")
        (excel / "magicsuffix.txt").write_bytes(b"Name\tcode\r\nNewAffix\tr08\r\n")
        cfg = {"overrides": [], "renames": {"Runeword171PD2": "Runeword171"}, "ids": {"Old": [50002, "ui.json"]}}
        (excel / "runes.txt").write_bytes(b"Name\r\nRuneword171\r\n")
        added = pd2_strings.assign(cfg, self.pd2, excel, self.strings())
        self.assertEqual(added, ["Runeword171PD2", "NewAffix", "NewSkill"])
        self.assertEqual(cfg["ids"]["Runeword171PD2"], [50004, "item-runes.json"])  # 50003 is "big"'s low 16 bits
        self.assertEqual(cfg["ids"]["NewSkill"], [50006, "skills.json"])  # column str long holds skillld1
        self.assertEqual(cfg["ids"]["NewAffix"][1], "item-runes.json")  # no other Name cell in magicsuffix; runes.Name holds Runeword171
        self.assertEqual(json.loads(pd2_strings.dump(cfg)), cfg)
        self.assertEqual(pd2_strings.assign(cfg, self.pd2, excel, self.strings()), [])


if __name__ == "__main__":
    unittest.main()
