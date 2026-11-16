"""The Sacrifice Altar (objects.txt row 624, class `Sacrifice Altar`) has HD art: an objects.json entry and its model json."""
import csv
import json
import unittest
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "files" / "data"


class AltarHd(unittest.TestCase):
    def test_entry_and_model(self):
        with open(DATA / "global/excel/objects.txt", encoding="latin-1", newline="") as f:
            ids = {r[0]: r[3] for r in csv.reader(f, delimiter="	")}
        self.assertEqual(ids["Sacrifice Altar"], "624")
        entry = json.loads((DATA / "hd/objects/objects.json").read_text("utf-8"))["sacrifice_altar"]
        self.assertEqual(entry["asset_path"], "shrines_healthmana")
        model = DATA / "hd/objects" / entry["asset_path"] / "sacrifice_altar.json"
        self.assertIn("models", json.loads(model.read_text("utf-8"))["dependencies"])


if __name__ == "__main__":
    unittest.main()
