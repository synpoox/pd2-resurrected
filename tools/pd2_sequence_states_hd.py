"""D2R's state machines plus body motion for PD2's sequence skills that D2R sequences under another skill.

D2R's HD renderer picks the body clip of a sequence skill (players mode 18, monsters mode 14, `SQ`) by skill ID from
the unit's state machine. PD2 puts some skills on a sequence that D2R's file has no state for under that skill id, so
the skill runs while the body plays no clip. Each new state copies a D2R state whose clip fits the skill's sequence:
  Frozen Orb (64, seq 12) <- Chain Lightning (53, seq 12): same cast sequence.
  Berserk (152, seq 11)   <- Frenzy (147, seq 11): the Barbarian's melee sequence 11 (Frenzy and Double Swing share one clip).
  Blade Dance (452, seq 10) <- Whirlwind (151, seq 10): the only Assassin state on sequence 10, a claw spin clip.
  Talic's MonWhirlwind (495) <- Whirlwind (151): D2R's Talic spins with Whirlwind (#1858).
  Korlic's Leap Attack Korlic (602) <- MonLeapAttack (367): D2R's Korlic leaps with MonLeapAttack, four steps (#1858).
  Decoy's DoppleZonStrafe (557) <- the rogue's Attack1 (mode 4): D2R's rogue has no sequence state (#1964). It gets the
    player's Strafe pair (#2156): a first state and a follow-up state (step 1) that restarts the clip with a 0.01 s blend.
    D2R's client Strafe (cltdofunc 20) asks for the follow-up (skill, step 1) per extra arrow, for any unit.
  The A1 merc's Strafe (26) runs in plain Attack1 (mode 4). D2R keys a monster state by skill only in Cast (7) and SQ (14)
    (other modes key by mode alone, first state wins), so the merc gets only the follow-up, as a Cast-mode state (#2156).
  Duriel's MonCharge (486), MonJab (484), MonSmite (485) <- Charge (107), Jab (10), Smite (97): D2R's Duriel (#2083).
  #2092, every other monster whose PD2 skill sits on a sequence D2R keys on a vanilla skill (vanilla source state):
    Blunderbore MonSmite <- Smite; Scarab, Crusher lancer MonJab <- Jab; Siege Beast, Reanimated Horde, Claw Viper
    MonCharge <- Charge / SerpentCharge; Uber Talic UberTalicWhirlwind <- Whirlwind; Uber Korlic Leap Attack <- MonLeapAttack;
    Blood Raven BloodRavenImmo <- Nest; Frozen horror boss <- Horror Arctic Blast; Torajan's PoisonEggSpawn <- MaggotEgg;
    the Traitor boss (act2hire) skills <- Jab; Demon Road boss <- DeathMaul; Imperial Palace mini boss (fallen shaman)
    <- ShamanFire; Radament's and the Unraveler boss's casts <- UnHolyBolt (their 'sequence' clip, not Resurrect2's 'cast').
  Sequences D2R itself has no state for (Fetish raise, Swarm, Overseer, Maggot lay, Flying Scimitar, towers, ...) and
  PD2-only units with no vanilla state (Warlord of Blood, Leoric map boss, Dark Temple boss, uber Madawc, ...) stay as they are.
The skill ids are merged skills.txt rows (tools/tests/test_sequence_states_hd.py checks them by name).

    py -3 tools/pd2_sequence_states_hd.py [--d2r D2R_DATA_ROOT]   (default: the toolkit's cache, pd2_table.D2R_DATA)
"""
from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path

import pd2_table

ROOT = Path(__file__).resolve().parent.parent


def hd(kind: str, unit: str) -> Path:
    return Path("hd") / "character" / kind / unit / f"{unit}_state_machine.json"

D2R_DATA = pd2_table.D2R_DATA


def read_state_machine(d2r: Path, kind: str, unit: str) -> dict:
    """The unit's D2R state machine; a default `modkit fetch` skips per-unit folders, so fetch the missing one."""
    path = d2r / hd(kind, unit)
    if not path.exists():
        cmd = [sys.executable, "-m", "modkit", "fetch", "--prefix", f"data/{hd(kind, unit).as_posix()}"]
        try:
            subprocess.run(cmd, cwd=pd2_table.toolkit(ROOT), check=True)
        except (subprocess.CalledProcessError, OSError, ValueError) as e:
            sys.exit(f"missing {path}; fetching it failed ({e}). Run from the toolkit root: {' '.join(cmd[1:])}")
        if not path.exists():
            sys.exit(f"fetched, but {path} is still missing: modkit fetch fills pd2_table.D2R_DATA's build (or MODKIT_CACHE), "
                     "not a custom --d2r. Point --d2r at that cache or set MODKIT_CACHE.")
    return json.loads(path.read_text("utf-8-sig"))


PLAYER_SEQUENCE_MODE = 18
MONSTER_SEQUENCE_MODE = 14
MONSTER_CAST_MODE = 7


def mon(new_skill: int, src_skill: int, name: str) -> tuple:
    return (MONSTER_SEQUENCE_MODE, new_skill, src_skill, f"Seq_{name}", f"_{name.lower()}")


# (folder, unit) -> (sequence mode, new skill id, source skill id, state name, transition-name suffix[, source mode]),
# or a list of them for a unit that needs several
STATES = {
    ("player", "sorceress"): (PLAYER_SEQUENCE_MODE, 64, 53, "FrozenOrb_Cast", "_frozenorb"),
    ("player", "barbarian"): (PLAYER_SEQUENCE_MODE, 152, 147, "Berserk_Sequence", "_berserk"),
    ("player", "assassin"): (PLAYER_SEQUENCE_MODE, 452, 151, "BladeDance_Sequence", "_bladedance"),
    ("enemy", "ancientbarb1"): [(MONSTER_SEQUENCE_MODE, 495, 151, "Seq_MonWhirlwind", "_monwhirlwind"),
                                mon(579, 151, "UberTalicWhirlwind")],
    ("enemy", "ancientbarb3"): [(MONSTER_SEQUENCE_MODE, 602, 367, "Skill_LeapAttackKorlic", "_leapattackkorlic"),
                                mon(143, 367, "UberLeapAttack")],
    # Decoy (dopplezonnew) draws the rogue merc's model; its Strafe sequence repeats A1 frames. The A1 merc's Strafe (26)
    # starts in plain Attack1, so it needs only the follow-up (#2156)
    ("enemy", "roguehire"): [(MONSTER_SEQUENCE_MODE, 557, -1, "Seq_DoppleZonStrafe", "_dopplestrafe", 4, True),
                             (MONSTER_CAST_MODE, 26, -1, "Strafe_FollowUp", "_strafe", 4, False, True),
                             # #2156 experiment: the client follow-up may resolve through Cast, as for the merc.
                             (MONSTER_CAST_MODE, 557, -1, "DoppleZonStrafe_CastFollowUp", "_dopplestrafe_cast", 4, False, True)],
    ("enemy", "duriel"): [
        (MONSTER_SEQUENCE_MODE, 486, 107, "Ability_MONCHARGE_Sequence", "_moncharge"),
        (MONSTER_SEQUENCE_MODE, 484, 10, "Ability_MONJAB_Sequence", "_monjab"),
        (MONSTER_SEQUENCE_MODE, 485, 97, "Ability_MONSMITE_Sequence", "_monsmite"),
    ],
    ("enemy", "blunderbore1"): mon(485, 97, "MonSmite"),
    ("enemy", "scarab1"): mon(484, 10, "MonJab"),
    ("enemy", "cr_lancer1"): mon(484, 10, "MonJab"),
    ("enemy", "siegebeast1"): mon(486, 107, "MonCharge"),
    ("enemy", "reanimatedhorde1"): mon(486, 107, "MonCharge"),
    ("enemy", "clawviper1"): mon(486, 352, "MonCharge"),
    ("enemy", "bloodraven"): mon(434, 167, "BloodRavenImmo"),
    ("enemy", "frozenhorror1"): mon(568, 323, "HorrorArcticBlastBoss"),
    ("enemy", "maggotegg1"): mon(522, 159, "PoisonEggSpawn"),
    ("enemy", "act2hire"): [mon(s, 10, n) for s, n in ((534, "TraitorBossBlessedAim"), (535, "TraitorBossVigor"),
                                                      (536, "TraitorBossConeJab"), (537, "TraitorBossStunSpear"))],
    ("enemy", "deathmauler1"): [mon(s, 308, n) for s, n in ((646, "EruptionDemonRoadBoss"), (497, "MonDecrepify"),
                                                           (573, "RathmaPrison"))],
    ("enemy", "fallenshaman1"): [mon(s, 160, n) for s, n in ((429, "Blink"), (659, "ChargedBoltImperialMiniBoss"),
                                                            (660, "ImperialMiniBossBlast"), (598, "SiegeBeastBuffNova"))],
    ("enemy", "radament"): [mon(s, 157, n) for s, n in (
        (277, "BladeShield"), (429, "Blink"), (448, "Combustion"), (197, "DiabWall"), (677, "FingerMageSpiderTombEvent"),
        (676, "FrozenOrbTombEvent"), (604, "HowlMonster"), (330, "ImpTeleport"), (229, "MoltenBoulder"),
        (639, "RadamentBossMeteor"), (637, "RadamentBossRoyalFire"))],
    ("enemy", "unraveler1"): mon(477, 157, "UnravelerBossMultiShot"),
}


def build(sm: dict, mode: int, new_skill: int, src_skill: int, name: str, suffix: str, src_mode: int | None = None,
          pair: bool = False, follow: bool = False) -> dict:
    """A copy of every state of src_skill in src_mode (one per step) as a `mode` state under new_skill, wired like its source.

    A source from another mode (a plain attack) becomes a looping sequence state.
    pair: add the player Strafe's follow-up (step 1) beside the first state: each arrow restarts the clip, blended in 0.01 s.
    follow: the copy is that follow-up alone (step 1, entered in 0.01 s from every state, itself included)."""
    sm = copy.deepcopy(sm)
    src_mode = mode if src_mode is None else src_mode
    srcs = [s for s in sm["states"] if s["modeId"] == src_mode and s["skillIndex"] == src_skill]
    if not srcs:
        raise ValueError(f"state machine has no mode {src_mode} state for skill {src_skill}")
    if any(s["skillIndex"] == new_skill and s["modeId"] == mode for s in sm["states"]):
        raise ValueError(f"state machine already has a sequence state for skill {new_skill}")
    next_id = max(s["stateId"] for s in sm["states"]) + 1
    new_ids = {}
    for k, src in enumerate(srcs):
        new_ids[src["stateId"]] = next_id + k
        state = copy.deepcopy(src)
        state.update(stateId=next_id + k, modeId=mode, skillIndex=new_skill,
                     _name=name if len(srcs) == 1 else f"{name}_{src['stepIndex']}")
        if follow:
            state["stepIndex"] = 1
        if src_mode != mode and not (pair or follow):
            state["loopCount"] = 0
        sm["states"].append(state)
    owns = []
    for g in list(sm["transitions"]):
        for item in list(g["settings"]):
            if item["to"] in new_ids:
                new = copy.deepcopy(item)
                new.update(to=new_ids[item["to"]], name=item["name"] + suffix)
                if follow:
                    new["crossfadeSeconds"] = 0.01
                g["settings"].append(new)
        if g["from"] in new_ids:
            owns.append(g)
    if len(owns) != len(srcs):
        raise ValueError("a source state has no transition group")
    for g in owns:
        own = copy.deepcopy(g)
        own.update(**{"from": new_ids[g["from"]], "name": g["name"] + suffix})
        sm["transitions"].append(own)
    if pair:
        add_follow_up(sm, next_id, name, suffix)
    return sm


def add_follow_up(sm: dict, first_id: int, name: str, suffix: str) -> None:
    """Amazon Strafe_FollowUpAttack: the first state's clip as step 1, entered from every state a first state is,
    in 0.01 s from the first state, itself and the states a plain attack blends quickly into."""
    first = next(s for s in sm["states"] if s["stateId"] == first_id)
    fid = max(s["stateId"] for s in sm["states"]) + 1
    follow = copy.deepcopy(first)
    follow.update(stateId=fid, stepIndex=1, _name=name + "_FollowUp")
    sm["states"].append(follow)
    own = next(g for g in sm["transitions"] if g["from"] == first_id)
    for g in sm["transitions"]:
        for item in list(g["settings"]):
            if item["to"] == first_id:
                new = copy.deepcopy(item)
                new.update(to=fid, name=item["name"] + "_followup", crossfadeSeconds=0.01)
                g["settings"].append(new)
    follow_group = copy.deepcopy(own)
    follow_group.update(**{"from": fid, "name": own["name"] + "_followup"})
    sm["transitions"].append(follow_group)
    for g, to_first in ((own, False), (follow_group, True)):
        g["settings"] = [i for i in g["settings"] if i["to"] not in (first_id, fid)]
        g["settings"].append({"type": "AnimationTransitionItem", "name": g["name"] + "_follow", "crossfadeSeconds": 0.01, "to": fid})
        if to_first:
            g["settings"].append({"type": "AnimationTransitionItem", "name": g["name"] + "_first", "crossfadeSeconds": 0.2, "to": first_id})


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--d2r", type=Path, default=D2R_DATA)
    ap.add_argument("--out", type=Path, default=ROOT / "files" / "data")
    ap.add_argument("--only", nargs="*", help="unit names to write (default: all)")
    a = ap.parse_args()
    for (kind, unit), spec in STATES.items():
        if a.only and unit not in a.only:
            continue
        sm = read_state_machine(a.d2r, kind, unit)
        out = a.out / hd(kind, unit)
        out.parent.mkdir(parents=True, exist_ok=True)
        for one in spec if isinstance(spec, list) else [spec]:
            sm = build(sm, *one)
        out.write_text(json.dumps(sm, separators=(",", ":")), "utf-8")
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
