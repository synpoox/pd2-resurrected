"""#1027: Dclone's and Lucion's box arenas ship as frozen static content in files/, and the merge leaves them alone."""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from pd2_ds1 import ds1_header, frozen  # noqa: E402

DATA = ROOT / "files" / "data"
ARENAS = {"uberdiablolvl": (48, 24), "lucionarena": (50, 50)}  # DS1 stem -> tiles
DT1 = ["global/tiles/pd2assets/v/act4/fort/foregate.dt1", "global/tiles/pd2assets/hellcaves_base.dt1",
       "global/tiles/pd2assets/hellcaves_obj.dt1", "global/tiles/pd2assets/void.dt1"]


def test_frozen_files_exist_and_are_the_arenas():
    for stem, (w, h) in ARENAS.items():
        ds1 = (DATA / f"global/tiles/expansion/map/{stem}.ds1").read_bytes()
        _v, width, height, *_ = ds1_header(ds1)
        assert (width, height) == (w, h), stem
        preset = json.loads((DATA / f"hd/env/preset/expansion/map/{stem}.json").read_text("utf-8"))
        assert preset["name"] == stem and preset["entities"], stem
    for rel in DT1:
        data = (DATA / rel).read_bytes()
        assert struct.unpack_from("<2i", data) == (7, 6) and len(data) > 1000, rel  # a DT1 (version 7.6)


def test_the_build_references_them():
    """lvlprest and levels name each DS1 (PD2's rows, ids kept in merge/), and the merge skips its copy of a frozen file."""
    ids = (ROOT / "merge/ids-lvlprest.tsv").read_text("utf-8")
    assert re.search(r"^UberDiabloLvl\t", ids, re.M) and re.search(r"^LucionArena\t", ids, re.M)
    merge = ROOT / "merge"
    for stem in ARENAS:
        assert frozen(merge, f"global/tiles/expansion/map/{stem}.ds1")
        assert frozen(merge, f"hd/env/preset/expansion/map/{stem}.json")
    assert not frozen(merge, "global/tiles/expansion/map/hellcaves.ds1")  # every other PD2-only area is still converted
    for src in ("tools/pd2_levels.py", "tools/presets/apply.py"):
        assert "frozen(" in (ROOT / src).read_text("utf-8"), src


def test_only_the_two_boxes_are_frozen_encounters():
    bound = {}
    for f in (ROOT / "plugin/placements").glob("*.cpp"):
        for enc, level, _t, src in re.findall(r'\{"(\w+)", (\d+), (\d+), Source::(\w+),', f.read_text("utf-8")):
            bound[enc] = (int(level), src)
    assert {e for e, (_l, s) in bound.items() if s == "Frozen"} == {"dclone", "lucion"}
    assert bound["dclone"] == (138, "Frozen") and bound["lucion"] == (184, "Frozen")
    for enc in ("rathma1", "rathma2", "rathma3", "temple", "bitterdepths"):
        assert bound[enc][1] != "Frozen", enc  # their arenas still come from the converter


def test_spawn_spots_stand_on_walkable_floor():
    """The centre of each arena (placements) lies inside the DS1's tiles: x = OffsetX*5 + tile*5 + 2."""
    places = {}
    for f in (ROOT / "plugin/placements").glob("*.cpp"):
        for enc, pid, x, y in re.findall(r'\{"(\w+)", "([\w-]+)", (0x[0-9a-f]+|\d+), (0x[0-9a-f]+|\d+), ', f.read_text("utf-8")):
            places[(enc, pid)] = (int(x, 0), int(y, 0))
    for enc, (ox, oy), stem in (("dclone", (3000, 1300), "uberdiablolvl"), ("lucion", (5000, 1200), "lucionarena")):
        x, y = places[(enc, "centre")]
        w, h = ARENAS[stem]
        tx, ty = (x - ox * 5) // 5, (y - oy * 5) // 5
        assert 0 <= tx < w and 0 <= ty < h, (enc, tx, ty)
