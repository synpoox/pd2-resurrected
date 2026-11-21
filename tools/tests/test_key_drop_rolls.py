"""key_drops rolls every PD2 key-drop item (ProjectDiablo.dll 0x103799d0, #1600), each on TC rows that exist."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT = (ROOT / "plugin" / "core-events" / "key_drops.cpp").read_text(encoding="utf-8")
ROLLS = re.findall(r'MakeItemCode\("(\w+)"\), Where::(\w+), \{"([^"]+)", "([^"]+)", "([^"]+)"\}', TEXT)
# item code -> condition, as PD2's function 0x103799d0 rolls them (docs/pd2/uber-bosses-dll.md, section 7)
PD2 = {"rtmo": "Undead", "lucd": "Demon", "lucb": "RedPortal", "lucc": "TerrorZone", "wss": "TerrorZone", "imrn": "Any",
       "lbox": "Any", "lpp": "Any", "rkey": "Any", "rtp": "Any", "rid": "Any", "llmr": "Any", "lsvl": "Any"}


BASE = {"lbox": "Larzuk Puzzlebox", "lpp": "Larzuk Puzzlepiece", "rkey": "SkeletonKey", "rtp": "Horadric TP Book",
        "rid": "Horadric ID Book", "llmr": "Lillith Mirror", "lsvl": "Vial of Lightsong"}


class KeyDropRolls(unittest.TestCase):
    def test_code_to_tc(self):
        for code, _, *names in ROLLS:
            if code in BASE:
                self.assertEqual(names, [f"{BASE[code]} {d}" for d in ("N", "NM", "H")], code)

    def test_rule_table(self):
        self.assertEqual({code: where for code, where, *_ in ROLLS}, PD2)

    def test_tcs_and_items_exist(self):
        excel = ROOT / "files" / "data" / "global" / "excel"
        if not (excel / "treasureclassex.txt").exists():
            self.skipTest("no files/data")
        tcs = {ln.split("\t")[0] for ln in (excel / "treasureclassex.txt").read_text(encoding="latin-1").split("\n")}
        codes = {ln.split("\t")[15] for ln in (excel / "misc.txt").read_text(encoding="latin-1").split("\n") if ln.count("\t") > 15}
        for code, _, *names in ROLLS:
            self.assertIn(code, codes)
            for name in names:
                self.assertIn(name, tcs)


class SpireShadowKeys(unittest.TestCase):
    """The Spire's shadows drop PD2's keys (0x10378170, #1582): monster 993/994/995 drops pk2/pk1/pk3."""

    def test_shadow_to_key(self):
        text = (ROOT / "plugin" / "core-events" / "map_events.cpp").read_text(encoding="utf-8")
        self.assertIn("Keys[3] = {'2', '1', '3'}", text)
        self.assertIn("Shadows[3] = {1010, 1011, 1012}", text)
        self.assertIn("SpireShadowDied(game, monster, event->levelId, seed)", TEXT)

    def test_keys_and_shadows_exist(self):
        excel = ROOT / "files" / "data" / "global" / "excel"
        if not (excel / "monstats.txt").exists():
            self.skipTest("no files/data")
        codes = {ln.split("\t")[15] for ln in (excel / "misc.txt").read_text(encoding="latin-1").split("\n") if ln.count("\t") > 15}
        self.assertTrue({"pk1", "pk2", "pk3"} <= codes)
        ids = [ln.split("\t")[0] for ln in (excel / "monstats.txt").read_text(encoding="latin-1").split("\n")]
        first = ids.index("shadowofhate")
        self.assertEqual(ids[first:first + 3], ["shadowofhate", "shadowofterror", "shadowofdestruction"])


if __name__ == "__main__":
    unittest.main()
