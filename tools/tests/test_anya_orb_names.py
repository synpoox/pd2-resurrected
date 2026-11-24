"""Anya's map orb names retain gold through D2R's native text color marker (#2163)."""
import csv
import json
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files/data"
NAMES = {
    "imma": "Arcane Orb", "imra": "Zakarum Orb", "rera": "Horadrim Orb",
    "scou": "Orb of Destruction", "upma": "Angelic Orb",
    "upmp": "Cartographer's Orb", "fort": "Orb of Fortification",
}
LANGS = "enUS zhTW deDE esES frFR itIT koKR plPL esMX jaJP ptBR ruRU zhCN".split()


class AnyaOrbNames(unittest.TestCase):
    def test_anya_orbs_use_native_gold_names_in_both_string_sets(self):
        with (DATA / "global/excel/misc.txt").open(encoding="utf-8-sig", newline="") as f:
            items = {r["code"]: r for r in csv.DictReader(f, delimiter="\t")}
        for folder in ("strings", "strings-legacy"):
            path = DATA / "local/lng" / folder / "item-names.json"
            strings = {r["Key"]: r for r in json.loads(path.read_text("utf-8-sig"))}
            for code, name in NAMES.items():
                self.assertEqual((items[code]["AnyaMin"], items[code]["AnyaMax"]), ("1", "1"))
                entry = strings[items[code]["namestr"]]
                for lang in LANGS:
                    with self.subTest(folder=folder, code=code, lang=lang):
                        # D2WinTTFont's parser (93847 RVA 0x90c1b0) consumes E07E + selector 4.
                        self.assertEqual(entry[lang], "\ue07e4" + name)


if __name__ == "__main__":
    unittest.main()
