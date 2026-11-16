"""docs/deviations.md: every table row has as many cells as its header (no hand-kept row counts, they collided across PRs)."""
import unittest
from pathlib import Path

DOC = Path(__file__).resolve().parents[2] / "docs" / "deviations.md"


def bad_rows(text: str) -> list[str]:
    """Table rows whose cell count differs from their table's header row."""
    bad, width = [], None
    for line in text.splitlines():
        if not line.startswith("|"):
            width = None
            continue
        cells = len(line.strip().strip("|").replace("\|", "").split("|"))
        if width is None:
            width = cells
        elif cells != width:
            bad.append(line)
    return bad


class DeviationTables(unittest.TestCase):
    def test_parser(self):
        self.assertEqual(bad_rows("| a | b |\n|---|---|\n| 1 | 2 |\n| 3 |\n"), ["| 3 |"])

    def test_rows_match_header(self):
        self.assertEqual(bad_rows(DOC.read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
