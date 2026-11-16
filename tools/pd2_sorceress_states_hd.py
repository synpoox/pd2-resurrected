"""D2R's sorceress state machine plus a cast pose for PD2's Frozen Orb (skill 64).

D2R's HD renderer picks the body clip of a sequence skill (mode 18, `SQ`) by skill ID from the class's state machine.
PD2 keeps Frozen Orb on the Chain Lightning sequence (skills.txt `SQ`/`SC`), which D2R's file has no state for under
skill 64, so the orb and its bolts fire while the body plays no clip. The new state copies Chain Lightning's.

    py -3 tools/pd2_sorceress_states_hd.py [--d2r D2R_DATA_ROOT]   (default: the toolkit cache, as pd2_merge.py)
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path

import pd2_ids

ROOT = Path(__file__).resolve().parent.parent
HD = Path("hd") / "character" / "player" / "sorceress" / "sorceress_state_machine.json"
D2R_DATA = Path(os.environ.get("MODKIT_CACHE", pd2_ids.toolkit(ROOT) / "d2r-data")) / "93847" / "data"
SEQUENCE_MODE, CHAIN_LIGHTNING, FROZEN_ORB = 18, 53, 64


def build(sm: dict) -> dict:
    sm = copy.deepcopy(sm)
    src = next(s for s in sm["states"] if s["modeId"] == SEQUENCE_MODE and s["skillIndex"] == CHAIN_LIGHTNING)
    if any(s["skillIndex"] == FROZEN_ORB and s["modeId"] == SEQUENCE_MODE for s in sm["states"]):
        raise ValueError("state machine already has a sequence state for skill 64")
    new_id = max(s["stateId"] for s in sm["states"]) + 1
    state = copy.deepcopy(src)
    state.update(stateId=new_id, skillIndex=FROZEN_ORB, _name="FrozenOrb_Cast")
    sm["states"].append(state)
    own = None
    for g in list(sm["transitions"]):
        for item in g["settings"]:
            if item["to"] == src["stateId"]:
                new = copy.deepcopy(item)
                new.update(to=new_id, name=item["name"] + "_frozenorb")
                g["settings"].append(new)
                break
        if g["from"] == src["stateId"]:
            own = copy.deepcopy(g)
            own.update(**{"from": new_id, "name": g["name"] + "_frozenorb"})
    if own is None:
        raise ValueError("no transition group for Chain Lightning's state")
    sm["transitions"].append(own)
    return sm


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--d2r", type=Path, default=D2R_DATA)
    ap.add_argument("--out", type=Path, default=ROOT / "files" / "data")
    a = ap.parse_args()
    sm = json.loads((a.d2r / HD).read_text("utf-8-sig"))
    out = a.out / HD
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(build(sm), separators=(",", ":")), "utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
