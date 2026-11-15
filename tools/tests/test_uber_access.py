"""#951: uber keys run through core-maps' own item spell, and only finished uber areas open."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BINDINGS = re.compile(r'\{"(\w+)", (\d+), (\d+), Source::(\w+), (true|false), (true|false)\}')


def bindings():
    out = {}
    for f in (ROOT / "plugin/placements").glob("*.cpp"):
        for enc, level, target, src, uber, nowp in BINDINGS.findall(f.read_text(encoding="utf-8")):
            out[enc] = (int(level), int(target), src, uber == "true", nowp == "true")
    return out


def test_keys_use_the_core_maps_item_spell():
    ops = json.loads((ROOT / "patches/080-uber-access.json").read_text(encoding="utf-8"))
    ops = ops["ops"] if isinstance(ops, dict) else ops
    keys = {o["key"]: o["set"] for o in ops if o.get("op") == "set"}
    assert set(keys) == {"Uber Map", "Rathma Map", "Lucion Map", "Uber Trist Map"}
    assert keys.pop("Uber Trist Map") == {"pSpell": "19", "len": "136"}  # D2R's Uber Tristram arena (#957)
    assert all(s == {"pSpell": "19"} for s in keys.values())
    src = (ROOT / "plugin/core-maps/uber_entry.cpp").read_text(encoding="utf-8")
    assert "UberKeySpell = 19" in src and "IsUberLevel" in src


def test_stock_areas_are_not_ubers_and_tristram_is_the_d2r_uber():
    b = bindings()
    for n in ("miniuber1", "miniuber2", "miniuber3"):
        assert b[n][2] == "Vanilla" and not b[n][3], n
    assert b["tristram"][2] == "D2RTristram" and b["tristram"][3] and b["tristram"][1] == 136


def test_every_uber_level_is_a_known_destination():
    for enc, (level, target, src, uber, _nw) in bindings().items():
        if uber:
            assert src in ("Existing", "D2RTristram"), enc
            assert target >= 209 or src == "D2RTristram", enc


def test_tristram_binding_is_d2r_136():
    src = (ROOT / "plugin/placements/miniubers.cpp").read_text()
    assert '{"tristram", 136, 136, Source::D2RTristram, true, true}' in src
