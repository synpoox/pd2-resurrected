"""Frozen Orb plays its cast pose (#690): D2R's HD renderer picks a sequence skill's body clip by skill ID."""
import json
import unittest
from pathlib import Path

SM = Path(__file__).resolve().parents[2] / "files/data/hd/character/player/sorceress/sorceress_state_machine.json"


class FrozenOrbCast(unittest.TestCase):
    def test_sequence_state_for_skill_64(self):
        sm = json.loads(SM.read_text("utf-8"))
        state = next(s for s in sm["states"] if s["modeId"] == 18 and s["skillIndex"] == 64)
        ids = [s["stateId"] for s in sm["states"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertIn(state["stateId"], [g["from"] for g in sm["transitions"]])
        self.assertTrue(state["animationBindings"])


if __name__ == "__main__":
    unittest.main()
