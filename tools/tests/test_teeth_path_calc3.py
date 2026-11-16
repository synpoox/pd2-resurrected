"""#464: srvdofunc 8 spawns calc3 damaging missiles unclamped; calc3 100 made 100 server missiles for calc1 on screen."""
import json
from pathlib import Path

OPS = json.loads((Path(__file__).resolve().parents[2] / "patches/075-teeth-path-calc3.json").read_text())["ops"]


def test_bone_spear_and_teeth_damage_only_their_own_missiles():
    keys = {o["key"]: o["set"] for o in OPS}
    assert keys["Bone Spear"] == keys["Teeth"] == {"calc3": "0"}  # 0 = the count (calc1)


def test_every_op_is_a_calc3_reset_on_skills():
    assert OPS and all(o["table"] == "skills" and o["set"] == {"calc3": "0"} for o in OPS)
