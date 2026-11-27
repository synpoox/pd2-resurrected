"""King Leoric's replacement art must support his existing animation modes and charge (#2260)."""
import csv
import json
import struct
import sys
import unittest
from pathlib import Path

FILES = Path(__file__).resolve().parents[2] / "files/data"
sys.path.insert(0, str(FILES.parents[1] / "tools"))
import pd2_animdata


def table(name):
    with (FILES / f"global/excel/{name}.txt").open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class LeoricGraphics(unittest.TestCase):
    def test_legacy_family_has_original_frames_and_action_events(self):
        boss = next(r for r in table("monstats") if r["Id"] == "leoricMapBoss")
        self.assertEqual(boss["Code"], "RD")
        self.assertEqual(boss["AI"], "ReanimatedHorde")
        self.assertEqual(boss["Sk2mode"], "seq_leoriccharge")
        self.assertEqual(boss["Sk1mode"], "SC")
        self.assertEqual(boss["Sk3mode"], "SC")
        ex = next(r for r in table("monstats2") if r["Id"] == boss["MonStatsEx"])
        self.assertEqual((ex["BaseW"], ex["TotalPieces"], ex["mSQ"]), ("hth", "1", "1"))
        records = {pd2_animdata.name_of(r): r for b in pd2_animdata.read(
            (FILES / "global/animdata.d2").read_bytes()) for r in b}
        for mode in ("NU", "WL", "A1", "A2", "SC", "GH", "DT", "DD"):
            original = records[f"!9{mode}HTH"]
            native = records[f"RD{mode}HTH"]
            self.assertEqual(struct.unpack_from("<I", original, 8), struct.unpack_from("<I", native, 8), mode)
            self.assertEqual(original[16:], native[16:], mode)

    def test_hd_cast_and_charge_clips(self):
        models = json.loads((FILES / "hd/character/monsters.json").read_text("utf-8"))
        model = models["leoricMapBoss"]
        self.assertEqual(model, "radament")
        sm = json.loads((FILES / f"hd/character/enemy/{model}/{model}_state_machine.json").read_text("utf-8"))
        skills = {r["skill"]: int(r["*Id"]) for r in table("skills") if r["*Id"].isdigit()}
        ids = set()
        for mode, skill, step, clip in ((14, "MonCharge", 0, "walk"), (14, "MonCharge", 1, "attack2"),
                                       (7, "LeoricSummonNova", 0, "cast"), (7, "Bone Armor", 0, "cast")):
            states = [s for s in sm["states"] if (s["modeId"], s["skillIndex"], s["stepIndex"])
                      == (mode, skills[skill], step)]
            self.assertEqual(len(states), 1)
            state = states[0]
            self.assertEqual(state["animationBindings"]["hth"], [clip])
            ids.add(state["stateId"])
            self.assertIn(state["stateId"], [g["from"] for g in sm["transitions"]])
        self.assertEqual(len(ids), 4)


if __name__ == "__main__":
    unittest.main()
