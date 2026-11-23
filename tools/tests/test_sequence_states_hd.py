"""PD2's sequence skills play their body motion (#690, #1166): D2R's HD renderer picks a sequence skill's clip by skill ID."""
import csv
import json
import unittest
from pathlib import Path

FILES = Path(__file__).resolve().parents[2] / "files/data"
CLASSES = {"ama": "amazon", "sor": "sorceress", "pal": "paladin", "bar": "barbarian", "dru": "druid",
           "ass": "assassin", "war": "warlock", "nec": "necromancer"}


def state_machine(cls: str) -> Path:
    return FILES / f"hd/character/player/{cls}/{cls}_state_machine.json"


class SequenceStates(unittest.TestCase):
    def test_new_states_are_wired(self):
        for cls, skill in (("sorceress", 64), ("barbarian", 152), ("assassin", 452)):
            sm = json.loads(state_machine(cls).read_text("utf-8"))
            state = next(s for s in sm["states"] if s["modeId"] == 18 and s["skillIndex"] == skill)
            ids = [s["stateId"] for s in sm["states"]]
            self.assertEqual(len(ids), len(set(ids)))
            self.assertIn(state["stateId"], [g["from"] for g in sm["transitions"]])
            self.assertTrue(state["animationBindings"])

    def test_every_player_sq_skill_has_a_state(self):
        # Classes without a committed file use D2R's own, which already has a state for each of their SQ skills.
        rows = list(csv.reader((FILES / "global/excel/skills.txt").open(encoding="utf-8-sig"), delimiter="\t"))
        head = rows[0]
        checked = set()
        for row in rows[1:]:
            d = dict(zip(head, row))
            if d["anim"] != "SQ" or d["charclass"] not in CLASSES:
                continue
            cls = CLASSES[d["charclass"]]
            if not state_machine(cls).exists():
                continue
            sm = json.loads(state_machine(cls).read_text("utf-8"))
            checked.add(cls)
            self.assertTrue(any(s["modeId"] == 18 and s["skillIndex"] == int(row[1]) for s in sm["states"]),
                            f"{d['skill']} ({row[1]}) has no sequence state in the {cls} state machine")
        self.assertEqual(checked, {"sorceress", "barbarian", "assassin"})

    def test_ancients_states_follow_their_skill_rows(self):
        # #1858: the HD clip is picked by skill id, so the copied states must carry the merged row of PD2's skill
        rows = list(csv.reader((FILES / "global/excel/skills.txt").open(encoding="utf-8-sig"), delimiter="\t"))
        ids = {row[0]: int(row[1]) for row in rows[1:]}
        for unit, skill, steps in (("ancientbarb1", "MonWhirlwind", 1), ("ancientbarb3", "Leap Attack Korlic", 4)):
            sm = json.loads((FILES / f"hd/character/enemy/{unit}/{unit}_state_machine.json").read_text("utf-8"))
            states = [s for s in sm["states"] if s["modeId"] == 14 and s["skillIndex"] == ids[skill]]
            self.assertEqual(len(states), steps, f"{unit}: {skill} ({ids[skill]})")
            froms = [g["from"] for g in sm["transitions"]]
            for s in states:
                self.assertIn(s["stateId"], froms)


if __name__ == "__main__":
    unittest.main()
