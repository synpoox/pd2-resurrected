"""#947: the encounter placement files agree with the level ids, and every consumer lookup finds its placement."""
import unittest
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugin"
FILES = sorted((PLUGIN / "placements").glob("*.cpp"))


def parse():
    bindings, places = {}, {}
    for f in FILES:
        text = f.read_text(encoding="utf-8")
        for enc, level, target, source in re.findall(r'\{"(\w+)", (\d+), (\d+), Source::(\w+),', text):
            assert enc not in bindings, enc
            bindings[enc] = (int(level), int(target), source, f.name)
        for enc, pid in re.findall(r'\{"(\w+)", "([\w-]+)", ', text):
            assert (enc, pid) not in places, (enc, pid)
            places[(enc, pid)] = f.name
    return bindings, places


def level_ids():
    out = {}
    for line in (ROOT / "merge/ids-levels.tsv").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("# "):
            out[int(line.split("\t")[1]) if line.split("\t")[1] != "-" else -1] = line.startswith("+")
    return out


class PlacementsTests(unittest.TestCase):
    def test_every_level_is_a_real_or_reserved_id_and_private_targets_are_new(self):
        bindings, _ = parse()
        ids = level_ids()
        for enc, (level, target, source, _f) in bindings.items():
            assert level in ids and target in ids, enc
            if source in ("Existing", "Private"):
                assert target >= 209 and ids[target], enc  # a reserved private id, a `+` line
            else:
                assert not ids[target], enc  # D2R's own area


    def test_private_levels_are_reserved_once(self):
        bindings, _ = parse()
        private = [t for (_l, t, s, _f) in bindings.values() if s in ("Existing", "Private")]
        assert len(private) == len(set(private)) == 7 and sorted(private) == list(range(209, 216))


    def test_every_consumer_lookup_has_a_placement(self):
        bindings, places = parse()
        for f in list((PLUGIN / "core-events").rglob("*.cpp")) + list((PLUGIN / "core-rules").rglob("*.cpp")):
            text = f.read_text(encoding="utf-8")
            for enc in re.findall(r'placements::LevelOf\("(\w+)"\)', text):
                assert enc in bindings, (f.name, enc)
            for enc, pid in re.findall(r'placements::(?:SpotOf|Need|Find)\("(\w+)", "([\w-]+)"\)', text):
                assert (enc, pid) in places, (f.name, enc, pid)


    def test_rathma_places_follow_the_arena_spots(self):
        _, places = parse()
        cart = ROOT / "files/data/global/excel/d2rloader/cartographer"
        spots = {(r[0], r[1]): (int(r[2]), int(r[3])) for r in (ln.split("\t") for ln in
                 (cart / "arenaspots.txt").read_text(encoding="utf-8").splitlines()[1:]) if r[0]}
        offy = {1: 4500, 2: 4800, 3: 5100}
        text = (PLUGIN / "placements/rathma.cpp").read_text(encoding="utf-8")
        pl = {(e, i): (int(x), int(y)) for e, i, x, y in
              re.findall(r'\{"(rathma\d)", "(\w+)", (\d+), (\d+), ', text)}
        for n in (1, 2, 3):
            assert f"Rathma {n}\t{209 + n}\tMesa\t12\t12\tMesa1" in (cart / "arenas.txt").read_text(encoding="utf-8")
            for (arena, name), (tx, ty) in spots.items():
                if arena == f"Rathma {n}":
                    assert pl[(f"rathma{n}", name)] == ((6000 + 8 + tx) * 5 + 2, (offy[n] + 8 + ty) * 5 + 2), (n, name)
            for boss in ("boss0", "boss1"):
                if (f"rathma{n}", boss) in pl:
                    ax, ay = pl[(f"rathma{n}", "arena")]
                    assert abs(pl[(f"rathma{n}", boss)][0] - ax) <= 3 and pl[(f"rathma{n}", boss)][1] == ay
        assert ("rathma3", "portal2") in places and ("rathma1", "portal1") not in places


    def test_no_consumer_hard_codes_a_bound_level_id(self):
        """A bound level number (133-138, 160-162, 181, 184, 185, 198, 213, 214) in code that is not a placements lookup fails."""
        bound = {133, 134, 135, 136, 138, 160, 161, 162, 181, 184, 185, 198, 213, 214}
        # Lines naming these numbers for another reason: table data keyed by other ids, comments, stat/skill/AI rows.
        allowed = {("map_bosses.cpp", "{159, PlaceholderA"), ("map_events.cpp", "{144, 200}"), ("map_stats.cpp", "{460, Route"),
                   ("map_stats.cpp", "{177, {178}}"), ("map_stats.cpp", "{179, {180}}"), ("map_stats.cpp", "MapState = 244"),
                   ("map_stats.cpp", "FirstMapLevel = 138"), ("map_stats.cpp", "fixed[] ="), ("unique_map_drops.cpp", "FirstMapLevel = 138"),
                   ("prime_minions.cpp", "AiRows"), ("prime_minions.cpp", "AI rows"), ("uber_entry.cpp", "StatUberTier"),
                   ("uber_entry.cpp", "133, 134, 135, 136, 137, 176")}
        bad = []
        for f in (PLUGIN / "core-events").rglob("*.cpp"):
            for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
                code = line.split("//")[0]
                if re.search(r"0x|rva::|(PartReg|CountersReg) \w+\(", code):
                    continue
                if any(int(m) in bound for m in re.findall(r"(?<![\w.])(\d{3})(?![\w.])", code)):
                    if not any(f.name == a and b in line for a, b in allowed) and not re.search(r"\{\d+, PlaceholderA", code):
                        bad.append(f"{f.name}:{n}: {line.strip()}")
        assert not bad, bad
