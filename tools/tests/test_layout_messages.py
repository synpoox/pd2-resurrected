"""Every UI layout message is "Target:Command[:Params]": an empty or colon-less one asserts in D2UI TypeDesc.cpp (#1649)."""
import re
import unittest
from pathlib import Path

LAYOUTS = Path(__file__).resolve().parents[2] / "files/data/global/ui/layouts"
# A "...Message" key's string, or each string in a "...Messages" array (layouts hold comments, so no json.loads)
MESSAGE = re.compile(r'"\w*Messages?"\s*:\s*("[^"]*"|\[[^\]]*\])')


class LayoutMessages(unittest.TestCase):
    def test_messages_have_target_and_command(self):
        bad = []
        for layout in sorted(LAYOUTS.rglob("*.json")):
            for match in MESSAGE.finditer(layout.read_text("utf-8")):
                for value in re.findall(r'"([^"]*)"', match.group(1)):
                    if not re.fullmatch(r"\w+:\w+(:.*)?", value):
                        bad.append(f"{layout.relative_to(LAYOUTS)}: {value!r}")
        self.assertEqual(bad, [])


if __name__ == "__main__":
    unittest.main()
