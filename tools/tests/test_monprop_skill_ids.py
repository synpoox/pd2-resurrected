"""monprop skill and state pars name the same skill or state as PD2's row (#1134): the row numbers moved here."""
import unittest
from pathlib import Path

import data_tables as d

PD2 = Path(__file__).resolve().parents[2] / "pd2-data" / "data" / "global" / "excel"

RENAMED = {"AmpDmg": "Amplify Damage"}  # the vanilla skill under D2R's spelling, same row


def names(rows, col):
    return [r[col] for r in rows]


def pd2_rows(table):
    lines = (PD2 / f"{table}.txt").read_text("latin-1").split("\n")
    head = lines[0].rstrip("\r").split("\t")
    return [dict(zip(head, l.rstrip("\r").split("\t"))) for l in lines[1:] if l.strip()]


class MonpropSkillIds(unittest.TestCase):
    @unittest.skipUnless(PD2.is_dir(), "no pd2-data: run tools/pd2_extract.py")
    def test_par_names_match_pd2(self):
        d.need(self)
        sk = {"skill": (names(pd2_rows("skills"), "skill"), names(d.rows("skills"), "skill")),
              "state": (names(pd2_rows("states"), "state"), names(d.rows("states"), "state"))}
        pd2 = {r["Id"]: r for r in pd2_rows("monprop")}
        checked = 0
        for r in d.rows("monprop"):
            for k, v in r.items():
                if not k.startswith("prop") or r["Id"] not in pd2:
                    continue
                kind = "state" if v == "state" else "skill" if v == "aura" or v.endswith("-skill") else None
                par, src = r["par" + k[4:]], pd2[r["Id"]]["par" + k[4:]]
                if kind is None or not src.isdigit():
                    continue
                theirs, ours = sk[kind]
                self.assertEqual(ours[int(par)], RENAMED.get(theirs[int(src)], theirs[int(src)]), f"{r['Id']} {k}")
                checked += 1
        self.assertGreater(checked, 100)


if __name__ == "__main__":
    unittest.main()
