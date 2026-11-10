"""pd2_force_ds1 on a synthetic lvlprest.txt: no PD2 install needed."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_force_ds1 as f  # noqa: E402

HEAD = "Name\tDef\tLevelId\tFiles\tFile1\tFile2\tFile3\tFile4\tFile5\tFile6\tDt1Mask"
ROWS = [
    "None\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0",
    "Dark Temple Map\t1155\t200\t3\tExpansion/Map/Darktemple.ds1\tExpansion/Map/DarktempleV2.ds1\tExpansion/Map/DarktempleV3.ds1\t0\t0\t0\t4194303",
    "Town\t1\t1\t0\tAct1/Town/TownN1.ds1\tAct1/Town/TownE1.ds1\t0\t0\t0\t0\t959",
    "Border A\t4\t4\t2\tAct1/Bord1.ds1\tAct1/Bord1b.ds1\tAct1/Bord1c.ds1\t0\t0\t0\t16405",
    "Border B\t5\t4\t2\tAct1/Bord2.ds1\tAct1/Bord2b.ds1\t0\t0\t0\t0\t16405",
]


class ForceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = f.lvlprest_path(4, Path(self.tmp.name))
        self.path.parent.mkdir(parents=True)
        self.raw = "\r\n".join([HEAD, *ROWS]).encode("latin-1") + b"\r\n"
        self.path.write_bytes(self.raw)

    def tearDown(self):
        self.tmp.cleanup()

    def row(self, name):
        return next(r for r in f.Table(self.path).rows if r[0] == name)

    def test_force_by_name_and_reset_round_trips_bytes(self):
        out = f.apply(self.path, ["dark temple map=darktemplev2.ds1"])
        self.assertIn("-> Expansion/Map/DarktempleV2.ds1", out[0])
        r = self.row("Dark Temple Map")
        self.assertEqual(r[3:10], ["1", "Expansion/Map/DarktempleV2.ds1", "0", "0", "0", "0", "0"])
        self.assertEqual(r[10], "4194303")
        self.assertIn(b"\r\n", self.path.read_bytes())
        f.reset(self.path)
        self.assertEqual(self.path.read_bytes(), self.raw)
        self.assertIn("nothing to reset", f.reset(self.path))

    def test_unchanged_rewrite_is_byte_identical(self):
        self.assertEqual(f.Table(self.path).text().encode("latin-1"), self.raw)

    def test_force_by_level_id_path_and_backslash(self):
        f.apply(self.path, ["200=Expansion\\Map\\DarktempleV3.ds1"])
        self.assertEqual(self.row("Dark Temple Map")[4], "Expansion/Map/DarktempleV3.ds1")

    def test_ds1_alone(self):
        f.apply(self.path, ["Bord2b.ds1"])
        self.assertEqual(self.row("Border B")[3:5], ["1", "Act1/Bord2b.ds1"])

    def test_refuses_non_variant_and_writes_nothing(self):
        with self.assertRaises(f.ForceError) as c:
            f.apply(self.path, ["200=Bord1.ds1"])
        self.assertIn("DarktempleV2.ds1", str(c.exception))
        self.assertEqual(self.path.read_bytes(), self.raw)
        self.assertFalse(self.path.with_name(self.path.name + f.BACKUP_SUFFIX).exists())

    def test_one_bad_selector_aborts_all(self):
        with self.assertRaises(f.ForceError):
            f.apply(self.path, ["200=darktemplev2.ds1", "Nowhere=x.ds1"])
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_shared_level_id_and_files_zero_row(self):
        t = f.Table(self.path)
        f.force(t, "4=Bord1b.ds1")  # LevelId 4 is on two rows; only one holds this DS1
        with self.assertRaises(f.ForceError):
            f.force(t, "4=Bord9.ds1")
        with self.assertRaises(f.ForceError):
            f.force(t, "Town=TownE1.ds1")  # Files=0: only File1 can load

    def test_second_force_keeps_original_backup(self):
        f.apply(self.path, ["200=darktemplev2.ds1"])
        f.apply(self.path, ["Bord2b.ds1"])
        f.reset(self.path)
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_reforce_another_variant_without_reset(self):
        f.apply(self.path, ["200=darktemplev2.ds1"])
        f.apply(self.path, ["200=darktemplev3.ds1"])
        self.assertEqual(self.row("Dark Temple Map")[4], "Expansion/Map/DarktempleV3.ds1")
        with self.assertRaises(f.ForceError):
            f.apply(self.path, ["200=Bord1.ds1"])  # still not one of the original variants
        f.reset(self.path)
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_slot_guard_refusals_become_force_errors(self):
        class Refuse(Exception):
            pass

        class Cfg:
            def slot_dir(self, n):
                return str(Path(self.tmp) / f"S{n}")

        def toolkit(msg):
            tc = type("tc", (), {"load": staticmethod(lambda _p: Cfg())})
            def check(cfg, n):
                if msg:
                    raise Refuse(msg)
                return n
            return lambda: (tc, type("ri", (), {"check_slot": staticmethod(check)}))
        saved = f._toolkit
        try:
            for msg in ("slot 1 is your own game", "slot 4 is not locked by MODKIT_RUN=None", "slot 4 still has a running game"):
                f._toolkit = toolkit(msg)
                with self.assertRaises(f.ForceError) as c:
                    f.lvlprest_path(4)
                self.assertIn(msg, str(c.exception))
                self.assertEqual(f.main(["--slot", "4", "--reset"]), 1)
            Cfg.tmp = self.tmp.name
            f._toolkit = toolkit(None)
            self.assertEqual(f.lvlprest_path(4), Path(self.tmp.name) / "S4" / f.REL)
        finally:
            f._toolkit = saved

    def test_cli(self):
        argv = ["--slots-dir", self.tmp.name, "--slot", "4"]
        self.assertEqual(f.main([*argv, "200=darktemplev2.ds1"]), 0)
        self.assertEqual(f.main([*argv, "200=nope.ds1"]), 1)
        self.assertEqual(f.main([*argv, "--reset"]), 0)
        self.assertEqual(self.path.read_bytes(), self.raw)


if __name__ == "__main__":
    unittest.main()
