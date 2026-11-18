"""#1348: each extra monster group stat (itemstatcost 641-649) names its group in its own tooltip string."""
import json
import unittest

import data_tables


class ExtraGroupNamesTests(unittest.TestCase):
    def setUp(self):
        data_tables.need(self)

    def test_each_stat_has_its_own_named_string(self):
        stats = data_tables.rows("itemstatcost")
        path = data_tables.DATA / "local/lng/strings/item-modifiers.json"
        enus = {e["Key"]: e["enUS"] for e in json.loads(path.read_text(encoding="utf-8-sig"))}
        keys = set()
        for sid in range(641, 650):
            row = stats[sid]
            assert row["*ID"] == str(sid) and row["descstrneg"] == row["descstrpos"], sid
            assert "Extra Monster Group:" in enus[row["descstrpos"]], sid
            keys.add(row["descstrpos"])
        assert len(keys) == 9
