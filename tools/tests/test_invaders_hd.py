"""The seven Invader HD units wear their player class (#1215): monsters.json keys, the generated files, the clips and the tokens."""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pd2_invaders_hd as gen  # noqa: E402

HD = ROOT / "files/data/hd/character"


def unit(cls):
    return json.loads((HD / f"enemy/invader{cls}.json").read_text("utf-8"))


def d2r():
    try:
        sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT)))
        from modkit import base
        b = base.Base()
        b.text("data/hd/character/enemy/dopplezon.json")
    except (ValueError, ImportError, OSError) as e:  # no toolkit, no CascLib, no game: nothing to compare with
        raise unittest.SkipTest(f"no D2R files: {e}")
    return b


class InvaderHdTests(unittest.TestCase):
    def test_monsters_json_keys_are_the_exact_ids(self):
        keys = json.loads((HD / "monsters.json").read_text("utf-8"))
        for mid, (cls, _) in gen.CLASSES.items():
            self.assertEqual(keys[mid], f"invader{cls}")
            self.assertTrue((HD / f"enemy/invader{cls}.json").is_file())

    def test_unit_points_at_the_player_skeleton_and_its_own_state_machine(self):
        for cls in (c for c, _ in gen.CLASSES.values()):
            with self.subTest(cls):
                u = unit(cls)
                comps = {c["type"]: c for c in u["entities"][0]["components"]}
                self.assertEqual(comps["SkeletonDefinitionComponent"]["filename"],
                                 f"data/hd/character/player/{cls}/skeleton/{cls}.skeleton")
                sm = comps["UnitRootComponent"]["state_machine_filename"]
                self.assertTrue((ROOT / "files" / sm).is_file(), sm)

    def test_every_rollable_token_has_an_attachment(self):
        rows = gen.monstats2()
        for mid, (cls, _) in gen.CLASSES.items():
            with self.subTest(cls):
                have = {(c["part"], c["variant"]) for e in unit(cls)["entities"] for c in e["components"]
                        if c["type"] == "UnitPartComponent"}
                for col, (part, _) in gen.SLOTS.items():
                    for tok in filter(None, rows[mid][col].split(",")):
                        if tok not in ("nil", "lit", "med", "hvy"):
                            self.assertIn((part, tok), have)

    def test_output_matches_the_generator(self):
        b = d2r()
        rows, items = gen.monstats2(), gen.item_paths(b)
        for mid, (cls, basew) in gen.CLASSES.items():
            for rel, obj in gen.build(b, cls, basew, rows[mid], items).items():
                with self.subTest(cls + rel):
                    p = HD / "enemy" / (f"invader{cls}.json" if rel == "" else f"invader{cls}/{rel}")
                    self.assertEqual(p.read_text("utf-8"), json.dumps(obj, separators=(",", ":")))

    def test_every_clip_exists_in_the_class_state_machine(self):
        b = d2r()
        for cls, _ in gen.CLASSES.values():
            with self.subTest(cls):
                have = {a["name"] for a in json.loads(b.text(f"data/hd/character/player/{cls}/{cls}_state_machine.json"))["animations"]}
                sm = json.loads((HD / f"enemy/invader{cls}/invader{cls}_state_machine.json").read_text("utf-8"))
                for st in sm["states"]:
                    for names in st["animationBindings"].values():
                        self.assertTrue(set(names) <= have, names)
                    if st["modeId"] in gen.MODES:
                        self.assertTrue(st["animationBindings"], st["name"])


if __name__ == "__main__":
    unittest.main()
