"""pd2_missiles_hd on synthetic tables (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_missiles_hd as H  # noqa: E402
from pd2_ids import Table  # noqa: E402


def table(text: str) -> Table:
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "t.txt"
        p.write_bytes(text.replace("|", "\t").encode("latin-1"))
        return Table(p)


D2R = "Missile|CelFile\r\nthrowaxe|MissileHandAxe\r\nancient throwing axe|MissileHandAxe\r\nbighead1|BigHead\r\n" \
      "shock field on ground|Expansion\\ShockField\r\n"
MODELS = {"dependencies": {}, "throwaxe": "missile_hand_axe", "ancient_throwing_axe": "missile_hand_axe",
          "bighead_1": "bighead_lightning_missile", "shock_field_on_ground": "expansion_shock_field"}
# D2R's rows, then PD2's: a CelFile match (folder prefix differs), PD2-only art, and a name D2R would underscore
MOD = D2R + "splitthrowaxe|MissileHandAxe\r\nurehChargedGround|expansion\\shockfield\r\nzharbolts|ZharBolt\r\n" \
            "iskatubliz1|iskatubliz\r\nA3 Merc fireball|BigHead\r\n"
LIKES = {"zharbolts": "bighead1", "iskatubliz1": "shock field on ground"}


class EntriesTests(unittest.TestCase):
    def test_celfile_match_and_lines(self):
        self.assertEqual(H.entries(table(MOD), table(D2R), MODELS, LIKES), {
            "splitthrowaxe": "missile_hand_axe", "urehchargedground": "expansion_shock_field",
            "urehChargedGround": "expansion_shock_field",  # capitals: both spellings (JSON keys are case-sensitive)
            "a_3_merc_fireball": "bighead_lightning_missile", "A_3_Merc_fireball": "bighead_lightning_missile",
            "zharbolts": "bighead_lightning_missile", "iskatubliz_1": "expansion_shock_field"})

    def test_refusals(self):
        cases = [
            ({"zharbolts": "bighead1"}, r"no HD model for 1 new missiles.*iskatubliz1 \(iskatubliz\)"),
            (LIKES | {"splitthrowaxe": "bighead1"}, "'splitthrowaxe' has a CelFile match"),
            (LIKES | {"throwaxe": "bighead1"}, "not new: throwaxe"),
            (LIKES | {"zharbolts": "nope"}, "'nope' has no missiles.json entry"),
        ]
        for likes, msg in cases:
            with self.subTest(msg), self.assertRaisesRegex(H.MissileHdError, msg):
                H.entries(table(MOD), table(D2R), MODELS, likes)
        with self.assertRaisesRegex(H.MissileHdError, "already has 'bighead_1'"):
            H.entries(table(D2R + "bighead_1|BigHead\r\n"), table(D2R), MODELS, {})
        with self.assertRaisesRegex(H.MissileHdError, "already has 'Zap_1'"):  # the own-spelling key is checked too
            H.entries(table(D2R + "Zap1|BigHead\r\n"), table(D2R), MODELS | {"Zap_1": "x"}, {})


class RuleTests(unittest.TestCase):
    def test_rule_writes_d2r_map_plus_new_missiles(self):
        with tempfile.TemporaryDirectory() as tmp:
            d2r, out, rule = Path(tmp) / "d2r", Path(tmp) / "out", Path(tmp) / "missiles-hd.tsv"
            for root, text in ((d2r, D2R), (out, MOD)):
                (root / "global" / "excel").mkdir(parents=True)
                (root / "global" / "excel" / "missiles.txt").write_bytes(text.replace("|", "\t").encode("latin-1"))
            (d2r / H.HD).parent.mkdir(parents=True)
            (d2r / H.HD).write_text(json.dumps(MODELS), "utf-8")
            rule.write_text("# missile\tlike\nzharbolts\tbighead1\niskatubliz1\tshock field on ground\n", "utf-8")
            pd2 = Path(tmp) / "pd2" / "global" / "missiles"
            pd2.mkdir(parents=True)
            (pd2 / "iskatubliz.dcc").write_bytes(b"dcc")
            (pd2 / "unused.dcc").write_bytes(b"x")
            H.apply(rule, Path(tmp) / "pd2", d2r, out)
            written = json.loads((out / H.HD).read_text("utf-8"))
            # legacy mode: PD2's own DCC for a row that uses it, nothing else (#350)
            self.assertEqual([p.name for p in (out / "global" / "missiles").iterdir()], ["iskatubliz.dcc"])
            self.assertEqual((out / "global" / "missiles" / "iskatubliz.dcc").read_bytes(), b"dcc")
        self.assertEqual(list(written)[:len(MODELS)], list(MODELS))  # D2R's entries first, dependencies leading
        self.assertEqual(written["splitthrowaxe"], "missile_hand_axe")
        self.assertEqual(len(written), len(MODELS) + 7)


if __name__ == "__main__":
    unittest.main()
