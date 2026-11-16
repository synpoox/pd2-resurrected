"""#958: the Cartographer arena rows agree with the level rows, the patch and the consumer lookups (arenas.txt, arenaspots.txt)."""
import csv
import json
import re
import unittest
from pathlib import Path

from test_placements import parse

ROOT = Path(__file__).resolve().parents[2]
CART = ROOT / "files/data/global/excel/d2rloader/cartographer"
LIMIT = 6553  # HD draws no floor past this world position (d2r-tools docs/cartographer/tables.md, arenas.txt)


def table(name):
    with open(CART / name, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def level_ops():
    """The `levels` set ops of every patch, by Id: later ops win, as the patches apply in order."""
    out = {}
    for f in sorted((ROOT / "patches").glob("*.json")):
        for op in json.loads(f.read_text(encoding="utf-8")).get("ops", []):
            if op.get("table") == "levels" and op.get("op") == "set":
                out.setdefault(op["where"]["Id"], {}).update(op["set"])
    return out


def base_offset(level):
    """OffsetX / OffsetY of a `+` level line in merge/ids-levels.tsv (the port's own rows)."""
    for line in (ROOT / "merge/ids-levels.tsv").read_text(encoding="utf-8").splitlines():
        if line.startswith("+") and line.split("\t")[1] == str(level):
            return {k: int(v) for k, v in re.findall(r"(OffsetX|OffsetY)=(-?\d+)", line)}
    return {}


class ArenaTests(unittest.TestCase):
    def setUp(self):
        self.arenas = table("arenas.txt")
        self.spots = table("arenaspots.txt")
        self.ops = level_ops()
        self.bindings, _ = parse()

    def level_of(self, arena):
        """The level row's effective values: the patch's set over the port's base line."""
        level = arena["Level"]
        values = dict(base_offset(level))
        values.update({k: int(v) for k, v in self.ops.get(level, {}).items() if re.fullmatch(r"-?\d+", v)})
        return values

    def test_every_arena_is_a_private_binding_level(self):
        private = {l for (l, _t, s, _f) in self.bindings.values() if s == "Private"}
        for arena in self.arenas:
            assert int(arena["Level"]) in private, arena["Name"]

    def test_every_arena_level_row_is_a_square_mesa_arena_inside_the_limit(self):
        for arena in self.arenas:
            ops = self.ops[arena["Level"]]
            w, h = int(arena["Width"]), int(arena["Height"])
            assert int(ops["LevelType"]) == 27 and int(ops["Pal"]) == 3, arena["Name"]
            assert arena["Tileset"] == "Mesa" and 1 <= w <= 14 and 1 <= h <= 12, arena["Name"]
            for key in ("SizeX", "SizeX(N)", "SizeX(H)"):
                assert int(ops[key]) >= w * 8 + 16, (arena["Name"], key)
            for key in ("SizeY", "SizeY(N)", "SizeY(H)"):
                assert int(ops[key]) >= h * 8 + 16, (arena["Name"], key)
            values = self.level_of(arena)
            assert values["OffsetX"] + int(ops["SizeX"]) <= LIMIT, arena["Name"]
            assert values["OffsetY"] + int(ops["SizeY"]) <= LIMIT, arena["Name"]

    def test_arenas_do_not_overlap(self):
        boxes = []
        for arena in self.arenas:
            values = self.level_of(arena)
            ops = self.ops[arena["Level"]]
            boxes.append((arena["Name"], values["OffsetX"], values["OffsetY"],
                          values["OffsetX"] + int(ops["SizeX"]), values["OffsetY"] + int(ops["SizeY"])))
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                assert a[3] <= b[1] or b[3] <= a[1] or a[4] <= b[2] or b[4] <= a[2], (a[0], b[0])

    def test_spots_are_inside_their_arena_and_unique(self):
        names = {a["Name"]: a for a in self.arenas}
        seen = set()
        for spot in self.spots:
            arena = names[spot["Arena"]]
            assert (spot["Arena"], spot["Spot"].lower()) not in seen, spot["Spot"]
            seen.add((spot["Arena"], spot["Spot"].lower()))
            assert 0 <= int(spot["X"]) < int(arena["Width"]) * 8 and 0 <= int(spot["Y"]) < int(arena["Height"]) * 8, spot

    def test_every_consumer_spot_is_in_its_arena(self):
        spots = {(s["Arena"], s["Spot"]) for s in self.spots}
        arena_at = {int(a["Level"]): a["Name"] for a in self.arenas}
        for f in (ROOT / "plugin/core-events").rglob("*.cpp"):
            for enc, spot in re.findall(r'ArenaSpotOf\("(\w+)", "(\w+)"', f.read_text(encoding="utf-8")):
                level = self.bindings[enc][0]
                assert level in arena_at, (f.name, enc)
                assert (arena_at[level], spot) in spots, (f.name, enc, spot)


if __name__ == "__main__":
    unittest.main()
