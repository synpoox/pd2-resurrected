"""The Sacrifice Altar (objects.txt row 621, class `Sacrifice Altar`) has HD art: an objects.json entry and its model json."""
import csv
import json
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files" / "data"


class AltarHd(unittest.TestCase):
    def test_entry_and_model(self):
        with open(DATA / "global/excel/objects.txt", encoding="latin-1", newline="") as f:
            rows = {r["Class"]: r for r in csv.DictReader(f, delimiter="	")}
        self.assertEqual(rows["Sacrifice Altar"]["*ID"], "621")
        self.assertEqual(rows["Sacrifice Altar"]["OperateFn"], "35")  # operate slot 35 (map_events.cpp) runs Desecration
        entry = json.loads((DATA / "hd/objects/objects.json").read_text("utf-8"))["sacrifice_altar"]
        self.assertEqual(entry["asset_path"], "shrines_healthmana")
        model = DATA / "hd/objects" / entry["asset_path"] / "sacrifice_altar.json"
        self.assertIn("models", json.loads(model.read_text("utf-8"))["dependencies"])


if __name__ == "__main__":
    unittest.main()
