"""Launcher patch notes split into one section per heading at their shallowest level (resurrected-launcher
notesMarkdown.parseNotes), so a version heading above the categories made one section of everything (#2113)."""
from pathlib import Path
import re
import unittest

NOTES = Path(__file__).resolve().parents[2] / "release" / "notes"
# Published before this test; a published release's manifest never changes (the launcher refuses a changed one).
SHIPPED = {"0.2.5-alpha.md"}
HEADING = re.compile(r" {0,3}(#{1,6})(?:[ \t]|$)")  # the launcher's ATX heading: up to 3 spaces of indent


def problems(text: str) -> list[str]:
    """Why the launcher would not show `text` as a summary and one section per category."""
    levels, fenced, first = [], False, None
    for line in text.splitlines():
        if line.lstrip(" ").startswith(("```", "~~~")):
            fenced = not fenced
        if fenced or not line.strip():
            continue
        m = HEADING.match(line)
        if first is None:
            first = "heading" if m else "text"
        if m:
            levels.append(len(m.group(1)))
    bad = ["start with a summary paragraph; the launcher shows the version and date"] if first == "heading" else []
    top = min(levels, default=0)
    if levels.count(top) == 1 and any(level > top for level in levels):
        bad.append("one heading wraps the others: make the categories the top heading level")
    return bad


class LauncherNotes(unittest.TestCase):
    def test_shapes(self):
        wrapped = "### 0.2.5-alpha (2026-11-23)\n\n#### Items\n\n- a\n\n#### Bug Fixes\n\n- b\n"
        self.assertEqual(len(problems(wrapped)), 2)
        self.assertEqual(len(problems("Summary.\n\n### 1.0 (date)\n\n#### Items\n\n- a\n")), 1)
        self.assertEqual(problems("Summary.\n\n## Items\n\n- a\n\n## Bug Fixes\n\n- b\n"), [])
        self.assertEqual(problems("Summary.\n\n## Fixes\n\n- a\n"), [])
        self.assertEqual(problems("Summary.\n\n## Fixes\n\n```\n# not a heading\n```\n"), [])

    def test_release_notes(self):
        for path in NOTES.glob("*.md"):
            if path.name not in SHIPPED:
                with self.subTest(path.name):
                    self.assertEqual(problems(path.read_text("utf-8")), [])


if __name__ == "__main__":
    unittest.main()
