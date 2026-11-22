"""plugin/common/invader_dress.h matches the tables (#1708): its items are the Invader TCs' bases with their own art tokens
and body levels, and its class lists are the invader rows' monstats2 lists the part bytes index."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
EXCEL = ROOT / "files/data/global/excel"
HEADER = (ROOT / "plugin/common/invader_dress.h").read_text("utf-8")
ART = HEADER  # Nearest lives in invader_dress.h
TOKENS = ["am", "ai", "ba", "dz", "ne", "pa", "so"]  # the player art folders, in CLASSES order
MODES = ["dt", "nu", "wl", "gh", "a1", "a2", "bl", "sc", "s1", "s2", "s3", "s4", "dd", "kb", "sq", "rn"]  # monster modes
PARTS = ["hd", "tr", "lg", "ra", "la", "rh", "lh", "sh", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8"]
# the TCs each class rolls for its look parts (invaders.cpp ClassDrops, every weapon set)
CLASS_TCS = [
    ["InvaderCirclets", "InvaderAmazon2hSpear", "InvaderAmazonBow", "InvaderAmazonCrossbow", "InvaderAmazon1hJavelin",
     "InvaderGenericShield"],
    ["InvaderGenericHelm", "InvaderAssassinWeapon"],
    ["InvaderBarbarianHelm", "InvaderBarbarian2h", "InvaderBarbarianThrow", "Invader2hSword"],
    ["InvaderDruidHelm", "InvaderDruidWeapon", "InvaderGenericShield"],
    ["InvaderGenericHelm", "InvaderNecroWeapon", "InvaderNecroShield"],
    ["InvaderGenericHelm", "InvaderPaladinWeapon", "InvaderPaladinShield"],
    ["InvaderCirclets", "InvaderSorc2hWeapon", "InvaderSorc1hWeapon", "InvaderGenericShield"],
]
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


def tc_codes(names=None):
    tcs = {r["Treasure Class"]: r for r in table("treasureclassex.txt")}

    def walk(tc):
        for i in range(1, 11):
            it = tcs[tc].get(f"Item{i}", "")
            if it:
                yield from walk(it) if it in tcs else [it]
    return {c for tc in (names or [t for t in tcs if t.startswith("Invader")]) for c in walk(tc)}


def cof_layers(d):
    """part -> the layer's weapon class, from a COF (28-byte header, 9-byte layers)."""
    return {PARTS[d[28 + 9 * i]]: d[33 + 9 * i:37 + 9 * i].split(b"\0")[0].decode().lower() for i in range(d[0])}


def d2r():
    try:
        sys.path.insert(0, str(ROOT / "tools"))
        sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT)))
        from modkit import base
        b = base.Base()
        b.bytes("data/global/chars/am/cof/amnubow.cof")
    except (ValueError, ImportError, OSError) as e:  # no toolkit, no CascLib, no game: nothing to compare with
        raise unittest.SkipTest(f"no D2R files: {e}")
    return b


def header_items():
    out = {}
    for m in re.finditer(r'\{"(\w+)", Slot::(\w+), "(\w+)"(?:, \{([\d, ]+)\})?\}', HEADER):
        out[m[1]] = (m[2], m[3], [int(x) for x in m[4].split(",")] if m[4] else None)
    return out


def header_classes():
    block = HEADER[HEADER.index("Classes[7] = {"):]
    return re.findall(r'\{"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)", (RH|LH)\}', block)[:7]


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

    def test_every_rolled_base_has_its_own_token_in_the_class_lists(self):
        """An invader wears every base it rolls: no random look stands in for a rolled weapon, shield or head."""
        have, got = header_items(), header_classes()
        for mid, tcs, lists in zip(CLASSES, CLASS_TCS, got):
            tokens = set(",".join(lists[:5]).split(","))
            for code in tc_codes(tcs):
                with self.subTest(f"{mid} {code}"):
                    self.assertIn(have[code][1], tokens)

    def test_hands_and_shields_can_be_empty(self):
        """A hand or shield the roll may leave empty has nil, so it never keeps the spawn's random token."""
        got = header_classes()
        for i in (0, 2, 6):  # the Amazon (bow or spear hand, shield), the Barbarian (one two-hander), the Sorceress (shield)
            with self.subTest(CLASSES[i]):
                self.assertIn("nil", ",".join(got[i][1:4]).split(","))

    def test_every_listed_token_has_legacy_art_or_a_nearest(self):
        """Each list token (through invader_dress.h's Nearest) has a DCC in every COF of the class's BaseW that has
        that layer: a missing legacy DCC crashes classic mode."""
        b = d2r()
        got = header_classes()
        near = {(c.lower(), f): to for c, f, to in re.findall(r'\{"(\w\w)", "(\w+)", "(\w+)"\}', ART)}
        self.assertTrue(near)
        rows = {r["Id"]: r for r in table("monstats2.txt") if r["Id"] in CLASSES}
        for mid, cls, lists in zip(CLASSES, TOKENS, got):
            wc = rows[mid]["BaseW"].lower()
            for mode in MODES:
                cof = f"data/global/chars/{cls}/cof/{cls}{mode}{wc}.cof"
                if not b.exists(cof):
                    continue
                layers = cof_layers(b.bytes(cof))
                for part, lst in zip(("hd", "rh", "lh", "sh", "s3"), lists[:5]):
                    if (cls, part, mode) == ("ne", "s3", "bl"):
                        continue  # D2R has no block art for any head; PD2's own S3v ne2 had the same gap before #1708
                    for tok in (t for t in lst.split(",") if t and t != "nil" and part in layers):
                        tok = near.get((cls, tok), tok)
                        p = f"data/global/chars/{cls}/{part}/{cls}{part}{tok}{mode}{layers[part]}.dcc"
                        with self.subTest(p):
                            self.assertTrue(b.exists(p))


if __name__ == "__main__":
    unittest.main()
