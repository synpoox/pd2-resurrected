"""D2R's state machines plus body motion for PD2's sequence skills that D2R sequences under another skill.

D2R's HD renderer picks the body clip of a sequence skill (players mode 18, monsters mode 14, `SQ`) by skill ID from
the unit's state machine. PD2 puts some skills on a sequence that D2R's file has no state for under that skill id, so
the skill runs while the body plays no clip. Each new state copies a D2R state whose clip fits the skill's sequence:
  Frozen Orb (64, seq 12) <- Chain Lightning (53, seq 12): same cast sequence.
  Berserk (152, seq 11)   <- Frenzy (147, seq 11): the Barbarian's melee sequence 11 (Frenzy and Double Swing share one clip).
  Blade Dance (452, seq 10) <- Whirlwind (151, seq 10): the only Assassin state on sequence 10, a claw spin clip.
  Talic's MonWhirlwind (495) <- Whirlwind (151): D2R's Talic spins with Whirlwind (#1858).
  Korlic's Leap Attack Korlic (602) <- MonLeapAttack (367): D2R's Korlic leaps with MonLeapAttack, four steps (#1858).
The skill ids are merged skills.txt rows (tools/tests/test_sequence_states_hd.py checks them by name).

    py -3 tools/pd2_sequence_states_hd.py [--d2r D2R_DATA_ROOT]   (default: the toolkit cache, as pd2_merge.py)
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path

import pd2_table

ROOT = Path(__file__).resolve().parent.parent


def hd(kind: str, unit: str) -> Path:
    return Path("hd") / "character" / kind / unit / f"{unit}_state_machine.json"

D2R_DATA = Path(os.environ.get("MODKIT_CACHE") or pd2_table.toolkit(ROOT) / "d2r-data") / "93847" / "data"
PLAYER_SEQUENCE_MODE = 18
MONSTER_SEQUENCE_MODE = 14
# (folder, unit) -> (sequence mode, new skill id, source skill id, state name, transition-name suffix)
STATES = {
    ("player", "sorceress"): (PLAYER_SEQUENCE_MODE, 64, 53, "FrozenOrb_Cast", "_frozenorb"),
    ("player", "barbarian"): (PLAYER_SEQUENCE_MODE, 152, 147, "Berserk_Sequence", "_berserk"),
    ("player", "assassin"): (PLAYER_SEQUENCE_MODE, 452, 151, "BladeDance_Sequence", "_bladedance"),
    ("enemy", "ancientbarb1"): (MONSTER_SEQUENCE_MODE, 495, 151, "Seq_MonWhirlwind", "_monwhirlwind"),
    ("enemy", "ancientbarb3"): (MONSTER_SEQUENCE_MODE, 602, 367, "Skill_LeapAttackKorlic", "_leapattackkorlic"),
}


def build(sm: dict, mode: int, new_skill: int, src_skill: int, name: str, suffix: str) -> dict:
    """A copy of every state of src_skill in `mode` (one per step) under new_skill, wired like its source."""
    sm = copy.deepcopy(sm)
    srcs = [s for s in sm["states"] if s["modeId"] == mode and s["skillIndex"] == src_skill]
    if not srcs:
        raise ValueError(f"state machine has no sequence state for skill {src_skill}")
    if any(s["skillIndex"] == new_skill and s["modeId"] == mode for s in sm["states"]):
        raise ValueError(f"state machine already has a sequence state for skill {new_skill}")
    next_id = max(s["stateId"] for s in sm["states"]) + 1
    new_ids = {}
    for k, src in enumerate(srcs):
        new_ids[src["stateId"]] = next_id + k
        state = copy.deepcopy(src)
        state.update(stateId=next_id + k, skillIndex=new_skill, _name=name if len(srcs) == 1 else f"{name}_{src['stepIndex']}")
        sm["states"].append(state)
    owns = []
    for g in list(sm["transitions"]):
        for item in list(g["settings"]):
            if item["to"] in new_ids:
                new = copy.deepcopy(item)
                new.update(to=new_ids[item["to"]], name=item["name"] + suffix)
                g["settings"].append(new)
        if g["from"] in new_ids:
            owns.append(g)
    if len(owns) != len(srcs):
        raise ValueError("a source state has no transition group")
    for g in owns:
        own = copy.deepcopy(g)
        own.update(**{"from": new_ids[g["from"]], "name": g["name"] + suffix})
        sm["transitions"].append(own)
    return sm


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--d2r", type=Path, default=D2R_DATA)
    ap.add_argument("--out", type=Path, default=ROOT / "files" / "data")
    ap.add_argument("--only", nargs="*", help="unit names to write (default: all)")
    a = ap.parse_args()
    for (kind, unit), spec in STATES.items():
        if a.only and unit not in a.only:
            continue
        sm = json.loads((a.d2r / hd(kind, unit)).read_text("utf-8-sig"))
        out = a.out / hd(kind, unit)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(build(sm, *spec), separators=(",", ":")), "utf-8")
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
