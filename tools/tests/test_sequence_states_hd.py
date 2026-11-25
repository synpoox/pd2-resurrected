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

    def test_monster_states_follow_their_skill_rows(self):
        # #1858, #1964, #2083, #2092: the HD clip is picked by skill id, so the copied states must carry the merged row of PD2's skill
        rows = list(csv.reader((FILES / "global/excel/skills.txt").open(encoding="utf-8-sig"), delimiter="\t"))
        ids = {row[0]: int(row[1]) for row in rows[1:]}
        for unit, skill, steps in (("ancientbarb1", "MonWhirlwind", 1), ("ancientbarb3", "Leap Attack Korlic", 4),
                                  ("roguehire", "DoppleZonStrafe", 2), ("duriel", "MonCharge", 1),
                                  ("duriel", "MonJab", 1), ("duriel", "MonSmite", 1),
                                  # #2092
                                  ("ancientbarb1", "UberTalicWhirlwind", 1), ("ancientbarb3", "Leap Attack", 4),
                                  ("blunderbore1", "MonSmite", 1), ("scarab1", "MonJab", 1), ("cr_lancer1", "MonJab", 1),
                                  ("siegebeast1", "MonCharge", 1), ("reanimatedhorde1", "MonCharge", 2),
                                  ("clawviper1", "MonCharge", 1), ("bloodraven", "BloodRavenImmo", 1),
                                  ("frozenhorror1", "Horror Arctic Blast Boss", 1), ("maggotegg1", "PoisonEggSpawn", 1),
                                  ("act2hire", "TraitorBossBlessedAim", 1), ("act2hire", "TraitorBossVigor", 1),
                                  ("act2hire", "TraitorBossConeJab", 1), ("act2hire", "TraitorBossStunSpear", 1),
                                  ("deathmauler1", "EruptionDemonRoadBoss", 2), ("deathmauler1", "MonDecrepify", 2),
                                  ("deathmauler1", "RathmaPrison", 2), ("fallenshaman1", "Blink", 1),
                                  ("fallenshaman1", "ChargedBoltImperialMiniBoss", 1),
                                  ("fallenshaman1", "ImperialMiniBossBlast", 1), ("fallenshaman1", "SiegeBeastBuffNova", 1),
                                  ("radament", "Blade Shield", 1), ("radament", "Blink", 1), ("radament", "Combustion", 1),
                                  ("radament", "DiabWall", 1), ("radament", "FingerMageSpiderTombEvent", 1),
                                  ("radament", "Frozen OrbTombEvent", 1), ("radament", "Howl Monster", 1),
                                  ("radament", "Imp Teleport", 1), ("radament", "Molten Boulder", 1),
                                  ("radament", "RadamentBossMeteor", 1), ("radament", "RadamentBossRoyalFire", 1),
                                  ("unraveler1", "UnravelerBossMultiShot", 1)):
            sm = json.loads((FILES / f"hd/character/enemy/{unit}/{unit}_state_machine.json").read_text("utf-8"))
            states = [s for s in sm["states"] if s["modeId"] == 14 and s["skillIndex"] == ids[skill]]
            self.assertEqual(len(states), steps, f"{unit}: {skill} ({ids[skill]})")
            froms = [g["from"] for g in sm["transitions"]]
            for s in states:
                self.assertIn(s["stateId"], froms)

    def test_a1_merc_strafe_follow_up(self):
        # #2156: D2R keys a monster state by (skill, step) only in Cast (7) and SQ (14); other modes key by mode alone
        # (first state wins), so a skill state there is dead. The merc's Strafe starts in plain Attack1 and each extra
        # arrow asks for (26, step 1): one Cast-mode state, entered in 0.01 s from every state, itself included.
        sm = json.loads((FILES / "hd/character/enemy/roguehire/roguehire_state_machine.json").read_text("utf-8"))
        self.assertFalse([s for s in sm["states"] if s["skillIndex"] != -1 and s["modeId"] not in (7, 14)])
        attack1 = next(s for s in sm["states"] if s["modeId"] == 4 and s["skillIndex"] == -1)
        follow = [s for s in sm["states"] if s["skillIndex"] == 26]
        self.assertEqual([(s["modeId"], s["stepIndex"]) for s in follow], [(7, 1)])
        self.assertEqual(follow[0]["animationBindings"], attack1["animationBindings"])
        fid = follow[0]["stateId"]
        for g in sm["transitions"]:
            self.assertIn((fid, 0.01), [(i["to"], i["crossfadeSeconds"]) for i in g["settings"]], g["name"])


if __name__ == "__main__":
    unittest.main()
