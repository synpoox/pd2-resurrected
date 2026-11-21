"""The 40 PD2 map-item icons stay removed (#1587); every map item's invfile names a shipped sprite."""
import csv
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXCEL = ROOT / "files/data/global/excel"
REMOVED = set("""a2sewersmap arcanemap ashesmap bastionmap caldeummap cryptsmap gravemap hellholemap icemap
invmap_dakenshar invmap_darktemple invmap_demonroad invmap_descent invmap_djinnsdomain invmap_imppalace
invmap_kyovashad invmap_nakrul invmap_outervoid invmap_poisonedwell invmap_skovoshold invmap_torturehalls
invmap_urehcity invmapfallengardens invmaphellcaves invmappvpdesert invmappvpmoor invmapzhar junglemap lavamap
librarymap marketmap mausmap pandemmap sanctuaryofsinmap secheronmap sewermap siegemap spidermap thronemap
westmarchmap""".split())


def rows(name):
    with open(EXCEL / name, newline="", encoding="latin-1") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class NoPd2MapArt(unittest.TestCase):
    def test_removed_names_stay_gone(self):
        files = subprocess.check_output(["git", "ls-files", "files", "merge/art"], cwd=ROOT, text=True).splitlines()
        back = [f for f in files if f.rsplit("/", 1)[-1].split(".")[0].split("-")[0] in REMOVED]
        self.assertEqual(back, [])
        for name in ("misc.txt", "uniqueitems.txt"):
            self.assertFalse({r["invfile"] for r in rows(name)} & REMOVED, name)
        for f in ("items/items.json", "items/uniques.json"):
            text = (ROOT / "files/data/hd" / f).read_text("utf-8")
            self.assertFalse([n for n in REMOVED if f'"pd2/{n}"' in text], f)

    def test_map_invfiles_have_sprites(self):
        maps = [r["invfile"] for r in rows("misc.txt") if r["type"] in ("t1m", "t2m", "t3m")]
        maps += [r["invfile"] for r in rows("uniqueitems.txt") if r["index"].endswith(" Map") and r["invfile"]]
        self.assertTrue(maps)
        for inv in maps:
            self.assertTrue((ROOT / f"files/data/global/items/{inv}.dc6").is_file(), inv)
            self.assertTrue((ROOT / f"files/data/hd/global/ui/items/misc/pd2/{inv}.sprite").is_file(), inv)


if __name__ == "__main__":
    unittest.main()
