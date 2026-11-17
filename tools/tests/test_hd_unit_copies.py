"""The HD unit and state-machine copies of D2R's diablo and mephisto match D2R's originals, bar the intended edits (#1228).
Skips when no D2R install (or toolkit) is readable here."""
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))
ENEMY = "data/hd/character/enemy/"

# copy -> (D2R original unit, original state machine, the copy's state machine, intended state edits, added states)
# edits: stateId -> the fields that may differ; added: stateIds the copy adds (with their transitions)
COPIES = {
    "lucion": ("mephisto", "mephisto/mephisto_state_machine.json", "lucion/lucion_state_machine.json",
               {8: {"animationBindings"}, 9: {"animationBindings"}, 10: {"animationBindings"},
                11: {"animationBindings"}, 14: {"animationBindings"}}, {20}),  # 20: Skill_LucionDash (#1205)
    "uberdiablonew": ("diablo", "diablo/diablo_state_machine.json", "uberdiablonew/uberdiablonew_state_machine.json",
                      {i: {"skillIndex"} for i in (21, 22, 23)}, set()),  # PD2's skill 615 for D2R's 198
}


def d2r():
    try:
        sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT)))
        from modkit import base
        b = base.Base()
        b.text(ENEMY + "diablo.json")
    except (ValueError, ImportError, OSError) as e:  # no toolkit, no CascLib, no game: nothing to compare with
        raise unittest.SkipTest(f"no D2R files: {e}")  # anything else (an API break) fails
    return b


def load(b, rel):
    return json.loads(b.text(ENEMY + rel))


def mine(rel):
    return json.loads((ROOT / "files/data/hd/character/enemy" / rel).read_text("utf-8-sig"))


class HdUnitCopyTests(unittest.TestCase):
    def test_units_differ_only_in_the_state_machine_path(self):
        b = d2r()
        for name, (orig, sm_o, sm_c, _, _) in COPIES.items():
            with self.subTest(name):
                want = json.dumps(load(b, orig + ".json")).replace(ENEMY + sm_o, ENEMY + sm_c)
                self.assertEqual(json.dumps(mine(name + ".json")), want)

    def test_state_machines_differ_only_in_the_intended_edits(self):
        b = d2r()
        for name, (_, sm_o, sm_c, edits, added) in COPIES.items():
            with self.subTest(name):
                a, c = load(b, sm_o), mine(sm_c)
                self.assertEqual({k: v for k, v in c.items() if k not in ("states", "transitions")},
                                 {k: v for k, v in a.items() if k not in ("states", "transitions")})
                states = {s["stateId"]: s for s in c["states"]}
                self.assertEqual(set(states) - set(added), {s["stateId"] for s in a["states"]})
                for s in a["states"]:
                    got, ok = states[s["stateId"]], edits.get(s["stateId"], set())
                    self.assertEqual({k: v for k, v in got.items() if k not in ok}, {k: v for k, v in s.items() if k not in ok},
                                     f"state {s['stateId']} drifted")
                groups = {g["from"]: g for g in c["transitions"]}
                self.assertEqual(set(groups) - added, {g["from"] for g in a["transitions"]})
                for g in a["transitions"]:
                    kept = [i for i in groups[g["from"]]["settings"] if i["to"] not in added]
                    self.assertEqual(kept, g["settings"], f"transitions from {g['from']} drifted")
                    self.assertEqual({k: v for k, v in groups[g["from"]].items() if k != "settings"},
                                     {k: v for k, v in g.items() if k != "settings"})


if __name__ == "__main__":
    unittest.main()
