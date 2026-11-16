"""D2R's player state machines plus body motion for PD2's sequence skills that D2R sequences under another skill.

D2R's HD renderer picks the body clip of a sequence skill (mode 18, `SQ`) by skill ID from the class's state machine.
PD2 puts some skills on a sequence (skills.txt `SQ`) that D2R's file has no state for under that skill id, so the
missiles fire while the body plays no clip. Each new state copies a D2R state whose clip fits the skill's sequence:
  Frozen Orb (64, seq 12) <- Chain Lightning (53, seq 12): same cast sequence.
  Berserk (152, seq 11)   <- Frenzy (147, seq 11): the Barbarian's melee sequence 11 (Frenzy and Double Swing share one clip).
  Blade Dance (452, seq 10) <- Whirlwind (151, seq 10): the only Assassin state on sequence 10, a claw spin clip.

    py -3 tools/pd2_sequence_states_hd.py [--d2r D2R_DATA_ROOT]   (default: the toolkit cache, as pd2_merge.py)
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path

import pd2_ids

ROOT = Path(__file__).resolve().parent.parent

def hd(cls: str) -> Path:
    return Path("hd") / "character" / "player" / cls / f"{cls}_state_machine.json"

D2R_DATA = Path(os.environ.get("MODKIT_CACHE", pd2_ids.toolkit(ROOT) / "d2r-data")) / "93847" / "data"
SEQUENCE_MODE = 18
# class -> (new skill id, source skill id, state name, transition-name suffix)
STATES = {
    "sorceress": (64, 53, "FrozenOrb_Cast", "_frozenorb"),
    "barbarian": (152, 147, "Berserk_Sequence", "_berserk"),
    "assassin": (452, 151, "BladeDance_Sequence", "_bladedance"),
}


def build(sm: dict, new_skill: int, src_skill: int, name: str, suffix: str) -> dict:
    sm = copy.deepcopy(sm)
    src = next(s for s in sm["states"] if s["modeId"] == SEQUENCE_MODE and s["skillIndex"] == src_skill)
    if any(s["skillIndex"] == new_skill and s["modeId"] == SEQUENCE_MODE for s in sm["states"]):
        raise ValueError(f"state machine already has a sequence state for skill {new_skill}")
    new_id = max(s["stateId"] for s in sm["states"]) + 1
    state = copy.deepcopy(src)
    state.update(stateId=new_id, skillIndex=new_skill, _name=name)
    sm["states"].append(state)
    own = None
    for g in list(sm["transitions"]):
        for item in g["settings"]:
            if item["to"] == src["stateId"]:
                new = copy.deepcopy(item)
                new.update(to=new_id, name=item["name"] + suffix)
                g["settings"].append(new)
                break
        if g["from"] == src["stateId"]:
            own = copy.deepcopy(g)
            own.update(**{"from": new_id, "name": g["name"] + suffix})
    if own is None:
        raise ValueError("no transition group for the source state")
    sm["transitions"].append(own)
    return sm


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--d2r", type=Path, default=D2R_DATA)
    ap.add_argument("--out", type=Path, default=ROOT / "files" / "data")
    a = ap.parse_args()
    for cls, spec in STATES.items():
        sm = json.loads((a.d2r / hd(cls)).read_text("utf-8-sig"))
        out = a.out / hd(cls)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(build(sm, *spec), separators=(",", ":")), "utf-8")
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
