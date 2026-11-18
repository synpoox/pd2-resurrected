"""#1353: Dclone's skills draw in HD as D2R's Diablo's do. Every missile a Dclone skill creates (sub-missile chains
included) has the HD entry of the matching missile of Diablo's skill, and ground_fire_big draws Diablo's flame pillars
(an entity filtered on the source skill `DiabFire`) for `UberDiabFire` too. Skips when no D2R install is readable."""
import json
import sys
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
HD = ROOT / "files/data/hd/missiles"
# Dclone's skill -> the D2R Diablo skill it copies (UberDiabSuperFire, UberDiabSummon and MonTeleport have none)
SKILLS = {"UberDiabWall": "DiabWall", "UberDiabFire": "DiabFire", "UberDiabLight": "DiabLight",
          "UberDiabCold": "DiabCold", "UberDiabRun": "DiabRun"}
MISSILE_COLS = ("srvmissile", "srvmissilea", "srvmissileb", "srvmissilec", "cltmissile", "cltmissilea",
                "cltmissileb", "cltmissilec", "cltmissiled")


def d2r():
    try:
        sys.path.insert(0, str(__import__("pd2_table").toolkit(ROOT)))
        from modkit import base
        b = base.Base()
        b.text("data/hd/missiles/missiles.json")
    except (ValueError, ImportError, OSError) as e:  # no toolkit, no CascLib, no game: nothing to compare with
        raise unittest.SkipTest(f"no D2R files: {e}")
    return b


def rows_from(text):
    lines = text.split("\n")
    head = lines[0].rstrip("\r").split("\t")
    return [dict(zip(head, ln.rstrip("\r").split("\t"))) for ln in lines[1:] if ln.strip()]


def pairs(mine, theirs, missiles_m, missiles_v):
    """(Dclone missile, Diablo missile) for the skill rows' missile columns and their sub-missile chains."""
    todo = [(mine[c], theirs[c]) for c in MISSILE_COLS if mine.get(c) or theirs.get(c)]
    seen = []
    while todo:
        m, v = todo.pop()
        if (m, v) in seen:
            continue
        seen.append((m, v))
        rm, rv = missiles_m.get(m, {}), missiles_v.get(v, {})
        todo += [(rm[c], rv.get(c, "")) for c in rm if "SubMissile" in c and (rm[c] or rv.get(c))]
    return seen


class DcloneHdTests(unittest.TestCase):
    def test_missiles_match_diablos(self):
        data_tables.need(self)
        b = d2r()
        vanilla = json.loads(b.text("data/hd/missiles/missiles.json"))
        hd = json.loads((HD / "missiles.json").read_text(encoding="utf-8"))
        skills_m = {r["skill"]: r for r in data_tables.rows("skills")}
        skills_v = {r["skill"]: r for r in rows_from(b.text("data/global/excel/skills.txt"))}
        missiles_m = {r["Missile"]: r for r in data_tables.rows("missiles")}
        missiles_v = {r["Missile"]: r for r in rows_from(b.text("data/global/excel/missiles.txt"))}
        for mine, theirs in SKILLS.items():
            for m, v in pairs(skills_m[mine], skills_v[theirs], missiles_m, missiles_v):
                with self.subTest(skill=mine, missile=m, diablo=v):
                    self.assertTrue(m and v, "the chains differ")
                    self.assertEqual(hd.get(m), vanilla[v])  # keys are case-sensitive
        # the nova has no Diablo counterpart: it keeps its legacy cel's model (#1361)
        self.assertEqual(hd["ubernova"], "expansion_baalsummonmissile")

    def test_ground_fire_big_adds_only_dclones_pillars(self):
        b = d2r()
        ours = json.loads((HD / "ground_fire_big.json").read_text(encoding="utf-8"))
        theirs = json.loads(b.text("data/hd/missiles/ground_fire_big.json"))
        added = [e for e in ours["entities"] if e not in theirs["entities"]]
        self.assertEqual({k: v for k, v in ours.items() if k != "entities"},
                         {k: v for k, v in theirs.items() if k != "entities"})
        self.assertEqual([e for e in ours["entities"] if e not in added], theirs["entities"])
        diablo = next(e for e in theirs["entities"] if e["name"] == "entity_vfx_diabloFire")
        want = json.dumps(diablo["components"]).replace('"DiabFire"', '"UberDiabFire"').replace("diabloFire", "uberDiabloFire")
        self.assertEqual(len(added), 1)
        self.assertEqual(added[0]["components"], json.loads(want))


if __name__ == "__main__":
    unittest.main()
