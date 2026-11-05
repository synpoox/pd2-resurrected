"""pd2_overlays_hd on synthetic tables (no PD2 or D2R data)."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_overlays_hd as H  # noqa: E402

D2R = "overlay|Filename|Character\r\nbattleorders|BattleOrders|common\r\nteleport|Teleport|sorceress\r\n"
# the merged table: D2R's rows, then PD2's appended ones with the blank Character tables.json leaves them
MERGED = D2R + "teleport_debuff|teleport_debuff|\r\nMendelnEvent|Extra\\GolemCast|\r\ndclone_t8_aura|aurared|\r\n"
RULE = "# overlay\tlike\nteleport_debuff\tteleport\nmendelnevent\tbattleorders\ndclone_t8_aura\tbattleorders\n"


def setup(tmp: Path, rule: str = RULE) -> tuple[Path, Path, Path]:
    d2r, out, path = tmp / "d2r", tmp / "out", tmp / "vfx-overlays.tsv"
    for root, text in ((d2r, D2R), (out, MERGED)):
        (root / "global" / "excel").mkdir(parents=True)
        (root / "global" / "excel" / "overlay.txt").write_bytes(text.replace("|", "\t").encode("latin-1"))
    for folder, name in (("common", "battleorders"), ("sorceress", "teleport")):
        (d2r / H.HD / folder).mkdir(parents=True, exist_ok=True)
        (d2r / H.HD / folder / f"{name}.json").write_text(json.dumps({"type": "OverlayDefinition", "name": name}))
    path.write_text(rule, "utf-8")
    return d2r, out, path


class RuleTests(unittest.TestCase):
    def test_new_rows_get_the_like_folder_and_a_copied_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            d2r, out, rule = setup(Path(tmp))
            H.apply(rule, Path(tmp) / "pd2", d2r, out)
            rows = (out / "global" / "excel" / "overlay.txt").read_text("latin-1").splitlines()
            self.assertEqual(rows[3:], ["teleport_debuff\tteleport_debuff\tsorceress",
                                        "MendelnEvent\tExtra\\GolemCast\tcommon", "dclone_t8_aura\taurared\tcommon"])
            self.assertEqual(rows[:3], D2R.replace("|", "\t").splitlines())  # D2R's rows untouched
            # the file name keeps PD2's spelling (JSON keys and names are case-sensitive)
            copied = json.loads((out / H.HD / "common" / "MendelnEvent.json").read_text("utf-8"))
            self.assertEqual(copied, {"type": "OverlayDefinition", "name": "MendelnEvent"})
            self.assertTrue((out / H.HD / "sorceress" / "teleport_debuff.json").is_file())
            # D2R's file name splits a letter from a digit (its own doubledamage1 is doubledamage_1.json)
            copied = json.loads((out / H.HD / "common" / "dclone_t_8_aura.json").read_text("utf-8"))
            self.assertEqual(copied["name"], "dclone_t_8_aura")

    def test_refusals(self):
        cases = [
            ("# x\nteleport_debuff\tteleport\ndclone_t8_aura\tteleport\n", r"no HD overlay for 1 new overlays.*MendelnEvent"),
            (RULE + "battleorders\tteleport\n", "not new: battleorders"),
            (RULE.replace("mendelnevent\tbattleorders", "mendelnevent\tnope"), "'nope' has no D2R overlay json"),
            ("teleport_debuff\n", "line 1: want 'overlay<tab>like'"),
        ]
        for rule, msg in cases:
            with self.subTest(msg), tempfile.TemporaryDirectory() as tmp:
                d2r, out, path = setup(Path(tmp), rule)
                with self.assertRaisesRegex(H.OverlayHdError, msg):
                    H.apply(path, Path(tmp) / "pd2", d2r, out)


if __name__ == "__main__":
    unittest.main()
