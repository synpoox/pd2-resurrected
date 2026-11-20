"""Keep pending release notes in the format the toolkit can fold (#1525)."""
import re
import unittest
from pathlib import Path

FRAGMENTS = Path(__file__).resolve().parents[2] / "changelog.d"
DEV_CATEGORIES = {"docs", "tools"}


def fragment_errors(folder: Path) -> list[str]:
    errors = []
    seen = {}
    for fragment in sorted(folder.glob("*.md")):
        if fragment.name.lower() == "readme.md":
            continue
        filename = fragment.name
        lines = [
            (number, line.rstrip())
            for number, line in enumerate(fragment.read_text(encoding="utf-8").splitlines(), 1)
            if line.strip()
        ]
        if lines and lines[0][1].startswith("#### "):
            number, heading = lines.pop(0)
            category = heading[5:].strip()
            if category.casefold() in DEV_CATEGORIES:
                errors.append(f"{filename}:{number}: {category} is a developer-only category")
        if not lines:
            errors.append(f"{filename}: expected at least one player-facing bullet")
        for number, line in lines:
            location = f"{filename}:{number}"
            if not re.fullmatch(r"- +\S.*", line):
                errors.append(f"{location}: expected a '- ' bullet after the optional category")
                continue
            bullet = line[2:].strip()
            first = seen.setdefault(bullet, location)
            if first != location:
                errors.append(f"{location}: duplicate bullet from {first}")
    return errors


class ChangelogFragmentsTest(unittest.TestCase):
    def test_pending_fragments(self):
        self.assertEqual(fragment_errors(FRAGMENTS), [])
