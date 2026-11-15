"""#1007: Rathma's bones and Lucion take the D2R Code of the model they draw, so D2R's hover finds them."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def test_rathma_bones_and_lucion_use_d2r_codes():
    ops = json.loads((ROOT / "patches/090-hover-codes.json").read_text(encoding="utf-8"))["ops"]
    codes = {o["key"]: o["set"]["Code"] for o in ops if o["table"] == "monstats"}
    assert codes == {"rathmaBone": "XU", "rathmaBoneClone": "XU", "Lucion": "MP"}
    likes = dict(l.split("\t") for l in (ROOT / "merge/monsters-hd.tsv").read_text(encoding="utf-8").splitlines() if l and l[0] != "#")
    assert likes["rathmaBone"] == "nihlathakboss" and likes["Lucion"] == "mephisto"
