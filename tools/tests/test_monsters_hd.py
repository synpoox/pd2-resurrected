"""pd2_monsters_hd on synthetic tables (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_monsters_hd as H  # noqa: E402
from pd2_ids import Table  # noqa: E402


def table(text: str) -> Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "\t").encode("latin-1"))
        return Table(p)


D2R = "Id|BaseId|Code\r\nskeleton1|skeleton1|SK\r\nsk_archer1|sk_archer1|SK\r\nsk_archer2|sk_archer1|SK\r\n" \
      "baalclone|baalclone|BA\r\nfamiliar|familiar|FI\r\nExpansion||\r\n"
MODELS = {"skeleton1": "skeleton1", "sk_archer1": "sk_archer1", "sk_archer2": "sk_archer1", "baalclone": "baal"}
# D2R's rows, then PD2's: a clone (mixed-case Id, lower-case Code) drawn as its BaseId, not as the Code's most common
# model; a new base drawn as its Code's; a BaseId with other art drawn as its own Code's; PD2-only codes; an override
MOD = D2R + "skeletonIce|skeleton1|sk\r\nnecroarcher|necroarcher|SK\r\nshadow|skeleton1|BA\r\n" \
            "ZharTheMad|summoner|$4\r\nIskatu|iskatu|!8\r\nbonemage|bonemage|SK\r\n"
LIKES = {"zharthemad": "baalclone", "iskatu": "skeleton1", "bonemage": "skeleton1"}
RULE = "# monster\tlike\nZharTheMad\tbaalclone\niskatu\tskeleton1\nbonemage\tskeleton1\n"


class EntriesTests(unittest.TestCase):
    def test_baseid_code_and_lines(self):
        self.assertEqual(H.entries(table(MOD), table(D2R), MODELS, LIKES), {
            "skeletonIce": "skeleton1", "necroarcher": "sk_archer1", "shadow": "baal", "ZharTheMad": "baal",
            "Iskatu": "skeleton1", "bonemage": "skeleton1"})  # D2R's lookup is case-sensitive: PD2's own spelling

    def test_refusals(self):
        cases = [
            ({"zharthemad": "baalclone", "bonemage": "skeleton1"}, r"no HD model for 1 new monsters.*Iskatu \(!8\)"),
            (LIKES | {"skeletonice": "skeleton1"}, "'skeletonIce' gets skeleton1 anyway"),
            (LIKES | {"necroarcher": "sk_archer2"}, "'necroarcher' gets sk_archer1 anyway"),
            (LIKES | {"skeleton1": "baalclone"}, "not new: skeleton1"),
            (LIKES | {"iskatu": "nope"}, "'nope' has no monsters.json entry"),
        ]
        for likes, msg in cases:
            with self.subTest(msg), self.assertRaisesRegex(H.MonsterHdError, msg):
                H.entries(table(MOD), table(D2R), MODELS, likes)
        with self.assertRaisesRegex(H.MonsterHdError, "already has 'skeleton2'"):
            H.entries(table(D2R + "skeleton2|skeleton1|SK\r\n"), table(D2R), MODELS | {"skeleton2": "skeleton1"}, {})

    def test_tie_needs_a_line(self):
        d2r = D2R + "colossal1|colossal1|0D\r\nancientbarb1|ancientbarb1|0D\r\n"
        models = MODELS | {"colossal1": "colossal1", "ancientbarb1": "ancientbarb1"}
        mod = d2r + "ancientbarbboss1|ancientbarbboss1|0D\r\n"
        with self.assertRaisesRegex(H.MonsterHdError, r"ancientbarbboss1 \(0D, tie: colossal1 or ancientbarb1\)"):
            H.entries(table(mod), table(d2r), models, {})
        self.assertEqual(H.entries(table(mod), table(d2r), models, {"ancientbarbboss1": "ancientbarb1"}),
                         {"ancientbarbboss1": "ancientbarb1"})


class RuleTests(unittest.TestCase):
    def test_rule_writes_d2r_map_plus_new_monsters(self):
        with tempfile.TemporaryDirectory() as tmp:
            pd2, d2r, out, rule = (Path(tmp) / n for n in ("pd2", "d2r", "out", "monsters-hd.tsv"))
            for root, text in ((d2r, D2R), (pd2, MOD)):
                (root / "global" / "excel").mkdir(parents=True)
                (root / "global" / "excel" / "monstats.txt").write_bytes(text.replace("|", "\t").encode("latin-1"))
            (d2r / H.HD).parent.mkdir(parents=True)
            (d2r / H.HD).write_text(json.dumps(MODELS), "utf-8")
            rule.write_text(RULE, "utf-8")
            H.apply(rule, pd2, d2r, out)
            written = json.loads((out / H.HD).read_text("utf-8"))
        self.assertEqual(list(written)[:len(MODELS)], list(MODELS))  # D2R's entries first
        self.assertEqual(written["skeletonIce"], "skeleton1")
        self.assertNotIn("skeletonice", written)
        self.assertEqual(len(written), len(MODELS) + 6)


if __name__ == "__main__":
    unittest.main()
