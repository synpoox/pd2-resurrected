"""The mod's own string ids (50000 and up) name one key each (#1653: the Maps tab's label shared ModItemQuantity's id)."""
import json
import unittest
from collections import defaultdict
from pathlib import Path

LNG = Path(__file__).resolve().parents[2] / "files/data/local/lng"


class StringIds(unittest.TestCase):
    def test_mod_ids_are_unique(self):
        for folder in ("strings", "strings-legacy"):
            keys = defaultdict(set)
            for f in (LNG / folder).glob("*.json"):
                for e in json.loads(f.read_text(encoding="utf-8-sig")):
                    if e["id"] >= 50000:
                        keys[e["id"]].add(e["Key"])
            self.assertEqual({i: k for i, k in keys.items() if len(k) > 1}, {}, folder)


if __name__ == "__main__":
    unittest.main()
