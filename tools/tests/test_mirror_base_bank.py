import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import mirror_base_bank  # noqa: E402

REPO = Path(__file__).resolve().parents[2]


class MirrorBaseBankTests(unittest.TestCase):
    def test_copies_rotw_tables_and_keeps_committed_base_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            excel = root / "out/data/global/excel"
            (excel / "base").mkdir(parents=True)
            (excel / "itemstatcost.txt").write_text("rotw stats")
            (excel / "inventory.txt").write_text("rotw inventory")
            (excel / "base/inventory.txt").write_text("base inventory")
            committed = root / "files/data/global/excel/base"
            committed.mkdir(parents=True)
            (committed / "inventory.txt").write_text("base inventory")
            self.assertEqual(mirror_base_bank.mirror(root), ["itemstatcost.txt"])
            self.assertEqual((excel / "base/itemstatcost.txt").read_text(), "rotw stats")
            self.assertEqual((excel / "base/inventory.txt").read_text(), "base inventory")

    def test_base_bank_commits_only_its_inventory(self):
        # armor/weapons/everything else come from the RotW bank, so both banks number items the same (#1853)
        names = sorted(p.name for p in (REPO / "files/data/global/excel/base").glob("*.txt"))
        self.assertEqual(names, ["inventory.txt"])

    def test_every_build_mirrors(self):
        for rel in ("tools/install.ps1", ".github/workflows/release.yml"):
            self.assertIn("mirror_base_bank.py", (REPO / rel).read_text("utf-8"), rel)


if __name__ == "__main__":
    unittest.main()
