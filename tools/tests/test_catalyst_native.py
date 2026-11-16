"""#953: the Catalyst Shard recipe for native maps keeps PD2's ingredients and eligibility (normal maps refused, marker on layer 1)."""
import unittest
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OPS = json.loads((ROOT / "patches/072-catalyst-native.json").read_text())["ops"]
ROWS = [o["values"] for o in OPS if o["table"] == "cubemain"]


class CatalystNativeTests(unittest.TestCase):
    def test_refusal_row_comes_first_and_blocks_normal_maps(self):
        r = ROWS[0]
        assert r["op"] == "30" and r["input 1"] == "cmap,nor" and r["input 2"] == "iwss,qty=1"


    def test_recipe_takes_any_other_native_map_and_one_shard(self):
        r = ROWS[1]
        assert (r["input 1"], r["input 2"], r["numinputs"], r["output"]) == ("cmap", "iwss,qty=1", "2", "useitem")
        assert "op" not in r


    def test_marker_is_the_layered_force_event_stat(self):
        r = ROWS[1]
        assert r["mod 1"] == "map-force-event-l" and r["mod 1 param"] == "1"  # MarkerLayer, plugin/common/event_link.h
        assert r["mod 1 min"] == r["mod 1 max"] == "63"  # PD2's value


    def test_marker_property_puts_the_param_on_the_layer(self):
        p = next(o["values"] for o in OPS if o["table"] == "properties")
        assert (p["code"], p["func1"], p["stat1"]) == ("map-force-event-l", "24", "map_force_event")  # func 1 would leave layer 0
