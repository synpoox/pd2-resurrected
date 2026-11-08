"""docs/deviations.md (#575): each "### Name (n)" heading's n matches its table's row count."""
import re
import unittest
from pathlib import Path

DOC = Path(__file__).resolve().parents[2] / "docs" / "deviations.md"


def heading_counts(text: str) -> list[tuple[str, int, int]]:
    """(heading, stated count, table rows) for each counted heading; rows exclude the header and separator."""
    out, current = [], None
    for line in text.splitlines():
        if line.startswith("#"):
            m = re.fullmatch(r"#+ (.*) \((\d+)\)", line.strip())
            current = [m.group(1), int(m.group(2)), -2] if m else None
            if current:
                out.append(current)
        elif current and line.startswith("|"):
            current[2] += 1
    return [(h, n, max(rows, 0)) for h, n, rows in out]


class DeviationCounts(unittest.TestCase):
    def test_parser(self):
        doc = "## A\n### Cut (2)\n\n| h |\n|---|\n| a |\n| b |\n### Not yet (1)\n| h |\n|---|\n"
        self.assertEqual(heading_counts(doc), [("Cut", 2, 2), ("Not yet", 1, 0)])

    def test_headings_match_rows(self):
        counts = heading_counts(DOC.read_text(encoding="utf-8"))
        self.assertTrue(counts)
        for heading, stated, rows in counts:
            self.assertEqual(stated, rows, f"{heading} ({stated}) has {rows} rows")


if __name__ == "__main__":
    unittest.main()
