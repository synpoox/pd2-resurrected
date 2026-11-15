"""#1007: a PD2 row's BaseId is the exact monstats Id (D2R reads it case-sensitively)."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_lucion_and_kin_name_their_own_exact_id():
    ops = json.loads((ROOT / "patches/090-baseid-case.json").read_text(encoding="utf-8"))["ops"]
    assert all(o["table"] == "monstats" and o["set"] == {"BaseId": o["key"]} for o in ops)
    assert {"Lucion", "LucionControl", "LucionSpawner"} <= {o["key"] for o in ops}
