"""Expanded merc inventory (#1627): the itemtypes row core-rules patches in, its children, and the panels' slots."""
import re
import unittest
from pathlib import Path

import data_tables

ROOT = Path(__file__).resolve().parents[2]
LAYOUTS = ROOT / "files/data/global/ui/layouts"
NEW_SLOTS = {"slot_neck", "slot_right_hand", "slot_left_hand", "slot_gloves", "slot_belt", "slot_feet"}


def slot_names(text):
    return set(re.findall(r'"type": "InventorySlotWidget", "name": "(\w+)"', text))


class MercInventory(unittest.TestCase):
    def test_merc_gear_row_and_children(self):
        data_tables.need(self)
        header = (ROOT / "plugin/common/pd2_engine.h").read_text(encoding="utf-8")
        row = int(re.search(r"ItemTypeMercGear\s*=\s*(\d+)", header).group(1))
        # the game's type number skips the "Expansion" divider row
        types = [t for t in data_tables.rows("itemtypes") if t["ItemType"] != "Expansion"]
        self.assertEqual(types[row]["Code"], "merc")
        # the helm test (row 37) stays D2R's; merc replaces the tors test (row 3), so tors must sit under merc
        self.assertEqual((types[3]["Code"], types[37]["Code"]), ("tors", "helm"))
        children = {t["Code"] for t in types if "merc" in (t["Equiv1"], t["Equiv2"])}
        self.assertEqual(children, {"tors", "ring", "amul", "glov", "boot", "belt"})
        for table in ("weapons", "armor", "misc"):
            for item in data_tables.rows(table):
                self.assertNotIn("merc", (item.get("type"), item.get("type2")), item.get("code"))

    def test_a2_merc_weapons_reach_sppl(self):
        # monstats act2hire wears "sppl"; spea and pole only chain to it through pas (#1730)
        data_tables.need(self)
        types = {t["Code"]: t for t in data_tables.rows("itemtypes") if t["Code"]}
        self.assertEqual(types["pas"]["Equiv1"], "sppl")
        for code in ("spea", "pole"):
            self.assertEqual(types[code]["Equiv2"], "pas")

    def test_a1_merc_wields_crossbows_and_quivers(self):
        # roguehire wore only "bow" (left "none"); "miss" covers bow and xbow (#1779)
        data_tables.need(self)
        rogue = next(m for m in data_tables.rows("monstats") if m["Id"] == "roguehire")
        self.assertEqual((rogue["rightArmItemType"], rogue["leftArmItemType"]), ("miss", "misl"))

    def test_hireling_rows_have_the_new_rects(self):
        # the server refuses a body slot whose rect is -1 (#1627)
        data_tables.need(self)
        for row in data_tables.rows("inventory"):
            if row["class"] in ("Hireling", "Hireling2"):
                for loc in ("neck", "rHand", "lHand", "belt", "feet", "gloves"):
                    self.assertNotEqual(row[f"{loc}Left"], "-1", (row["class"], loc))

    def test_panels_have_the_new_slots(self):
        for name in ("hirelinginventorypanelhd.json", "hirelinginventorypanel.json", "controller/hirelinginventorypanelhd.json"):
            text = (LAYOUTS / name).read_text(encoding="utf-8")
            self.assertLessEqual(NEW_SLOTS, slot_names(text), name)
        for name in ("hirelinginventorypanelhd.json", "hirelinginventorypanel.json"):
            text = (LAYOUTS / name).read_text(encoding="utf-8")
            self.assertEqual(text.count('"isHireable": true'), 10, name)


if __name__ == "__main__":
    unittest.main()
