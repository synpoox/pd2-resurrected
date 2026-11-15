"""#947: the encounter placement files agree with the level ids, and every consumer lookup finds its placement."""
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


def test_every_level_is_a_real_or_reserved_id_and_private_targets_are_new():
    bindings, _ = parse()
    ids = level_ids()
    for enc, (level, target, source, _f) in bindings.items():
        assert level in ids and target in ids, enc
        if source == "Existing":
            assert target >= 209 and ids[target], enc  # a reserved private id, a `+` line
        else:
            assert not ids[target], enc  # D2R's own area


def test_private_levels_are_reserved_once():
    bindings, _ = parse()
    private = [t for (_l, t, s, _f) in bindings.values() if s == "Existing"]
    assert len(private) == len(set(private)) == 7 and sorted(private) == list(range(209, 216))


def test_every_consumer_lookup_has_a_placement():
    bindings, places = parse()
    for f in list((PLUGIN / "core-maps").rglob("*.cpp")) + list((PLUGIN / "core-rules").rglob("*.cpp")):
        text = f.read_text(encoding="utf-8")
        for enc in re.findall(r'placements::LevelOf\("(\w+)"\)', text):
            assert enc in bindings, (f.name, enc)
        for enc, pid in re.findall(r'placements::(?:SpotOf|Need|Find)\("(\w+)", "([\w-]+)"\)', text):
            assert (enc, pid) in places, (f.name, enc, pid)


def test_rathma_stages_keep_their_geometry():
    _, places = parse()
    text = (PLUGIN / "placements/rathma.cpp").read_text(encoding="utf-8")
    assert '{"rathma3", "arena", 0x4e9a, 0x1403' in text and '{"rathma1", "arena", 0x476c, 0x35bf' in text
    assert ("rathma3", "portal2") in places and ("rathma1", "portal1") not in places
