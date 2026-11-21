"""Writes the seven Invader HD units: PD2's Invaders wear real player-class art (#1215).

D2R has no monster path for a player model, so each class gets its own unit JSON that names the `player/<class>`
models and skeleton, plus a monster state machine that plays that class's own clips (`<BaseW>_walk` and so on).
Built from D2R's files, as D2R's Dopplezon and Shadow Warrior do it: the body parts of `player/<class>.json`, the
monster state-machine frame of `enemy/dopplezon`, and weapon, shield and helmet attachments named by the monstats2
`RHv`/`LHv`/`SHv`/`HDv` lists (the part a monster rolls picks the attachment, as in `enemy/cr_archer1`).
Run: py -3 tools/pd2_invaders_hd.py   (rewrites files/data/hd/character/enemy/invader*, and monsters.json's seven keys)
Nothing from D2R is copied into the repo as a file: the output only names D2R's paths."""
import copy
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
CH = "data/hd/character/"
OUT = ROOT / "files/data/hd/character"

# monstats Id -> (class folder, BaseW). BaseW is the monstats2 weapon class: the player clip prefix.
CLASSES = {
    "InvaderAmazon": ("amazon", "bow"), "InvaderAssassin": ("assassin", "ht2"),
    "InvaderBarbarian": ("barbarian", "1ss"), "InvaderDruid": ("druid", "1hs"),
    "InvaderNecromancer": ("necromancer", "1hs"), "InvaderPaladin": ("paladin", "1hs"),
    "InvaderSorceress": ("sorceress", "stf"),
}
# monster mode -> the player modes whose clip it plays (the first that has one: the assassin has only Attack2, the
# barbarian no Skill2 clip, so a skill falls back to an attack)
MODES = {0: [0], 1: [1], 2: [2], 3: [4], 4: [7, 8], 5: [8, 7], 6: [9, 1], 7: [10], 8: [13, 7, 8], 9: [14, 7, 8], 10: [15, 7, 8], 11: [16, 7, 8],
         12: [17], 13: [19], 14: [7, 8], 15: [3]}
# player mode -> monster mode, for an item's own state machine (a bow draws its string by mode)
PLAYER_TO_MONSTER = {p: m for m, ps in MODES.items() for p in ps[:1] if m != 14}
# monstats2 list -> (part, the bone the attachment hangs on)
SLOTS = {"RHv": ("rhand", "R_weapon_attach"), "LHv": ("lhand", "L_weapon_attach"),
         "SHv": ("shield", "shield_attach"), "HDv": ("head", "helmet")}
ITEM_DIRS = ("weapon", "armor/helmet", "armor/shield", "armor/pelt", "armor/voodoo_head", "armor/circlet", "misc")


def monstats2():
    rows = (ROOT / "files/data/global/excel/monstats2.txt").read_text("latin-1").split("\n")
    head = rows[0].rstrip("\r").split("\t")
    return {c[0]: dict(zip(head, c)) for c in (r.rstrip("\r").split("\t") for r in rows[1:]) if c[0] in CLASSES}


def jload(b, path):
    return json.loads(b.text(path))


def exists(b, path):
    try:
        b.text(path)
        return True
    except (OSError, KeyError, ValueError):
        return False


def item_paths(b):
    """token -> D2R's item JSON path (data/hd/items/...)."""
    out = {}
    for e in jload(b, "data/hd/items/items.json"):
        for tok, v in e.items():
            asset = v["asset"]
            for d in ITEM_DIRS:
                p = f"data/hd/items/{d}/{asset if d == 'weapon' else asset.split('/', 1)[-1]}.json"
                if exists(b, p):
                    out[tok] = p
                    break
    return out


def item_machine(b, path, cls, files):
    """The item's state machine for a monster: D2R's own monster one, else its <class> player one on monster modes ('' if none)."""
    stem = path[:-5]
    name = stem.rsplit("/", 1)[1]
    for who in ("bloodraven", "cr_archer1"):
        p = f"{stem}/{name}_{who}_state_machine.json"
        if exists(b, p):
            return p
    p = f"{stem}/{name}_{cls}_state_machine.json"
    if not exists(b, p):
        return ""
    sm = jload(b, p)
    sm["unitType"] = "UNIT_MONSTER"
    for s in sm["states"]:
        s["modeId"] = PLAYER_TO_MONSTER.get(s["modeId"], 100)
    files[f"items/{name}_state_machine.json"] = sm
    return CH + f"enemy/invader{cls}/items/{name}_state_machine.json"


def build(b, cls, basew, row, items):
    """{path under enemy/invader<cls>/: JSON} plus '' for the unit itself."""
    key = f"invader{cls}"
    files = {}
    player = jload(b, CH + f"player/{cls}.json")
    dopp = jload(b, CH + "enemy/dopplezon.json")
    sm_path = CH + f"enemy/{key}/{key}_state_machine.json"

    # body: the class's own parts; the root is Dopplezon's monster root on the class skeleton
    root = copy.deepcopy(dopp["entities"][0])
    root["components"] = [c for c in root["components"] if c["type"] != "DefinitionPreloadComponent"]
    for c in root["components"]:
        if c["type"] == "UnitRootComponent":
            c["state_machine_filename"] = sm_path
        elif c["type"] == "SkeletonDefinitionComponent":
            c["filename"] = CH + f"player/{cls}/skeleton/{cls}.skeleton"
    ents = [root] + [e for e in player["entities"] if e["name"] != "entity_root"
                     and any(c["type"] in ("UnitPartComponent", "ModelDefinitionComponent") for c in e["components"])]
    deps = {k: list(v) for k, v in player["dependencies"].items()}
    deps["json"] = [{"path": sm_path}]

    # attachments: one entity per token a slot can roll
    n = 0
    for col, (part, bone) in SLOTS.items():
        for tok in dict.fromkeys(t for t in row[col].split(",") if t and t != "nil"):
            if tok in ("lit", "med", "hvy"):
                continue  # a body variant, not an item
            path = items[tok]
            n += 1
            sm = item_machine(b, path, cls, files) if part in ("rhand", "lhand") else ""
            nm = f"entity_{part}_{tok}"
            ents.append({"type": "Entity", "name": nm, "id": 1215000 + n * 7 + len(key), "components": [
                {"type": "UnitPartComponent", "name": f"{nm}_UnitPart", "part": part, "variant": tok},
                {"type": "UnitAttachmentComponent", "name": f"{nm}_UnitAttachment", "filename": path,
                 "overrideBoneName": bone, "orientTowardBoneName": "",
                 "attachmentTransform": {"type": "Transform", "name": f"{nm}_attachmentTransform",
                                         "translation": {"x": 0.0, "y": 0.0, "z": 0.0},
                                         "rotation": {"x": 0.0, "y": 0.0, "z": 0.0},
                                         "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
                 "attachmentStateMachineFilename": sm, "part": part, "variant": tok}]})
            deps["json"] += [{"path": p} for p in (path, sm) if p]
    files[""] = {"dependencies": deps, "type": "UnitDefinition", "name": key, "entities": ents}

    # state machine: Dopplezon's monster frame, every monster mode bound to the class's own clip
    pl = jload(b, CH + f"player/{cls}/{cls}_state_machine.json")
    have = {a["name"]: a["filename"] for a in pl["animations"]}
    pstates = {}
    for s in pl["states"]:
        pstates.setdefault(s["modeId"], s)

    def clip(modes):
        for nm in modes:
            for k in (basew, "hth"):
                bound = pstates.get(nm, {}).get("animationBindings", {}).get(k)
                if bound and all(x in have for x in bound):
                    return bound
        raise ValueError(f"{key}: no clip for player modes {modes}")

    sm = jload(b, CH + "enemy/dopplezon/dopplezon_state_machine.json")
    sm["name"] = key
    used = {}
    for st in sm["states"]:
        if st["modeId"] in MODES:
            c = clip(MODES[st["modeId"]])
            st["animationBindings"] = {"hth": c, basew: c}
            used.update({x: have[x] for x in c})
        else:
            st["animationBindings"] = {}  # Dopplezon's run turns and starts: not a monster mode
    sm["animations"] = [{"type": "AnimationItem", "name": k, "filename": v} for k, v in sorted(used.items())]
    sm["dependencies"] = {k: ([{"path": v} for v in sorted(used.values())] if k == "animations" else [])
                          for k in sm["dependencies"]}
    files[f"{key}_state_machine.json"] = sm
    return files


def dump(path, obj):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, separators=(",", ":")), "utf-8")


def write_all(b):
    rows, items = monstats2(), item_paths(b)
    mj = OUT / "monsters.json"
    t = mj.read_text("utf-8")
    for mid, (cls, basew) in CLASSES.items():
        key = f"invader{cls}"
        for rel, obj in build(b, cls, basew, rows[mid], items).items():
            dump(OUT / "enemy" / (f"{key}.json" if rel == "" else f"{key}/{rel}"), obj)
        t, k = re.subn(rf'("{mid}": ")[^"]*(")', rf"\g<1>{key}\g<2>", t)
        assert k == 1, mid
    mj.write_text(t, "utf-8", newline="")


if __name__ == "__main__":
    sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT)))
    from modkit import base
    write_all(base.Base())
    print("ok")
