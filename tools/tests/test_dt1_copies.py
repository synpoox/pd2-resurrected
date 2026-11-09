"""pd2_dt1_copies on synthetic DT1 with images, and the generator naming a reindexed copy by its D2R tile."""
import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_dt1_copies as C  # noqa: E402
import pd2_presets as P  # noqa: E402
from test_presets import TERRAIN, ds1, stock  # noqa: E402


def dt1(tiles: list[tuple[tuple[int, int, int], bytes | None]], blocked: set = frozenset()) -> bytes:
    """A DT1 of (key, pixels) tiles: each with pixels gets one block holding them, None no block."""
    head = bytearray(276 + 96 * len(tiles))
    struct.pack_into("<2i", head, 0, 7, 6)
    struct.pack_into("<2i", head, 268, len(tiles), 276)
    body = bytearray()
    for k, (key, pixels) in enumerate(tiles):
        h = 276 + 96 * k
        struct.pack_into("<3i", head, h + 20, *key)
        if key in blocked:
            head[h + 40:h + 65] = b"\1" * 25
        if pixels is not None:
            ptr = len(head) + len(body)
            struct.pack_into("<3i", head, h + 72, ptr, 20 + len(pixels), 1)
            body += struct.pack("<hhhbbhihi", 0, 0, 0, 0, 0, 1, len(pixels), 0, 20) + pixels
    return bytes(head + body)


class CopiesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d2r = Path(self.tmp.name)
        for path, data in {"act2/arcane/Sanctuary.dt1": dt1([((1, 2, 3), b"stairs"), ((0, 0, 0), None)]),
                           "act1/town/floor.dt1": dt1([((0, 1, 0), b"grass"), ((1, 2, 3), b"grass")])}.items():
            (self.d2r / path).parent.mkdir(parents=True, exist_ok=True)
            (self.d2r / path).write_bytes(data)

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_reindexed_tile_maps_to_the_d2r_tile_with_its_image(self):
        pd2 = {"pd2assets/sanctuaryreindexed.dt1": dt1([((9, 9, 9), b"stairs"), ((1, 2, 3), b"new art")])}
        self.assertEqual(C.copies(pd2, C.d2r_images(self.d2r), {}),
                         {("pd2assets/sanctuaryreindexed.dt1", 9, 9, 9): ("act2/arcane/sanctuary.dt1", 1, 2, 3)})

    def test_same_pixels_with_other_subtile_flags_or_no_image_are_no_copy(self):
        pd2 = {"pd2assets/x.dt1": dt1([((1, 2, 3), b"stairs"), ((0, 0, 0), None)], blocked={(1, 2, 3)})}
        self.assertEqual(C.copies(pd2, C.d2r_images(self.d2r), {}), {})

    def test_of_several_homes_the_stand_in_comes_first_then_the_same_index(self):
        images = C.d2r_images(self.d2r)
        pd2 = {"pd2assets/x.dt1": dt1([((1, 2, 3), b"grass")])}
        self.assertEqual(C.copies(pd2, images, {})[("pd2assets/x.dt1", 1, 2, 3)], ("act1/town/floor.dt1", 1, 2, 3))
        pd2 = {"pd2assets/x.dt1": dt1([((5, 5, 5), b"grass")])}
        self.assertEqual(C.copies(pd2, images, {})[("pd2assets/x.dt1", 5, 5, 5)], ("act1/town/floor.dt1", 0, 1, 0))


class LayoutTests(unittest.TestCase):
    def test_a_copied_tile_is_named_from_its_own_dt1_not_an_earlier_one_with_the_same_index(self):
        data = ds1(1, 1, walls=[], floors=[{(0, 0): (1, 2)}])
        f = b"\x00" * 25
        dt1s = {"act2/palace/cellar.dt1": {(0, 1, 2): f}, "pd2assets/x.dt1": {(0, 1, 2): f}}
        copies = {("pd2assets/x.dt1", 0, 1, 2): ("act2/arcane/sanctuary.dt1", 0, 1, 2)}
        self.assertEqual(P.layout(data, dt1s, copies).keys[(0, 0)], ("f", (("act2/arcane/sanctuary.dt1", 0, 1, 2),)))
        self.assertEqual(P.layout(data, dt1s).keys[(0, 0)], ("f", (("act2/palace/cellar.dt1", 0, 1, 2),)))


class GeneratorTests(unittest.TestCase):
    def test_a_preset_draws_the_stock_pieces_of_the_d2r_tile_a_pd2_tile_copies_at_another_index(self):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            rule, d2r, out = t / "presets-hd.tsv", t / "d2r", t / "out"

            def write(path: Path, data):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data if isinstance(data, bytes) else data.replace("|", "\t").encode())
            write(d2r / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1\nTown|0|1|Act1/S.ds1\n")
            write(out / "global/excel/lvlprest.txt", "Name|LevelId|Dt1Mask|File1|File2|File3|File4|File5|File6\n"
                  "Town|0|1|Act1/S.ds1|||||\nPd2 Map|1|1|Expansion/Map/P.ds1|||||\n")
            write(out / "global/excel/levels.txt", "Name|LevelType\nTown|0\nPd2 Map|1\n")
            write(out / "global/excel/lvltypes.txt", "Name|File 1\nAct 1 - Town|Act1/A/Wall.dt1\nPd2 Set|pd2assets/x.dt1\n")
            # stock: tile (1,2,3) at x=0 holds the pillar, tile (4,5,6) at x=1 the torch
            write(d2r / "global/tiles/act1/a/wall.dt1", dt1([((1, 2, 3), b"pillar art"), ((4, 5, 6), b"torch art")]))
            write(d2r / "global/tiles/act1/s.ds1", ds1(2, 1, [{(0, 0): (1, 2, 3), (1, 0): (4, 5, 6)}], [{}],
                                                       ["\\d2\\data\\global\\tiles\\act1\\a\\wall.tg1"]))
            write(d2r / "hd/env/preset/act1/s.json", json.dumps(stock()).encode())
            write(d2r / f"hd/env/preset/{P.TERRAIN}.json", json.dumps({"terrain": TERRAIN}).encode())
            # PD2 reindexes: its (1,2,3) is D2R's torch tile, its (7,7,7) new art; no D2R DT1 holds 25% at the same index
            write(out / "global/tiles/pd2assets/x.dt1", dt1([((1, 2, 3), b"torch art"), ((7, 7, 7), b"new art")]))
            write(out / "global/tiles/expansion/map/p.ds1", ds1(2, 1, [{(0, 0): (1, 2, 3), (1, 0): (7, 7, 7)}], [{}]))
            rule.write_text("Pd2 Set\tb.json\npd2assets/x.dt1\t-\n")
            with redirect_stdout(io.StringIO()):
                P.apply(rule, Path(), d2r, out)
            preset = json.loads((out / "hd/env/preset/expansion/map/p.json").read_text())
            # the torch, at x 0 (PD2's tile 0), not the pillar its index names in D2R; the new tile stays empty
            self.assertEqual([(e["name"], e["components"][0]["position"]["x"]) for e in preset["entities"]],
                             [("torch", 4.0)])


if __name__ == "__main__":
    unittest.main()
