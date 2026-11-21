"""docs/console-commands.md names every console command our plugins register and every `core-events event` word (#1663)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugin"
DOC = (ROOT / "docs" / "console-commands.md").read_text(encoding="utf-8")


def commands():
    """Each plugin dir that registers a console command; the command is named after the plugin dir."""
    return sorted(p.parent.name for p in PLUGIN.glob("*/plugin.cpp") if "RegisterConsoleCommand" in p.read_text(encoding="utf-8"))


def event_words():
    src = (PLUGIN / "core-events" / "map_events.cpp").read_text(encoding="utf-8")
    block = re.search(r"Names\[\] = \{(.*?)\};", src, re.S).group(1)
    return re.findall(r'\{"(\w+)",', block)


def sub_command_words():
    """Sub-commands parsed in test_cmd.h and plugin.cpp (kill, life, corrupt, desecrate, event)."""
    h = (PLUGIN / "common" / "test_cmd.h").read_text(encoding="utf-8")
    p = (PLUGIN / "core-events" / "plugin.cpp").read_text(encoding="utf-8")
    return sorted(set(re.findall(r'"(kill|life|corrupt|desecrate)[ "]', h)) | set(re.findall(r'"(event) "', p)))


class ConsoleCommandsDoc(unittest.TestCase):
    def test_every_plugin_command_is_documented(self):
        found = commands()
        self.assertEqual(found, ["core-events", "core-rules", "core-ui"], "a plugin registers a command: update this test")
        for name in found:
            self.assertIn(f"`{name}", DOC, f"console command {name} is not in docs/console-commands.md")

    def test_every_core_events_word_is_documented(self):
        words = event_words()
        self.assertIn("invaders", words)
        for word in words:
            self.assertIn(f"`{word}`", DOC, f"core-events event word {word} is not in docs/console-commands.md")
        for sub in sub_command_words():
            self.assertIn(f"`core-events {sub}", DOC, f"core-events {sub} is not in docs/console-commands.md")

    def test_usage_text_names_every_word(self):
        src = (PLUGIN / "core-events" / "plugin.cpp").read_text(encoding="utf-8")
        usage = re.search(r'event ((?:\w+\|)+\w+)"', src).group(1).split("|")
        self.assertEqual(sorted(usage), sorted(event_words()))


if __name__ == "__main__":
    unittest.main()
