"""core-rules' boss aura list is fixed skills.txt ids (#1830): a renumbered skills.txt would give bosses other skills."""
import re
import unittest
from pathlib import Path

import data_tables

SOURCE = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "boss_aura.cpp"
NAMES = ["Mon Conviction", "Mon Fanaticism", "Mon Holy Shock", "MonHolyFreeze", "Mon Holy Fire", "Mon Might",
         "Mon Concentration", "Mon Vigor"]


class BossAuraList(unittest.TestCase):
    def test_list_ids_are_pd2s_eight_monster_auras(self):
        data_tables.need(self)
        text = SOURCE.read_text(encoding="utf-8")
        ids = [int(n) for n in re.search(r"AuraList\[\] = \{([^}]*)\}", text).group(1).split(",")]
        skills = data_tables.rows("skills")
        self.assertEqual([skills[i]["skill"] for i in ids], NAMES)
        fanaticism = int(re.search(r"MonFanaticism = (\d+);", text).group(1))
        self.assertEqual(skills[fanaticism]["skill"], "Mon Fanaticism")

    def test_lord_de_seis_row(self):
        data_tables.need(self)
        row = int(re.search(r"LordDeSeis = (\d+);", SOURCE.read_text(encoding="utf-8")).group(1))
        self.assertEqual(data_tables.rows("superuniques")[row]["Superunique"], "Lord De Seis")


if __name__ == "__main__":
    unittest.main()
