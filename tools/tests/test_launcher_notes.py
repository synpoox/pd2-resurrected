"""Launcher patch notes split into one section per heading at their shallowest level (resurrected-launcher
notesMarkdown.parseNotes), so a version heading above the categories made one section of everything (#2113)."""
from pathlib import Path
import re
import unittest

NOTES = Path(__file__).resolve().parents[2] / "release" / "notes"


class LauncherNotes(unittest.TestCase):
    def test_summary_then_category_sections(self):
        for path in NOTES.glob("*.md"):
            text = path.read_text("utf-8").strip()
            levels = [len(m.group(1)) for m in re.finditer(r"^(#{1,6})\s", text, re.M)]
            with self.subTest(path.name):
                self.assertFalse(text.startswith("#"), "start with a summary paragraph; the launcher shows version and date")
                self.assertGreater(levels.count(min(levels, default=0)), 1, "the top heading level must be the categories")


if __name__ == "__main__":
    unittest.main()
