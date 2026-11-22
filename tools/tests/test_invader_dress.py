"""plugin/common/invader_dress.h matches the tables (#1708): its items are the Invader TCs' bases with their own art tokens
and body levels, and its class lists are the invader rows' monstats2 lists the part bytes index."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
EXCEL = ROOT / "files/data/global/excel"
HEADER = (ROOT / "plugin/common/invader_dress.h").read_text("utf-8")
CLASSES = ["InvaderAmazon", "InvaderAssassin", "InvaderBarbarian", "InvaderDruid", "InvaderNecromancer", "InvaderPaladin",
           "InvaderSorceress"]
SHOWN = {"helm": "Head", "circ": "Head", "phlm": "Head", "pelt": "Head", "tors": "Body", "shie": "Shield", "ashd": "Shield",
         "head": "Skull"}
NOT_SHOWN = {"glov", "boot", "belt", "ring", "amul", "bowq", "xboq"}


def table(name):
    rows = (EXCEL / name).read_text("latin-1").split("\n")
    head = rows[0].rstrip("\r").split("\t")
    return [dict(zip(head, r.rstrip("\r").split("\t"))) for r in rows[1:] if r.strip()]


def items():
    return {r["code"]: r for n in ("weapons.txt", "armor.txt", "misc.txt") for r in table(n) if r.get("code")}


def tc_codes():
    tcs = {r["Treasure Class"]: r for r in table("treasureclassex.txt")}

    def walk(tc):
        for i in range(1, 11):
            it = tcs[tc].get(f"Item{i}", "")
            if it:
                yield from walk(it) if it in tcs else [it]
    return {c for tc in tcs if tc.startswith("Invader") for c in walk(tc)}


def header_items():
    out = {}
    for m in re.finditer(r'\{"(\w+)", Slot::(\w+), "(\w+)"(?:, \{([\d, ]+)\})?\}', HEADER):
        out[m[1]] = (m[2], m[3], [int(x) for x in m[4].split(",")] if m[4] else None)
    return out


def header_classes():
    block = HEADER[HEADER.index("Classes[7] = {"):]
    return re.findall(r'\{"([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)", (RH|LH)\}', block)[:7]


class InvaderDressTests(unittest.TestCase):
    def test_every_shown_base_is_listed_with_its_own_token(self):
        have, base = header_items(), items()
        for code in sorted(tc_codes()):
            r = base[code]
            with self.subTest(code):
                if r["type"] in NOT_SHOWN:
                    self.assertNotIn(code, have)
                    continue
                slot, tok, body = have[code]
                self.assertEqual(slot, SHOWN.get(r["type"], "Weapon"))
                self.assertEqual(tok, r["alternategfx"])
                if slot == "Body":
                    self.assertEqual(body, [int(r[k] or 0) for k in ("Torso", "Legs", "rArm", "lArm", "rSPad", "lSPad")])
        self.assertLessEqual(set(have), tc_codes())

    def test_class_lists_are_the_monstats2_lists(self):
        rows = {r["Id"]: r for r in table("monstats2.txt") if r["Id"] in CLASSES}
        got = header_classes()
        self.assertEqual(len(got), 7)
        for mid, (hd, rh, lh, sh, s3, _hand) in zip(CLASSES, got):
            with self.subTest(mid):
                r = rows[mid]
                self.assertEqual([hd, rh, lh, sh, s3], [r["HDv"], r["RHv"], r["LHv"], r["SHv"], r["S3v"]])
                for col in ("TRv", "LGv", "Rav", "Lav", "S1v", "S2v"):
                    self.assertEqual(r[col], "lit,med,hvy", col)
                for flag, lst in (("HD", hd), ("RH", rh), ("LH", lh), ("SH", sh), ("S3", s3)):
                    self.assertEqual(r[flag] == "1", bool(lst), flag)

    def test_nearest_targets_are_in_the_class_list(self):
        got = header_classes()
        for cls, frm, to in re.findall(r'\{(\d), "(\w+)", "(\w+)"\}', HEADER):
            hd, rh, lh, *_ = got[int(cls)]
            with self.subTest(frm):
                self.assertIn(to, (rh + "," + lh).split(","))
                self.assertNotIn(frm, (rh + "," + lh).split(","))


if __name__ == "__main__":
    unittest.main()
